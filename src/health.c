/*
 * health.c - task supervision, health check, watchdog and CONFIRMED record
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-004, ZFN-004 (HLT-001..HLT-005), ZGEN-004
 *
 * Health is the only thread that feeds the watchdog.  Every other thread
 * posts its bit in a k_event at least once per second.  If any bit is missing
 * for 2 s the watchdog is no longer fed and BOOT counts a failed trial.
 */

#include <zephyr/kernel.h>

#include "app_fault.h"
#include "comms.h"
#include "flash_confirm.h"
#include "handover.h"
#include "health.h"
#include "iwdg.h"
#include "led.h"
#include "zdemo.h"
#include "zdemo_threads.h"

#define HEALTH_LOOP_MS            20U
#define HEALTH_CHECKIN_TIMEOUT_MS 2000U
#define HEALTH_CHECK_AT_MS        5000U
#define HEALTH_STOP_KICK_MS       10000U
#define HEALTH_REFRESH_MS         100U
#define HEALTH_MIN_CHECKINS       5U

#define HEALTH_EVT_LED    BIT(HEALTH_TASK_LED)
#define HEALTH_EVT_BUTTON BIT(HEALTH_TASK_BUTTON)
#define HEALTH_EVT_COMMS  BIT(HEALTH_TASK_COMMS)
#define HEALTH_EVT_MASK   (HEALTH_EVT_LED | HEALTH_EVT_BUTTON | HEALTH_EVT_COMMS)

K_EVENT_DEFINE(health_events);

static uint32_t checkin_count[HEALTH_TASK_COUNT + 1U];
static uint32_t last_seen[HEALTH_TASK_COUNT + 1U];
static uint32_t start_time;
static uint32_t last_kick;
static volatile uint8_t health_state = APP_HEALTH_NOT_REQUIRED;
static bool check_done;
static bool supervision_lost;

void health_checkin(health_task_id_t task)
{
	if ((uint32_t)task < HEALTH_TASK_COUNT) {
		k_event_post(&health_events, BIT(task));
	}
}

uint8_t health_state_get(void)
{
	return health_state;
}

bool health_is_failed(void)
{
	return health_state == APP_HEALTH_FAILED;
}

static void checkin_account(uint32_t bits, uint32_t now)
{
	for (uint32_t i = 0U; i < HEALTH_TASK_COUNT; i++) {
		if ((bits & BIT(i)) != 0U) {
			checkin_count[i]++;
			last_seen[i] = now;
		}
	}

	/* Health itself is alive whenever this function runs. */
	checkin_count[HEALTH_TASK_COUNT]++;
	last_seen[HEALTH_TASK_COUNT] = now;
}

static void supervise(uint32_t now)
{
	if (supervision_lost) {
		return;
	}
	for (uint32_t i = 0U; i < (HEALTH_TASK_COUNT + 1U); i++) {
		if ((now - last_seen[i]) > HEALTH_CHECKIN_TIMEOUT_MS) {
			supervision_lost = true;
			return;
		}
	}
}

static bool health_conditions_met(void)
{
#ifdef CONFIG_ZDEMO_FAIL_HEALTH
	return false;
#else
	for (uint32_t i = 0U; i < (HEALTH_TASK_COUNT + 1U); i++) {
		if (checkin_count[i] < HEALTH_MIN_CHECKINS) {
			return false;
		}
	}
	if (app_fault_is_current_boot()) {
		return false;
	}
	/* A fault in the previous trial boot is a failed trial; an older
	 * fault from before the update is only reported in Status. */
	if (g_handover.trial_count > 1U && app_fault_is_previous_boot()) {
		return false;
	}

	/* No heap: there is nothing to monitor here (see main.c). */
	return comms_rx_running();
#endif
}

static void health_check_fail(void)
{
	check_done = true;
	health_state = APP_HEALTH_FAILED;
	(void)led_post_health_failed();
}

static void health_check_pass(void)
{
	check_done = true;
	health_state = APP_HEALTH_PASSED;
}

static void health_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_thread_name_set(k_current_get(), "Health");

	uint32_t now = k_uptime_get_32();

	start_time = now;
	last_kick = now;
	for (uint32_t i = 0U; i < (HEALTH_TASK_COUNT + 1U); i++) {
		last_seen[i] = now;
		checkin_count[i] = 0U;
	}

	bool pending = g_handover.present && g_handover.trial == 2U;
	health_state = pending ? APP_HEALTH_RUNNING : APP_HEALTH_NOT_REQUIRED;

	for (;;) {
		uint32_t bits = k_event_wait(&health_events, HEALTH_EVT_MASK,
					     true, K_MSEC(HEALTH_LOOP_MS));

		now = k_uptime_get_32();
		checkin_account(bits, now);
		supervise(now);

		if (pending && !check_done &&
		    (now - start_time) >= HEALTH_CHECK_AT_MS) {
			if (health_conditions_met()) {
				if (app_flash_confirm_running_slot()) {
					health_check_pass();
				} else {
					health_check_fail();
				}
			} else if ((now - start_time) >= HEALTH_STOP_KICK_MS) {
				health_check_fail();
			}
		}

		bool kick = !supervision_lost;

		if (health_state == APP_HEALTH_FAILED &&
		    (now - start_time) >= HEALTH_STOP_KICK_MS) {
			kick = false;
		}

		if (kick && (now - last_kick) >= HEALTH_REFRESH_MS) {
			app_wdt_feed();
			last_kick = now;
		}
	}
}

K_THREAD_DEFINE(zdemo_health_thread, ZDEMO_HEALTH_STACK, health_thread, NULL,
		NULL, NULL, ZDEMO_PRIO_HEALTH, 0, 0);
