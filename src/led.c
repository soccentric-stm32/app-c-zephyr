/*
 * led.c - LED patterns and LD3 error display
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZFN-001 (LED-001..LED-007), ZFN-002 (BTN-002/003)
 *
 * The LED thread owns the pattern state.  Other threads post messages to its
 * queue.  Pattern transitions are computed from absolute uptime, so jitter
 * is bounded by the scheduler and does not accumulate.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "comms.h"
#include "health.h"
#include "led.h"
#include "zdemo.h"
#include "zdemo_threads.h"

/* Maximum number of LED messages queued. */
#define LED_QUEUE_LENGTH 8U
/* LED thread check-in period to the health monitor. */
#define LED_CHECKIN_MS   500U

/* LD3 fast-blink period (ms) used for the health-failed error display. */
#define LED_ERROR_FAST_PERIOD 100U
/* Sentinel meaning "no further transition". */
#define NO_TRANSITION         0xFFFFFFFFUL

/**
 * @brief Message types sent to the LED thread via the LED message queue.
 */
typedef enum {
	LED_MSG_SET = 0,          /**< Set one LED's pattern and period. */
	LED_MSG_SET_ALL,          /**< Set both user LEDs together. */
	LED_MSG_BUTTON,           /**< Button press (1 = short, 2 = long). */
	LED_MSG_CLEAR_ERROR,      /**< Clear all error flags. */
	LED_MSG_HEALTH_FAILED,    /**< Raise the health-failed error flag. */
	LED_MSG_ERROR             /**< Raise arbitrary error flags. */
} led_msg_type_t;

/**
 * @brief Work item carried in the LED message queue.
 *
 * Field meanings depend on @ref led_msg_type_t; unused fields may contain
 * stale data.
 */
typedef struct {
	uint8_t type;
	uint8_t led;
	uint8_t pattern;
	uint8_t press;
	uint16_t period;
	uint32_t flags;
	bool force;
} led_msg_t;

/**
 * @brief Pattern state for one LED.
 *
 * Transitions are computed from the @ref start timestamp so scheduler
 * jitter does not accumulate.
 */
typedef struct {
	uint8_t pattern;
	uint16_t period;
	uint32_t start;
} led_cfg_t;

/** Message queue between producers and the LED thread. */
K_MSGQ_DEFINE(led_msgq, sizeof(led_msg_t), LED_QUEUE_LENGTH, 4);

/** LD1 (green) GPIO descriptor. */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
/** LD2 (blue) GPIO descriptor. */
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
/** LD3 (orange) GPIO descriptor. */
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/**
 * @brief Next pattern id, wrapping at APP_PATTERN_COUNT.
 *
 * @param current Current id.
 * @return Next id.
 */
static uint32_t next_pattern(uint32_t current)
{
	return (current + 1U) % APP_PATTERN_COUNT;
}

/**
 * @brief Drive one LED on or off (no-op if the driver is not ready).
 *
 * @param led Descriptor of the LED pin.
 * @param on  true to switch on.
 */
static void led_write(const struct gpio_dt_spec *led, bool on)
{
	if (gpio_is_ready_dt(led)) {
		(void)gpio_pin_set_dt(led, on ? 1 : 0);
	}
}

/**
 * @brief Expand a LED pattern into a sequence of on/off segments.
 *
 * Fills the dur[] and on[] arrays; durations sum to one full cycle.
 *
 * @param cfg         LED pattern config.
 * @param second_led  true for the second LED of a CHASE pair.
 * @param dur         Receives segment durations in ms.
 * @param on          Receives the level for each segment.
 * @return Number of segments filled.
 */
static uint32_t segments_for(const led_cfg_t *cfg, bool second_led,
			     uint16_t dur[20], bool on[20])
{
	uint32_t n = 0U;
	uint32_t period = cfg->period;

	switch (cfg->pattern) {
	case APP_PATTERN_OFF:
		break;

	case APP_PATTERN_ON:
		dur[n] = 0xFFFFU;
		on[n] = true;
		n++;
		break;

	case APP_PATTERN_BLINK:
		dur[n] = (uint16_t)(period / 2U);
		on[n] = true;
		n++;
		dur[n] = (uint16_t)(period - (period / 2U));
		on[n] = false;
		n++;
		break;

	case APP_PATTERN_FAST: {
		uint32_t fast = period / 5U;

		if (fast < 2U) {
			fast = 2U;
		}
		dur[n] = (uint16_t)(fast / 2U);
		on[n] = true;
		n++;
		dur[n] = (uint16_t)(fast - (fast / 2U));
		on[n] = false;
		n++;
		break;
	}

	case APP_PATTERN_HEARTBEAT: {
		uint32_t cycle = period < 250U ? 250U : period;

		dur[n] = 50U;
		on[n] = true;
		n++;
		dur[n] = 150U;
		on[n] = false;
		n++;
		dur[n] = 50U;
		on[n] = true;
		n++;
		if (cycle > 250U) {
			dur[n] = (uint16_t)(cycle - 250U);
			on[n] = false;
			n++;
		}
		break;
	}

	case APP_PATTERN_CHASE: {
		uint32_t half = period / 2U;

		if (second_led) {
			dur[n] = (uint16_t)half;
			on[n] = false;
			n++;
			dur[n] = (uint16_t)(period - half);
			on[n] = true;
		} else {
			dur[n] = (uint16_t)half;
			on[n] = true;
			n++;
			dur[n] = (uint16_t)(period - half);
			on[n] = false;
		}
		n++;
		break;
	}

	case APP_PATTERN_MORSE_SOS:
	default:
		/* ... --- ... with a 100 ms unit. */
		dur[n] = 100U; on[n] = true;  n++;
		dur[n] = 100U; on[n] = false; n++;
		dur[n] = 100U; on[n] = true;  n++;
		dur[n] = 100U; on[n] = false; n++;
		dur[n] = 100U; on[n] = true;  n++;
		dur[n] = 300U; on[n] = false; n++;
		dur[n] = 300U; on[n] = true;  n++;
		dur[n] = 100U; on[n] = false; n++;
		dur[n] = 300U; on[n] = true;  n++;
		dur[n] = 100U; on[n] = false; n++;
		dur[n] = 300U; on[n] = true;  n++;
		dur[n] = 300U; on[n] = false; n++;
		dur[n] = 100U; on[n] = true;  n++;
		dur[n] = 100U; on[n] = false; n++;
		dur[n] = 100U; on[n] = true;  n++;
		dur[n] = 100U; on[n] = false; n++;
		dur[n] = 100U; on[n] = true;  n++;
		dur[n] = 700U; on[n] = false; n++;
		break;
	}

	return n;
}

/**
 * @brief Compute the current level and next transition time for a pattern.
 *
 * Position within the cycle is derived from absolute uptime, so jitter is
 * bounded and never accumulates.
 *
 * @param cfg        LED pattern config.
 * @param second_led true for the second LED of a CHASE pair.
 * @param now        Current uptime in ms.
 * @param level      Receives the desired output level.
 * @param next_ms    Receives the time (ms) until the next level change.
 */
static void evaluate(const led_cfg_t *cfg, bool second_led, uint32_t now,
		     bool *level, uint32_t *next_ms)
{
	uint16_t dur[20];
	bool on[20];

	if (cfg->pattern == APP_PATTERN_OFF) {
		*level = false;
		*next_ms = NO_TRANSITION;
		return;
	}
	if (cfg->pattern == APP_PATTERN_ON) {
		*level = true;
		*next_ms = NO_TRANSITION;
		return;
	}

	uint32_t count = segments_for(cfg, second_led, dur, on);
	uint32_t cycle = 0U;

	for (uint32_t i = 0U; i < count; i++) {
		cycle += dur[i];
	}
	if (cycle == 0U) {
		*level = false;
		*next_ms = NO_TRANSITION;
		return;
	}

	uint32_t pos = (now - cfg->start) % cycle;
	uint32_t acc = 0U;

	for (uint32_t i = 0U; i < count; i++) {
		if (dur[i] == 0U) {
			continue;
		}
		if (pos < (acc + dur[i])) {
			*level = on[i];
			*next_ms = (acc + dur[i]) - pos;
			return;
		}
		acc += dur[i];
	}

	*level = false;
	*next_ms = cycle - pos;
}

/**
 * @brief Apply a Set/SetAll command to the pattern state.
 *
 * CHASE is a pair spanning LED1 and LED2; setting one of them to a
 * different pattern turns the other off. LD3 is handled independently.
 *
 * @param cfg      Pattern state array (three LEDs).
 * @param led      Target LED id.
 * @param pattern  New pattern id.
 * @param period   New period in ms.
 * @param now      Current uptime in ms (restarts the pattern phase).
 */
static void apply_set(led_cfg_t cfg[3], uint8_t led, uint8_t pattern,
		      uint16_t period, uint32_t now)
{
	if (led == APP_LED_ID_3) {
		cfg[2].pattern = pattern;
		cfg[2].period = period;
		cfg[2].start = now;
		return;
	}

	if (pattern == APP_PATTERN_CHASE) {
		cfg[0].pattern = APP_PATTERN_CHASE;
		cfg[0].period = period;
		cfg[0].start = now;
		cfg[1].pattern = APP_PATTERN_CHASE;
		cfg[1].period = period;
		cfg[1].start = now;
		return;
	}

	/* Leaving CHASE breaks the pair. */
	if (cfg[0].pattern == APP_PATTERN_CHASE ||
	    cfg[1].pattern == APP_PATTERN_CHASE) {
		if (led == APP_LED_ID_1) {
			cfg[1].pattern = APP_PATTERN_OFF;
			cfg[1].period = period;
			cfg[1].start = now;
		} else {
			cfg[0].pattern = APP_PATTERN_OFF;
			cfg[0].period = period;
			cfg[0].start = now;
		}
	}

	uint32_t index = (led == APP_LED_ID_2) ? 1U : 0U;

	cfg[index].pattern = pattern;
	cfg[index].period = period;
	cfg[index].start = now;
}

/**
 * @brief Apply one queued message to the pattern state and error flags.
 *
 * @param cfg         Pattern state array.
 * @param msg         The message to process.
 * @param now         Current uptime in ms.
 * @param error_flags Receives the updated LD3 error flags.
 */
static void handle_message(led_cfg_t cfg[3], const led_msg_t *msg,
			   uint32_t now, uint32_t *error_flags)
{
	switch (msg->type) {
	case LED_MSG_SET:
		if (msg->led == APP_LED_ID_3 && !msg->force) {
			return;
		}
		apply_set(cfg, msg->led, msg->pattern, msg->period, now);
		break;

	case LED_MSG_SET_ALL:
		apply_set(cfg, APP_LED_ID_1, msg->pattern, msg->period, now);
		cfg[1].pattern = msg->pattern;
		cfg[1].period = msg->period;
		cfg[1].start = now;
		break;

	case LED_MSG_BUTTON:
		if (msg->press == 2U) {   /* long: restore defaults */
			cfg[0].pattern = APP_PATTERN_HEARTBEAT;
			cfg[0].period = APP_LED_DEFAULT_PERIOD;
			cfg[0].start = now;
			cfg[1].pattern = APP_PATTERN_OFF;
			cfg[1].period = APP_LED_DEFAULT_PERIOD;
			cfg[1].start = now;
			comms_post_button_event(2U, APP_PATTERN_HEARTBEAT,
						APP_LED_DEFAULT_PERIOD);
		} else {
			uint16_t period = cfg[0].period;

			if (period < APP_LED_MIN_PERIOD ||
			    period > APP_LED_MAX_PERIOD) {
				period = APP_LED_DEFAULT_PERIOD;
			}
			uint8_t pattern = (uint8_t)next_pattern(cfg[0].pattern);

			apply_set(cfg, APP_LED_ID_1, pattern, period, now);
			cfg[1].pattern = pattern;
			cfg[1].period = period;
			cfg[1].start = now;
			comms_post_button_event(1U, pattern, period);
		}
		break;

	case LED_MSG_CLEAR_ERROR:
		*error_flags = 0U;
		break;

	case LED_MSG_HEALTH_FAILED:
		*error_flags |= APP_ERROR_HEALTH;
		break;

	case LED_MSG_ERROR:
		*error_flags |= msg->flags;
		break;

	default:
		break;
	}
}

/**
 * @brief Main LED loop: evaluate patterns, drive pins, process messages.
 *
 * Owns the pattern state and the LD3 error display (health-failed = fast
 * blink; fault/watchdog = solid). Sleeps until the next pattern transition
 * or the health check-in deadline, whichever is earlier.
 *
 * @param p1 Unused.
 * @param p2 Unused.
 * @param p3 Unused.
 */
static void led_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_thread_name_set(k_current_get(), "LED");

	if (!gpio_is_ready_dt(&led1) || !gpio_is_ready_dt(&led2) ||
	    !gpio_is_ready_dt(&led3) ||
	    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&led3, GPIO_OUTPUT_INACTIVE) != 0) {
		k_panic();
	}

	uint32_t now = k_uptime_get_32();
	led_cfg_t cfg[3] = {
		{ APP_PATTERN_HEARTBEAT, APP_LED_DEFAULT_PERIOD, now },
		{ APP_PATTERN_OFF,       APP_LED_DEFAULT_PERIOD, now },
		{ APP_PATTERN_OFF,       APP_LED_DEFAULT_PERIOD, now }
	};
	uint32_t error_flags = 0U;
	uint32_t last_checkin = now;

	for (;;) {
		now = k_uptime_get_32();
		bool led1_on = false;
		bool led2_on = false;
		bool led3_on = false;
		uint32_t next1 = NO_TRANSITION;
		uint32_t next2 = NO_TRANSITION;
		uint32_t next3 = NO_TRANSITION;

		evaluate(&cfg[0], false, now, &led1_on, &next1);
		evaluate(&cfg[1], true, now, &led2_on, &next2);

		if ((error_flags & APP_ERROR_HEALTH) != 0U) {
			uint32_t pos = now % LED_ERROR_FAST_PERIOD;

			led3_on = pos < (LED_ERROR_FAST_PERIOD / 2U);
			next3 = (led3_on ? (LED_ERROR_FAST_PERIOD / 2U)
					 : LED_ERROR_FAST_PERIOD) - pos;
		} else if ((error_flags &
			    (APP_ERROR_FAULT | APP_ERROR_WATCHDOG)) != 0U) {
			led3_on = true;
			next3 = NO_TRANSITION;
		} else {
			evaluate(&cfg[2], false, now, &led3_on, &next3);
		}

		led_write(&led1, led1_on);
		led_write(&led2, led2_on);
		led_write(&led3, led3_on);

		if ((now - last_checkin) >= LED_CHECKIN_MS) {
			health_checkin(HEALTH_TASK_LED);
			last_checkin = now;
		}

		uint32_t since = now - last_checkin;
		uint32_t wait = (since >= LED_CHECKIN_MS)
				? 1U : (LED_CHECKIN_MS - since);

		if (next1 < wait) {
			wait = next1;
		}
		if (next2 < wait) {
			wait = next2;
		}
		if (next3 < wait) {
			wait = next3;
		}
		if (wait == 0U) {
			wait = 1U;
		}

		led_msg_t msg;

		if (k_msgq_get(&led_msgq, &msg, K_MSEC(wait)) == 0) {
			now = k_uptime_get_32();
			handle_message(cfg, &msg, now, &error_flags);
			while (k_msgq_get(&led_msgq, &msg, K_NO_WAIT) == 0) {
				handle_message(cfg, &msg, now, &error_flags);
			}
		}
	}
}

/* LED pattern display thread. */
K_THREAD_DEFINE(zdemo_led_thread, ZDEMO_LED_STACK, led_thread, NULL, NULL,
		NULL, ZDEMO_PRIO_LED, 0, 0);

/**
 * @brief Queue one message to the LED thread (fails if the queue is full).
 *
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
static int led_post(const led_msg_t *msg)
{
	return k_msgq_put(&led_msgq, msg, K_NO_WAIT);
}

/**
 * @brief Ask the LED thread to set one LED's pattern and period.
 *
 * @param led     LED id (1..3).
 * @param pattern Pattern id.
 * @param period  Period in ms.
 * @param force   true to bypass the LD3 reservation guard.
 * @return 0 on success (queued), negative errno on failure.
 */
int led_post_set(uint8_t led, uint8_t pattern, uint16_t period, bool force)
{
	led_msg_t msg = {
		.type = LED_MSG_SET,
		.led = led,
		.pattern = pattern,
		.period = period,
		.force = force
	};

	return led_post(&msg);
}

/**
 * @brief Ask the LED thread to set both user LEDs to the same pattern.
 *
 * @param pattern Pattern id.
 * @param period  Period in ms.
 * @return 0 on success (queued), negative errno on failure.
 */
int led_post_set_all(uint8_t pattern, uint16_t period)
{
	led_msg_t msg = {
		.type = LED_MSG_SET_ALL,
		.pattern = pattern,
		.period = period
	};

	return led_post(&msg);
}

/**
 * @brief Report a button press to the LED thread for its effect.
 *
 * @param press 1 = short press, 2 = long press.
 * @return 0 on success (queued), negative errno on failure.
 */
int led_post_button(uint8_t press)
{
	led_msg_t msg = {
		.type = LED_MSG_BUTTON,
		.press = press
	};

	return led_post(&msg);
}

/**
 * @brief Ask the LED thread to clear all error flags (stop LD3).
 *
 * @return 0 on success (queued), negative errno on failure.
 */
int led_post_clear_error(void)
{
	led_msg_t msg = { .type = LED_MSG_CLEAR_ERROR };

	return led_post(&msg);
}

/**
 * @brief Raise the health-failed error flag (LD3 fast blink).
 *
 * @return 0 on success (queued), negative errno on failure.
 */
int led_post_health_failed(void)
{
	led_msg_t msg = { .type = LED_MSG_HEALTH_FAILED };

	return led_post(&msg);
}

/**
 * @brief Raise arbitrary LD3 error flags.
 *
 * @param flags APP_ERROR_* bit mask.
 * @return 0 on success (queued), negative errno on failure.
 */
int led_post_error(uint32_t flags)
{
	led_msg_t msg = {
		.type = LED_MSG_ERROR,
		.flags = flags
	};

	return led_post(&msg);
}
