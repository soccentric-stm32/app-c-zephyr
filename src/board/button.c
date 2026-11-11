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

#include "app/zdemo.h"
#include "app/task_report.h"
#include "app/zdemo_threads.h"
#include "board/button.h"
#include "board/led.h"
#include "system/health.h"

LOG_MODULE_DECLARE(app);

/* Debounce window a raw level must hold before it counts as a change. */
#define BUTTON_DEBOUNCE_MS  30U
/* Presses shorter than this (and released) are reported as short presses. */
#define BUTTON_SHORT_MAX_MS 1000U
/* Presses held at least this long are reported as long presses. */
#define BUTTON_LONG_MS      2000U
/* Interval between button-thread check-ins to the health monitor. */
#define BUTTON_CHECKIN_MS   500U
/* Maximum time the thread sleeps on the wake semaphore. */
#define BUTTON_POLL_MS      10U

/** Device-tree descriptor of the user button (alias sw0). */
static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
/** GPIO callback context for the button interrupt. */
static struct gpio_callback button_cb;
/** Semaphore signalled by the button ISR to wake the thread. */
static K_SEM_DEFINE(button_sem, 0, 1);

/**
 * @brief Read the raw, un-debounced level of the button pin.
 *
 * @return true if the pin reads high (button physically pressed),
 *         false if low.
 */
static bool button_raw(void)
{
	return gpio_pin_get_raw(button.port, button.pin) > 0;
}

/**
 * @brief GPIO interrupt handler for the button.
 *
 * Only wakes the button thread; all debounce and press classification is
 * done in thread context.
 */
static void button_isr(const struct device *port, struct gpio_callback *cb,
		       gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_sem_give(&button_sem);
}

/**
 * @brief Button debounce and short/long press classification thread.
 *
 * Wakes on the button ISR semaphore (with a polling fallback), debounces
 * the raw pin level, and posts a message to the LED thread on release:
 * - short press: release after less than 1 s
 * - long press:  held for at least 2 s (posted once, while held)
 *
 * Also check-ins with the health monitor periodically.
 */
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
	struct task_report report = {0};

	for (;;) {
		TASK_REPORT(report, 2, "Button",
			    "debounces B1, short press cycles the LED pattern, long press resets it");
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

/* Button thread: debounce + short/long press detection. */
K_THREAD_DEFINE(zdemo_button_thread, ZDEMO_BUTTON_STACK, button_thread, NULL,
		NULL, NULL, ZDEMO_PRIO_BUTTON, 0, 0);
