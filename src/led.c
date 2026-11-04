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

#define LED_QUEUE_LENGTH 8U
#define LED_CHECKIN_MS   500U

#define LED_ERROR_FAST_PERIOD 100U
#define NO_TRANSITION         0xFFFFFFFFUL

typedef enum {
	LED_MSG_SET = 0,
	LED_MSG_SET_ALL,
	LED_MSG_BUTTON,
	LED_MSG_CLEAR_ERROR,
	LED_MSG_HEALTH_FAILED,
	LED_MSG_ERROR
} led_msg_type_t;

typedef struct {
	uint8_t type;
	uint8_t led;
	uint8_t pattern;
	uint8_t press;
	uint16_t period;
	uint32_t flags;
	bool force;
} led_msg_t;

typedef struct {
	uint8_t pattern;
	uint16_t period;
	uint32_t start;
} led_cfg_t;

K_MSGQ_DEFINE(led_msgq, sizeof(led_msg_t), LED_QUEUE_LENGTH, 4);

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static uint32_t next_pattern(uint32_t current)
{
	return (current + 1U) % APP_PATTERN_COUNT;
}

static void led_write(const struct gpio_dt_spec *led, bool on)
{
	if (gpio_is_ready_dt(led)) {
		(void)gpio_pin_set_dt(led, on ? 1 : 0);
	}
}

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

K_THREAD_DEFINE(zdemo_led_thread, ZDEMO_LED_STACK, led_thread, NULL, NULL,
		NULL, ZDEMO_PRIO_LED, 0, 0);

static int led_post(const led_msg_t *msg)
{
	return k_msgq_put(&led_msgq, msg, K_NO_WAIT);
}

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

int led_post_set_all(uint8_t pattern, uint16_t period)
{
	led_msg_t msg = {
		.type = LED_MSG_SET_ALL,
		.pattern = pattern,
		.period = period
	};

	return led_post(&msg);
}

int led_post_button(uint8_t press)
{
	led_msg_t msg = {
		.type = LED_MSG_BUTTON,
		.press = press
	};

	return led_post(&msg);
}

int led_post_clear_error(void)
{
	led_msg_t msg = { .type = LED_MSG_CLEAR_ERROR };

	return led_post(&msg);
}

int led_post_health_failed(void)
{
	led_msg_t msg = { .type = LED_MSG_HEALTH_FAILED };

	return led_post(&msg);
}

int led_post_error(uint32_t flags)
{
	led_msg_t msg = {
		.type = LED_MSG_ERROR,
		.flags = flags
	};

	return led_post(&msg);
}
