/*
 * button.c - user button B1 (PC13), GPIO interrupt + 30 ms debounce
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZFN-002 (BTN-001..BTN-004)
 *
 * B1 is physically high when pressed.  The GPIO interrupt only wakes the
 * thread; debounce and short/long classification use absolute uptime.  The
 * LED thread owns the pattern state, so this thread posts a button message.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "button.h"
#include "health.h"
#include "led.h"
#include "zdemo.h"
#include "zdemo_threads.h"

#define BUTTON_DEBOUNCE_MS  30U
#define BUTTON_SHORT_MAX_MS 1000U
#define BUTTON_LONG_MS      2000U
#define BUTTON_CHECKIN_MS   500U
#define BUTTON_POLL_MS      10U

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;
static K_SEM_DEFINE(button_sem, 0, 1);

static bool button_raw(void)
{
	return gpio_pin_get_raw(button.port, button.pin) > 0;
}

static void button_isr(const struct device *port, struct gpio_callback *cb,
		       gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&button_sem);
}

static void button_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_thread_name_set(k_current_get(), "Button");

	if (!gpio_is_ready_dt(&button) ||
	    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_DOWN) != 0) {
		k_panic();
	}

	gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
	if (gpio_add_callback_dt(&button, &button_cb) != 0 ||
	    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH) != 0) {
		k_panic();
	}

	uint32_t now = k_uptime_get_32();
	bool stable = button_raw();
	bool candidate = stable;
	uint32_t candidate_since = now;
	uint32_t press_start = now;
	bool pressed = stable;
	bool long_sent = false;
	uint32_t last_checkin = now;

	for (;;) {
		(void)k_sem_take(&button_sem, K_MSEC(BUTTON_POLL_MS));
		now = k_uptime_get_32();
		bool raw = button_raw();

		if (raw != candidate) {
			candidate = raw;
			candidate_since = now;
		} else if (candidate != stable &&
			   (now - candidate_since) >= BUTTON_DEBOUNCE_MS) {
			stable = candidate;
			if (stable) {
				pressed = true;
				long_sent = false;
				press_start = now;
			} else {
				uint32_t duration = now - press_start;

				if (pressed && !long_sent) {
					if (duration < BUTTON_SHORT_MAX_MS) {
						(void)led_post_button(1U);
					} else if (duration >= BUTTON_LONG_MS) {
						(void)led_post_button(2U);
					}
				}
				pressed = false;
			}
		}

		if (pressed && !long_sent &&
		    (now - press_start) >= BUTTON_LONG_MS) {
			long_sent = true;
			(void)led_post_button(2U);
		}

		if ((now - last_checkin) >= BUTTON_CHECKIN_MS) {
			health_checkin(HEALTH_TASK_BUTTON);
			last_checkin = now;
		}
	}
}

K_THREAD_DEFINE(zdemo_button_thread, ZDEMO_BUTTON_STACK, button_thread, NULL,
		NULL, NULL, ZDEMO_PRIO_BUTTON, 0, 0);
