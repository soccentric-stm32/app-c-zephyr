/*
 * comms.c - HWLINK Comms thread
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZFN-003 (COM-001..COM-007), ZFN-005
 *
 * One thread parses COBS frames from the interrupt-driven USART3 ring,
 * verifies every FlatBuffer, executes the DEMO request set and sends the
 * reply.  Repeated request sequence numbers resend the previous response
 * without executing the request again.  All buffers are static and flatcc
 * uses a fixed arena, so no thread allocates after start-up.
 */

#include <soc.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/version.h>

#include "app/zdemo.h"
#include "board/led.h"
#include "boot/boot_contract.h"
#include "boot/handover.h"
#include "boot/identity.h"
#include "comms/comms.h"
#include "comms/hwlink.h"
#include "comms/uart_hwlink.h"
#include "flatcc/flatcc_builder.h"
#include "hwlink_builder.h"
#include "hwlink_verifier.h"
#include "system/app_fault.h"
#include "system/health.h"
#include "system/reset_cause.h"

/* Number of events the comms work queue can hold. */
#define COMMS_EVENT_QUEUE_LEN 8U
/* Maximum time the comms thread sleeps waiting for work. */
#define COMMS_LOOP_MS         10U
/* Interval between comms-thread check-ins to the health monitor. */
#define COMMS_CHECKIN_MS      500U
/* Per-transmission UART send timeout. */
#define COMMS_TX_TIMEOUT_MS   200U
/* Idle time after which a non-default baud rate reverts to the default. */
#define COMMS_BAUD_REVERT_MS  5000U
/* Maximum number of threads reported in a Status message. */
#define COMMS_MAX_TASKS       12U

/**
 * @brief Types of work items delivered to the comms thread.
 */
typedef enum {
	COMMS_EVT_RX = 0,	/** New data available on the USART RX ring. */
	COMMS_EVT_BUTTON	/** Button press/released; report if link open. */
} comms_event_type_t;

/**
 * @brief Work item carried in the comms message queue.
 *
 * Field meanings depend on @ref comms_event_type_t; unused fields may
 * contain stale data.
 */
typedef struct {
	uint8_t type;
	uint8_t press;
	uint8_t pattern;
	uint16_t period;
} comms_event_t;

/**
 * @brief Shadow copy of LED state used when building Status responses.
 */
typedef struct {
	uint8_t pattern;
	uint16_t period;
} led_mirror_t;

/** Message queue between the RX ISR / button thread and the comms thread. */
K_MSGQ_DEFINE(comms_msgq, sizeof(comms_event_t), COMMS_EVENT_QUEUE_LEN, 4);

/** Fixed-arena FlatCC builder used for all outgoing messages. */
static flatcc_builder_t builder;

/** Frame assembly.  rx_enc is the COBS body without its delimiter. */
/** COBS-encoded receive body being accumulated (no delimiter yet). */
static uint8_t rx_enc[HWLINK_MAX_ENCODED];
/** Number of valid bytes in rx_enc. */
static size_t rx_enc_len;
/** True if the receive body exceeded the frame limit (drop until 0x00). */
static bool rx_enc_overflow;

/** Decoded (COBS-decoded, not yet deframed) receive buffer. */
static uint8_t rx_raw[HWLINK_MAX_FRAME];
/** Deframed FlatBuffer payload extracted from the last receive frame. */
static uint8_t rx_payload[HWLINK_MAX_FRAME];
/** Outgoing frame, built before COBS encoding. */
static uint8_t raw_tx[HWLINK_MAX_FRAME];

/** The last reply, kept as an unencoded frame for duplicate requests. */
/** Last sent response frame, raw (unencoded) for duplicate re-sends. */
static uint8_t last_response[HWLINK_MAX_FRAME];
/** Length of last_response in bytes. */
static size_t last_response_len;
/** Sequence number of the last executed request. */
static uint16_t last_request_seq;
/** True if last_request_seq refers to an executed request. */
static bool last_request_valid;

/** COBS-encoded transmit buffer; aligned for the flash write path. */
static uint8_t tx_frame[HWLINK_MAX_ENCODED] __aligned(32);

static led_mirror_t led_mirror[3] = {
	{ APP_PATTERN_HEARTBEAT, APP_LED_DEFAULT_PERIOD },
	{ APP_PATTERN_OFF,       APP_LED_DEFAULT_PERIOD },
	{ APP_PATTERN_OFF,       APP_LED_DEFAULT_PERIOD }
};

static bool link_open;
static bool baud_revert_pending;
static uint32_t baud_last_valid;


/**
 * @brief Return the 64-bit device UID.
 *
 * Uses the same two UID words, in the same order, as the C demo's
 * HAL_GetUIDw0()/HAL_GetUIDw1() expression.
 */
static uint64_t device_uid(void)
{
	/* Same two UID words, in the same order, as the C demo's
	 * HAL_GetUIDw0()/HAL_GetUIDw1() expression. */
	uint32_t word0 = *(volatile uint32_t *)UID_BASE;
	uint32_t word1 = *(volatile uint32_t *)(UID_BASE + 4U);

	return ((uint64_t)word0 << 32U) | (uint64_t)word1;
}

/**
 * @brief Return the DBGMCU part/revision IDCODE.
 *
 * DBGMCU is clocked from the debug domain on the F7 and is readable
 * without an application clock gate.
 */
static uint32_t device_idcode(void)
{
	/* DBGMCU is clocked from the debug domain on the F7 and is readable
	 * without an application clock gate. */
	return DBGMCU->IDCODE;
}

/**
 * @brief Map the flash bank configuration to the HWLINK flash-mode enum.
 */
static hwlink_FlashMode_enum_t flash_mode(void)
{
	return (FLASH->OPTCR & FLASH_OPTCR_nDBANK) != 0U
	       ? hwlink_FlashMode_SINGLE_BANK : hwlink_FlashMode_DUAL_BANK;
}

/**
 * @brief Reset the receive parser state (discard the partial frame).
 */
static void reset_parser(void)
{
	rx_enc_len = 0U;
	rx_enc_overflow = false;
}

/**
 * @brief Build, optionally store, encode and transmit one raw frame.
 *
 * @param payload     FlatBuffer payload bytes.
 * @param payload_len Length of payload in bytes.
 * @param flags       Frame flags field.
 * @param seq         Frame sequence number.
 * @param store       If true, keep the raw frame for duplicate re-sends.
 * @return true if the full transaction succeeded.
 */
static bool send_payload(const uint8_t *payload, size_t payload_len,
			 uint8_t flags, uint16_t seq, bool store)
{
	size_t raw_len = 0U;
	size_t encoded_len = 0U;

	if (!hwlink_build_raw(payload, payload_len, flags, seq, raw_tx,
			      sizeof(raw_tx), &raw_len)) {
		return false;
	}
	if (store && raw_len <= sizeof(last_response)) {
		memcpy(last_response, raw_tx, raw_len);
		last_response_len = raw_len;
	}
	if (!hwlink_cobs_encode(raw_tx, raw_len, tx_frame, sizeof(tx_frame),
				&encoded_len)) {
		return false;
	}
	return hwlink_uart_send(tx_frame, (uint16_t)encoded_len,
				COMMS_TX_TIMEOUT_MS);
}

/**
 * @brief Serialize a FlatBuffer body, then send it as a raw frame.
 *
 * The fixed builder arena is reset after the send completes.
 *
 * @param body   FlatBuffer body reference (already created).
 * @param seq    Frame sequence number.
 * @param flags  Frame flags field.
 * @param store  If true, keep the raw frame for duplicate re-sends.
 * @return true on success.
 */
static bool send_body(hwlink_Body_union_ref_t body, uint16_t seq,
		      uint8_t flags, bool store)
{
	if (hwlink_Message_create_as_root(&builder, body) == 0) {
		flatcc_builder_reset(&builder);
		return false;
	}

	size_t size = 0U;
	void *direct = flatcc_builder_get_direct_buffer(&builder, &size);

	if (direct == NULL || size == 0U || size > sizeof(rx_payload)) {
		flatcc_builder_reset(&builder);
		return false;
	}

	bool ok = send_payload(direct, size, flags, seq, store);

	flatcc_builder_reset(&builder);
	return ok;
}

/**
 * @brief Send an Ack response for the given sequence number.
 *
 * @param seq Sequence number to echo.
 * @return true if the Ack was sent.
 */
static bool send_ack(uint16_t seq)
{
	flatcc_builder_reset(&builder);
	hwlink_Ack_ref_t ack = hwlink_Ack_create(&builder, seq, 0U);

	return ack != 0 &&
	       send_body(hwlink_Body_as_Ack(ack), seq, HWLINK_FLAG_RESPONSE, true);
}

/**
 * @brief Send a Nak response with an error code and optional detail text.
 *
 * @param seq             Sequence number of the failed request.
 * @param error           HWLINK error code.
 * @param detail          Human-readable detail text (may be NULL).
 * @param expected_offset Expected CRC offset, or 0 for none.
 * @return true if the Nak was sent.
 */
static bool send_nak(uint16_t seq, hwlink_Error_enum_t error,
		     const char *detail, uint32_t expected_offset)
{
	flatcc_builder_reset(&builder);
	flatbuffers_string_ref_t text =
		flatbuffers_string_create_str(&builder, detail != NULL ? detail : "");
	hwlink_Nak_ref_t nak =
		hwlink_Nak_create(&builder, seq, error, text, expected_offset);

	return nak != 0 &&
	       send_body(hwlink_Body_as_Nak(nak), seq, HWLINK_FLAG_RESPONSE, true);
}

/**
 * @brief Send a Pong response carrying the current uptime.
 *
 * @param seq Sequence number to echo.
 * @return true if the Pong was sent.
 */
static bool send_pong(uint16_t seq)
{
	flatcc_builder_reset(&builder);
	hwlink_Pong_ref_t pong =
		hwlink_Pong_create(&builder, (uint64_t)k_uptime_get());

	return pong != 0 &&
	       send_body(hwlink_Body_as_Pong(pong), seq, HWLINK_FLAG_RESPONSE,
			 true);
}

/**
 * @brief Build and send the HelloReply identifying this firmware.
 *
 * Includes name, version, commit, build date, identity detail, UID,
 * device/revision IDs, flash mode and protocol version.
 *
 * @param seq Request sequence number to echo.
 * @return true if the HelloReply was sent.
 */
static bool send_hello_reply(uint16_t seq)
{
	flatcc_builder_reset(&builder);

	flatbuffers_string_ref_t name =
		flatbuffers_string_create_str(&builder, APP_FIRMWARE_NAME);
	flatbuffers_string_ref_t version = flatbuffers_string_create_str(
		&builder, APP_VERSION_STR " (Zephyr " KERNEL_VERSION_STRING ")");
	flatbuffers_string_ref_t commit =
		flatbuffers_string_create_str(&builder, APP_GIT_COMMIT);
	flatbuffers_string_ref_t build =
		flatbuffers_string_create_str(&builder, APP_BUILD_DATE);
	char identity_detail[192];
	(void)app_identity_detail(identity_detail, sizeof(identity_detail));
	flatbuffers_string_ref_t detail =
		flatbuffers_string_create_str(&builder, identity_detail);

	uint32_t idcode = device_idcode();
	uint32_t dev_id = idcode & 0xFFFU;
	uint32_t rev_id = (idcode >> 16U) & 0xFFFFU;

	hwlink_HelloReply_ref_t reply = hwlink_HelloReply_create(
		&builder, hwlink_Role_DEMO, name, version, commit, build, detail,
		device_uid(), dev_id, rev_id, flash_mode(),
		(uint16_t)HWLINK_PROTOCOL_MAJOR, (uint16_t)HWLINK_PROTOCOL_MINOR,
		(uint16_t)HWLINK_MAX_FRAME);

	return reply != 0 &&
	       send_body(hwlink_Body_as_HelloReply(reply), seq,
			 HWLINK_FLAG_RESPONSE, true);
}

/**
 * @brief Map a Zephyr thread state string to the compact HWLINK code.
 *
 * Uses the same numbering as the FreeRTOS demo's eTaskState.
 *
 * @param thread Thread to inspect.
 * @return 0 running, 1 ready, 2 blocked, 3 suspended, 4 deleted.
 */
static uint8_t thread_state_code(k_tid_t thread)
{
	char text[24];

	(void)k_thread_state_str(thread, text, sizeof(text));

	/* Same compact numbering as the FreeRTOS demo's eTaskState. */
	if (strstr(text, "running") != NULL) {
		return 0U;   /* eRunning */
	}
	if (strstr(text, "queued") != NULL || strstr(text, "ready") != NULL) {
		return 1U;   /* eReady */
	}
	if (strstr(text, "pending") != NULL ||
	    strstr(text, "sleeping") != NULL) {
		return 2U;   /* eBlocked */
	}
	if (strstr(text, "suspended") != NULL) {
		return 3U;   /* eSuspended */
	}
	if (strstr(text, "dead") != NULL) {
		return 4U;   /* eDeleted */
	}
	return 0U;
}

/**
 * @brief Return the FreeRTOS-style stack high-water mark (min free space).
 *
 * The HWLINK field is the minimum remaining stack space in bytes, not
 * the amount used. Returns 0 if the query fails.
 *
 * @param thread Thread to inspect.
 * @return Minimum unused stack space in bytes (clamped to 16 bits).
 */
static unsigned int task_high_water(k_tid_t thread)
{
	size_t unused = 0U;

	/* The HWLINK field is the stack high-water mark in the FreeRTOS sense:
	 * the minimum remaining stack space, not the amount used. */
	if (k_thread_stack_space_get(thread, &unused) != 0) {
		return 0U;
	}
	if (unused > UINT16_MAX) {
		unused = UINT16_MAX;
	}
	return (unsigned int)unused;
}

/**
 * @brief Per-thread information reported in a Status message.
 */
struct task_info {
	char name[CONFIG_THREAD_MAX_NAME_LEN];
	uint8_t priority;
	uint8_t state;
	uint16_t stack_high_water;
};

/**
 * @brief Scratch collector for the k_thread_foreach callback results.
 */
struct task_collect {
	struct task_info tasks[COMMS_MAX_TASKS];
	uint32_t count;
};

/**
 * @brief k_thread_foreach callback: append one thread's info to the collector.
 *
 * @param thread    Thread being visited.
 * @param user_data Pointer to the collecting structure.
 */
static void collect_task(const struct k_thread *thread, void *user_data)
{
	struct task_collect *collect = user_data;

	if (collect->count >= COMMS_MAX_TASKS) {
		return;
	}

	const char *name = k_thread_name_get((k_tid_t)thread);
	struct task_info *info = &collect->tasks[collect->count];

	if (name == NULL || name[0] == '\0') {
		name = "?";
	}
	strncpy(info->name, name, sizeof(info->name) - 1U);
	info->name[sizeof(info->name) - 1U] = '\0';
	info->priority = (uint8_t)k_thread_priority_get((k_tid_t)thread);
	info->state = thread_state_code((k_tid_t)thread);
	info->stack_high_water = (uint16_t)task_high_water((k_tid_t)thread);
	collect->count++;
}

/**
 * @brief Build and send a full Status response (tasks, LEDs, fault, heap).
 *
 * @param seq Request sequence number to echo.
 * @return true if the Status message was sent.
 */
static bool send_status(uint16_t seq)
{
	struct task_collect collect = { 0 };

	k_thread_foreach(collect_task, &collect);

	flatcc_builder_reset(&builder);

	hwlink_TaskStats_ref_t task_refs[COMMS_MAX_TASKS];

	for (uint32_t i = 0U; i < collect.count; i++) {
		flatbuffers_string_ref_t name = flatbuffers_string_create_str(
			&builder, collect.tasks[i].name);

		task_refs[i] = hwlink_TaskStats_create(
			&builder, name, collect.tasks[i].priority,
			collect.tasks[i].state,
			collect.tasks[i].stack_high_water);
	}

	hwlink_LedState_ref_t led_refs[3];

	for (uint32_t i = 0U; i < 3U; i++) {
		led_refs[i] = hwlink_LedState_create(
			&builder, (hwlink_LedId_enum_t)(i + 1U),
			(hwlink_LedPattern_enum_t)led_mirror[i].pattern,
			led_mirror[i].period);
	}

	bool fault_present = app_fault_is_valid();
	hwlink_FaultInfo_ref_t fault = 0;

	if (fault_present) {
		flatbuffers_string_ref_t fault_reason = flatbuffers_string_create_str(
			&builder,
			app_fault_reason_text((app_fault_reason_t)g_app_fault.reason));
		flatbuffers_string_ref_t fault_task = flatbuffers_string_create_str(
			&builder, app_fault_task());
		flatbuffers_string_ref_t fault_file = flatbuffers_string_create_str(
			&builder, app_fault_file());

		fault = hwlink_FaultInfo_create(
			&builder, true, fault_reason, g_app_fault.cfsr,
			g_app_fault.hfsr, g_app_fault.mmfar, g_app_fault.bfar,
			g_app_fault.pc, g_app_fault.lr, fault_task, fault_file,
			g_app_fault.line);
	}

	uint32_t heap_free = 0U;
	uint32_t heap_min_free = 0U;

	app_heap_stats(&heap_free, &heap_min_free);

	flatbuffers_string_ref_t name =
		flatbuffers_string_create_str(&builder, APP_FIRMWARE_NAME);
	flatbuffers_string_ref_t version = flatbuffers_string_create_str(
		&builder, APP_VERSION_STR " (Zephyr " KERNEL_VERSION_STRING ")");
	flatbuffers_string_ref_t commit =
		flatbuffers_string_create_str(&builder, APP_GIT_COMMIT);
	flatbuffers_string_ref_t build =
		flatbuffers_string_create_str(&builder, APP_BUILD_DATE);
	flatbuffers_string_ref_t reset_text =
		flatbuffers_string_create_str(&builder, app_reset_cause_text);

	hwlink_Status_start(&builder);
	hwlink_Status_firmware_name_add(&builder, name);
	hwlink_Status_version_add(&builder, version);
	hwlink_Status_commit_add(&builder, commit);
	hwlink_Status_build_time_add(&builder, build);
	hwlink_Status_uid_add(&builder, device_uid());
	hwlink_Status_uptime_ms_add(&builder, (uint64_t)k_uptime_get());
	hwlink_Status_slot_add(&builder, (hwlink_Slot_enum_t)g_handover.slot);
	hwlink_Status_trial_add(&builder,
				(hwlink_TrialState_enum_t)g_handover.trial);
	hwlink_Status_trial_count_add(&builder, g_handover.trial_count);
	hwlink_Status_health_add(&builder,
				 (hwlink_HealthState_enum_t)health_state_get());
	hwlink_Status_reset_cause_add(&builder, app_reset_cause_flags);
	hwlink_Status_reset_cause_text_add(&builder, reset_text);
	if (fault != 0) {
		hwlink_Status_fault_add(&builder, fault);
	}

	hwlink_Status_leds_start(&builder);
	for (uint32_t i = 0U; i < 3U; i++) {
		hwlink_Status_leds_push(&builder, led_refs[i]);
	}
	hwlink_Status_leds_end(&builder);

	hwlink_Status_tasks_start(&builder);
	for (uint32_t i = 0U; i < collect.count; i++) {
		hwlink_Status_tasks_push(&builder, task_refs[i]);
	}
	hwlink_Status_tasks_end(&builder);

	hwlink_Status_heap_free_add(&builder, heap_free);
	hwlink_Status_heap_min_free_add(&builder, heap_min_free);
	hwlink_Status_protocol_major_add(&builder,
					 (uint16_t)HWLINK_PROTOCOL_MAJOR);
	hwlink_Status_protocol_minor_add(&builder,
					 (uint16_t)HWLINK_PROTOCOL_MINOR);

	hwlink_Status_ref_t status = hwlink_Status_end(&builder);

	if (status == 0) {
		flatcc_builder_reset(&builder);
		return false;
	}
	return send_body(hwlink_Body_as_Status(status), seq,
			 HWLINK_FLAG_RESPONSE, true);
}

/**
 * @brief Build and send a ButtonEvent message (EVENT flag, not stored).
 *
 * @param press   Press classification.
 * @param pattern Current LED pattern.
 * @param period  Current LED period in ms.
 * @return true if the event was sent.
 */
static bool send_button_event(uint8_t press, uint8_t pattern, uint16_t period)
{
	flatcc_builder_reset(&builder);
	hwlink_ButtonEvent_ref_t event = hwlink_ButtonEvent_create(
		&builder, (hwlink_ButtonPress_enum_t)press,
		(hwlink_LedPattern_enum_t)pattern, period);

	return event != 0 &&
	       send_body(hwlink_Body_as_ButtonEvent(event), 0U,
			 HWLINK_FLAG_EVENT, false);
}

/**
 * @brief Validate an LED blink period against the allowed range.
 *
 * @param period Period in ms; must be within range and a multiple of 10.
 * @return true if valid.
 */
static bool period_valid(uint16_t period)
{
	return period >= APP_LED_MIN_PERIOD && period <= APP_LED_MAX_PERIOD &&
	       (period % 10U) == 0U;
}

/**
 * @brief Validate an LED pattern id.
 *
 * @param pattern Pattern id.
 * @return true if the id is a known pattern.
 */
static bool pattern_valid(uint8_t pattern)
{
	return pattern < APP_PATTERN_COUNT;
}

/**
 * @brief Update the LED state mirror so Status reflects the new command.
 *
 * Handles the CHASE special case spanning LED1/LED2 and the reserved LD3.
 *
 * @param led     LED id (1..3).
 * @param pattern New pattern id.
 * @param period  New period in ms.
 */
static void mirror_set(uint8_t led, uint8_t pattern, uint16_t period)
{
	if (led == APP_LED_ID_1 || led == APP_LED_ID_2) {
		if (pattern == APP_PATTERN_CHASE) {
			led_mirror[0].pattern = APP_PATTERN_CHASE;
			led_mirror[0].period = period;
			led_mirror[1].pattern = APP_PATTERN_CHASE;
			led_mirror[1].period = period;
			return;
		}
		if (led_mirror[0].pattern == APP_PATTERN_CHASE ||
		    led_mirror[1].pattern == APP_PATTERN_CHASE) {
			led_mirror[led == APP_LED_ID_1 ? 1 : 0].pattern =
				APP_PATTERN_OFF;
			led_mirror[led == APP_LED_ID_1 ? 1 : 0].period = period;
		}
		led_mirror[led - 1U].pattern = pattern;
		led_mirror[led - 1U].period = period;
	} else if (led == APP_LED_ID_3) {
		led_mirror[2].pattern = pattern;
		led_mirror[2].period = period;
	}
}

/**
 * @brief Process a SetLed request: validate, apply, then ACK (or NAK).
 *
 * @param set Decoded SetLed table.
 * @param seq Sequence number of the request.
 */
static void handle_set_led(const hwlink_SetLed_table_t set, uint16_t seq)
{
	uint8_t led = (uint8_t)hwlink_SetLed_led(set);
	uint8_t pattern = (uint8_t)hwlink_SetLed_pattern(set);
	uint16_t period = hwlink_SetLed_period_ms(set);
	bool force = hwlink_SetLed_force(set) != 0U;

	if (!pattern_valid(pattern) || !period_valid(period) ||
	    led < APP_LED_ID_1 || led > APP_LED_ID_3) {
		(void)send_nak(seq, hwlink_Error_BAD_ARGUMENT,
			       "LED, pattern or period", 0U);
		return;
	}
	if (led == APP_LED_ID_3 && !force) {
		(void)send_nak(seq, hwlink_Error_BAD_ARGUMENT,
			       "LD3 is reserved; force required", 0U);
		return;
	}
	if (led_post_set(led, pattern, period, force) != 0) {
		(void)send_nak(seq, hwlink_Error_BUSY, "LED queue full", 0U);
		return;
	}
	mirror_set(led, pattern, period);
	(void)send_ack(seq);
}

/**
 * @brief Process a SetLedAll request: validate, apply to both user LEDs, ACK.
 *
 * @param set Decoded SetLedAll table.
 * @param seq Sequence number of the request.
 */
static void handle_set_led_all(const hwlink_SetLedAll_table_t set, uint16_t seq)
{
	uint8_t pattern = (uint8_t)hwlink_SetLedAll_pattern(set);
	uint16_t period = hwlink_SetLedAll_period_ms(set);

	if (!pattern_valid(pattern) || !period_valid(period)) {
		(void)send_nak(seq, hwlink_Error_BAD_ARGUMENT,
			       "pattern or period", 0U);
		return;
	}
	if (led_post_set_all(pattern, period) != 0) {
		(void)send_nak(seq, hwlink_Error_BUSY, "LED queue full", 0U);
		return;
	}
	if (pattern == APP_PATTERN_CHASE) {
		mirror_set(APP_LED_ID_1, pattern, period);
	} else {
		led_mirror[0].pattern = pattern;
		led_mirror[0].period = period;
		led_mirror[1].pattern = pattern;
		led_mirror[1].period = period;
	}
	(void)send_ack(seq);
}

/**
 * @brief Verify, dispatch and respond to a decoded FlatBuffer message.
 *
 * Handles the DEMO request set (Hello, Ping, GetStatus, SetLed, SetLedAll,
 * ClearFault, SetBaud, EnterBootloader, Reset); unsupported bodies get an
 * UNSUPPORTED Nak.
 *
 * @param seq     Sequence number of the request.
 * @param payload Deframed payload bytes.
 * @param len     Payload length in bytes.
 */
static void handle_payload(uint16_t seq, const uint8_t *payload, size_t len)
{
	if (hwlink_Message_verify_as_root(payload, len) != flatcc_verify_ok) {
		(void)send_nak(seq, hwlink_Error_MALFORMED, "flatbuffer verifier",
			       0U);
		return;
	}

	hwlink_Message_table_t message = hwlink_Message_as_root(payload);
	hwlink_Body_union_t body = hwlink_Message_body_union(message);

	switch (body.type) {
	case hwlink_Body_Hello:
		link_open = true;
		(void)send_hello_reply(seq);
		break;

	case hwlink_Body_Ping:
		(void)send_pong(seq);
		break;

	case hwlink_Body_GetStatus:
		if (!send_status(seq)) {
			(void)send_nak(seq, hwlink_Error_BUSY,
				       "status build failed", 0U);
		}
		break;

	case hwlink_Body_SetLed:
		handle_set_led((hwlink_SetLed_table_t)body.value, seq);
		break;

	case hwlink_Body_SetLedAll:
		handle_set_led_all((hwlink_SetLedAll_table_t)body.value, seq);
		break;

	case hwlink_Body_ClearFault:
		app_fault_clear();
		(void)led_post_clear_error();
		(void)send_ack(seq);
		break;

	case hwlink_Body_SetBaud: {
		uint32_t rate =
			hwlink_SetBaud_rate((hwlink_SetBaud_table_t)body.value);

		if (rate != APP_UART_DEFAULT_BAUD &&
		    rate != APP_UART_FAST_BAUD) {
			(void)send_nak(seq, hwlink_Error_BAD_ARGUMENT,
				       "baud rate", 0U);
			break;
		}
		if (!send_ack(seq)) {
			break;
		}
		reset_parser();
		if (hwlink_uart_set_baud(rate)) {
			baud_revert_pending = rate != APP_UART_DEFAULT_BAUD;
			baud_last_valid = k_uptime_get_32();
		} else {
			baud_revert_pending = false;
		}
		break;
	}

	case hwlink_Body_EnterBootloader:
		if (send_ack(seq)) {
			app_handover_request_bootloader();
			sys_reboot(SYS_REBOOT_COLD);
		}
		break;

	case hwlink_Body_Reset:
		if (send_ack(seq)) {
			sys_reboot(SYS_REBOOT_COLD);
		}
		break;

	case hwlink_Body_HelloReply:
	case hwlink_Body_Pong:
	case hwlink_Body_Ack:
	case hwlink_Body_Nak:
	case hwlink_Body_Log:
	case hwlink_Body_Progress:
	case hwlink_Body_GetBootInfo:
	case hwlink_Body_BootInfo:
	case hwlink_Body_UpdateBegin:
	case hwlink_Body_UpdateData:
	case hwlink_Body_UpdateEnd:
	case hwlink_Body_UpdateResult:
	case hwlink_Body_Activate:
	case hwlink_Body_UpdateAbort:
	case hwlink_Body_Status:
	case hwlink_Body_ButtonEvent:
	default:
		(void)send_nak(seq, hwlink_Error_UNSUPPORTED,
			       "message not supported", 0U);
		break;
	}
}

/**
 * @brief Deframe one raw frame, drop bad CRCs, and dispatch the payload.
 *
 * Repeated requests (same sequence) re-send the previous response without
 * re-executing. Response/event frames are ignored.
 *
 * @param raw     Raw frame bytes.
 * @param raw_len Frame length in bytes.
 */
static void handle_raw(const uint8_t *raw, size_t raw_len)
{
	size_t payload_len = 0U;
	uint8_t flags = 0U;
	uint16_t seq = 0U;

	if (!hwlink_decode_raw(raw, raw_len, rx_payload, sizeof(rx_payload),
			       &payload_len, &flags, &seq)) {
		return;   /* bad CRC is dropped silently */
	}

	baud_last_valid = k_uptime_get_32();
	if ((flags & (HWLINK_FLAG_RESPONSE | HWLINK_FLAG_EVENT)) != 0U) {
		return;
	}

	if (last_request_valid && seq == last_request_seq &&
	    last_response_len > 0U) {
		size_t encoded_len = 0U;

		if (hwlink_cobs_encode(last_response, last_response_len, tx_frame,
				       sizeof(tx_frame), &encoded_len)) {
			(void)hwlink_uart_send(tx_frame, (uint16_t)encoded_len,
					       COMMS_TX_TIMEOUT_MS);
		}
		return;
	}

	last_request_valid = seq != 0U;
	last_request_seq = seq;
	last_response_len = 0U;

	handle_payload(seq, rx_payload, payload_len);
}

/**
 * @brief Feed one received byte into the COBS frame parser.
 *
 * A zero byte terminates the COBS body and triggers deframing; an
 * oversized body is flagged and dropped until the next delimiter.
 *
 * @param byte Received byte.
 */
static void feed_byte(uint8_t byte)
{
	if (byte == 0U) {
		if (!rx_enc_overflow && rx_enc_len > 0U) {
			size_t raw_len = 0U;

			if (hwlink_cobs_decode(rx_enc, rx_enc_len, rx_raw,
					       sizeof(rx_raw), &raw_len) == 0) {
				handle_raw(rx_raw, raw_len);
			}
		}
		reset_parser();
	} else if (rx_enc_len < sizeof(rx_enc)) {
		rx_enc[rx_enc_len++] = byte;
	} else {
		rx_enc_overflow = true;
	}
}

/**
 * @brief Drain up to 32 x 64-byte chunks from the UART RX ring.
 */
static void poll_receive(void)
{
	uint8_t chunk[64];

	for (uint32_t batch = 0U; batch < 32U; batch++) {
		uint32_t got = hwlink_uart_read(chunk, sizeof(chunk));

		if (got == 0U) {
			break;
		}
		for (uint32_t i = 0U; i < got; i++) {
			feed_byte(chunk[i]);
		}
	}
}

/**
 * @brief Handle a button event: update the LED mirror and, if the link is
 * open, send a ButtonEvent message.
 *
 * @param event Button event with press class, pattern and period.
 */
static void handle_button_event(const comms_event_t *event)
{
	if (event->press == 2U) {
		led_mirror[0].pattern = APP_PATTERN_HEARTBEAT;
		led_mirror[0].period = APP_LED_DEFAULT_PERIOD;
		led_mirror[1].pattern = APP_PATTERN_OFF;
		led_mirror[1].period = APP_LED_DEFAULT_PERIOD;
	} else if (event->pattern < APP_PATTERN_COUNT &&
		   event->period >= APP_LED_MIN_PERIOD &&
		   event->period <= APP_LED_MAX_PERIOD) {
		led_mirror[0].pattern = event->pattern;
		led_mirror[0].period = event->period;
		led_mirror[1].pattern = event->pattern;
		led_mirror[1].period = event->period;
	}
	if (link_open) {
		(void)send_button_event(event->press, event->pattern,
					event->period);
	}
}

/**
 * @brief Main comms loop.
 *
 * Processes RX and button work items from the message queue, drains the
 * UART ring, reverts a non-default baud rate after idle, and check-ins
 * with the health monitor periodically.
 *
 * @param p1 Unused.
 * @param p2 Unused.
 * @param p3 Unused.
 */
static void comms_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_thread_name_set(k_current_get(), "Comms");

	if (flatcc_builder_init(&builder) != 0) {
		k_panic();
	}

	comms_event_t event;
	uint32_t last_checkin = k_uptime_get_32();

	for (;;) {
		if (k_msgq_get(&comms_msgq, &event, K_MSEC(COMMS_LOOP_MS)) == 0) {
			do {
				if (event.type == COMMS_EVT_RX) {
					poll_receive();
				} else if (event.type == COMMS_EVT_BUTTON) {
					handle_button_event(&event);
				}
			} while (k_msgq_get(&comms_msgq, &event,
					    K_NO_WAIT) == 0);
		}

		poll_receive();

		uint32_t now = k_uptime_get_32();

		if (baud_revert_pending &&
		    (now - baud_last_valid) >= COMMS_BAUD_REVERT_MS) {
			reset_parser();
			(void)hwlink_uart_set_baud(APP_UART_DEFAULT_BAUD);
			baud_revert_pending = false;
		}

		if ((now - last_checkin) >= COMMS_CHECKIN_MS) {
			health_checkin(HEALTH_TASK_COMMS);
			last_checkin = now;
		}
	}
}

/* Comms thread: parse, verify, execute and reply over UART. */
K_THREAD_DEFINE(zdemo_comms_thread, ZDEMO_COMMS_STACK, comms_thread, NULL,
		NULL, NULL, ZDEMO_PRIO_COMMS, 0, 0);

/**
 * @brief Post an RX-available event to the comms thread (ISR-safe).
 */
void comms_post_rx_from_isr(void)
{
	comms_event_t event = { .type = COMMS_EVT_RX };

	(void)k_msgq_put(&comms_msgq, &event, K_NO_WAIT);
}

bool comms_rx_running(void)
{
	return hwlink_uart_rx_running();
}

void comms_post_button_event(uint8_t press, uint8_t pattern, uint16_t period)
{
	comms_event_t event = {
		.type = COMMS_EVT_BUTTON,
		.press = press,
		.pattern = pattern,
		.period = period
	};

	(void)k_msgq_put(&comms_msgq, &event, K_NO_WAIT);
}
