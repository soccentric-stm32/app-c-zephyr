/*
 * iwdg.c - watchdog service on the STM32 IWDG
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-004, ZBT-002, ZFN-004 (HLT-003)
 *
 * BOOT starts the IWDG with a 4 s timeout before hand-over.  The first feed
 * happens in Zephyr's PRE_KERNEL_1 initialisation, before drivers and the
 * scheduler; main feeds again before long-running initialisation.  The Zephyr
 * watchdog API is used for all later feeds.
 */

#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "system/iwdg.h"

/* IWDG timeout used by this firmware (BOOT sets the same 4 s window). */
#define APP_WDT_TIMEOUT_MS 4000U

/** Watchdog device from the devicetree (alias watchdog0). */
static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
/** true once app_wdt_init() has installed the timeout. */
static bool wdt_installed;

/**
 * @brief First IWDG service, run in PRE_KERNEL_1 before any driver starts.
 *
 * BOOT starts the IWDG with a 4 s timeout; this kick keeps it alive while
 * Zephyr initialises, before the scheduler or the health thread can run.
 *
 * @return 0 always.
 */
static int app_wdt_early_feed(void)
{
	IWDG->KR = 0x0000AAAAUL;
	return 0;
}

/* Keep the IWDG alive before drivers and the scheduler are available. */
SYS_INIT(app_wdt_early_feed, PRE_KERNEL_1, 0);

/**
 * @brief Feed the watchdog through the Zephyr driver.
 *
 * A safe no-op if the driver is not yet ready; called from task context
 * at a bounded rate by the health thread.
 */
void app_wdt_feed(void)
{
	if (device_is_ready(wdt)) {
		(void)wdt_feed(wdt, 0);
	}
}

/**
 * @brief Install (once) and start the IWDG timeout at 4 s; then feed it.
 *
 * The IWDG cannot be stopped once BOOT has started it; installing a new
 * timeout re-writes PR/RLR via the key access sequence, which the F7
 * hardware supports.
 *
 * @return true on success.
 */
bool app_wdt_init(void)
{
	if (!device_is_ready(wdt)) {
		return false;
	}

	/* The IWDG cannot be stopped once BOOT has started it.  Installing a
	 * timeout while it is running re-writes PR/RLR through the key access
	 * sequence, which is supported by the hardware.
	 */
	if (!wdt_installed) {
		struct wdt_timeout_cfg cfg = {
			.window = {
				.min = 0U,
				.max = APP_WDT_TIMEOUT_MS,
			},
			.callback = NULL,
			.flags = WDT_FLAG_RESET_SOC,
		};

		if (wdt_install_timeout(wdt, &cfg) < 0) {
			return false;
		}
		wdt_installed = true;
	}

	if (wdt_setup(wdt, 0U) < 0) {
		return false;
	}

	app_wdt_feed();
	return true;
}
