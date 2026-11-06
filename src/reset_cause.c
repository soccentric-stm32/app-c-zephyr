/*
 * reset_cause.c - reset cause captured through the hwinfo API
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZGEN-005 (DGEN-005), ZFN-001 (LED-005)
 */

#include <zephyr/drivers/hwinfo.h>

#include "reset_cause.h"

/**
 * @brief Raw reset-cause flags captured via the hwinfo API (RESET_* bits).
 */
uint32_t app_reset_cause_flags;

/**
 * @brief Human-readable text for the most recent captured reset cause.
 */
const char *app_reset_cause_text = "unknown";

/**
 * @brief Capture the reset cause, set the text, and clear the sticky flags.
 *
 * Reads the reset-cause bits via the Zephyr hwinfo API, maps them to a
 * short string (watchdog, software, reset pin, brown-out, power-on,
 * low power, CPU lock-up, unknown), and clears the sticky bits.
 *
 * Call early at boot before any other reset-cause consumer clears them.
 */
void app_reset_cause_capture(void)
{
	uint32_t flags = 0U;

	if (hwinfo_get_reset_cause(&flags) != 0) {
		flags = 0U;
	}
	app_reset_cause_flags = flags;

	if ((flags & RESET_WATCHDOG) != 0U) {
		app_reset_cause_text = "watchdog";
	} else if ((flags & RESET_SOFTWARE) != 0U) {
		app_reset_cause_text = "software";
	} else if ((flags & RESET_PIN) != 0U) {
		app_reset_cause_text = "reset pin";
	} else if ((flags & RESET_BROWNOUT) != 0U) {
		app_reset_cause_text = "brown-out";
	} else if ((flags & RESET_POR) != 0U) {
		app_reset_cause_text = "power-on";
	} else if ((flags & RESET_LOW_POWER_WAKE) != 0U) {
		app_reset_cause_text = "low power";
	} else if ((flags & RESET_CPU_LOCKUP) != 0U) {
		app_reset_cause_text = "CPU lock-up";
	} else {
		app_reset_cause_text = "unknown";
	}

	(void)hwinfo_clear_reset_cause();
}

/**
 * @brief Test whether the last reset was caused by the watchdog.
 *
 * @return true if the captured reset cause is RESET_WATCHDOG.
 */
bool app_reset_was_error(void)
{
	return (app_reset_cause_flags & RESET_WATCHDOG) != 0U;
}
