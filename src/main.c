/*
 * main.c - Zephyr demo application entry point
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (Zephyr board nucleo_f767zi), started by BOOT
 * SRS:     SRS-ZEPHYR-F767: ZBT-001..005, ZGEN-003..005, ZRT-001..006,
 *          ZFN-001..005, ZSAFE-001..004
 *
 * The four application threads (Health, Comms, Button, LED) are statically
 * defined.  main() only performs the reset-safe early start-up sequence and
 * then returns; Zephyr keeps running the application threads.
 */

#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

#include "app_fault.h"
#include "handover.h"
#include "health.h"
#include "iwdg.h"
#include "led.h"
#include "reset_cause.h"
#include "uart_hwlink.h"
#include "zdemo.h"

BUILD_ASSERT(CONFIG_FLASH_LOAD_OFFSET == 0xC000,
	     "ZBT-001: the application must be linked at 0x0800C000");
BUILD_ASSERT(CONFIG_FLASH_LOAD_SIZE == 0xF4000,
	     "ZBT-001: the application slot is 976 KB");

/* Runtime heap used for the Status statistics.  The application itself
 * never allocates; flatcc uses its own fixed arena. */
K_HEAP_DEFINE(zdemo_heap, 6144);

void app_heap_stats(uint32_t *free_bytes, uint32_t *min_free_bytes)
{
	struct sys_memory_stats stats = { 0 };

	if (sys_heap_runtime_stats_get(&zdemo_heap.heap, &stats) != 0) {
		*free_bytes = 0U;
		*min_free_bytes = 0U;
		return;
	}

	*free_bytes = (uint32_t)stats.free_bytes;
	*min_free_bytes = (uint32_t)(stats.free_bytes +
				     stats.allocated_bytes -
				     stats.max_allocated_bytes);
}

int main(void)
{
	/* The fault record is in no-init RAM; bump its boot id first. */
	app_fault_boot_start();

	/* ZBT-002/APP-004: feed the watchdog BOOT started as early as
	 * possible, before any long initialisation. */
	app_wdt_feed();

	/* ZGEN-005: keep the reset cause for Status, then clear it. */
	app_reset_cause_capture();

	/* ZBT-004: decode the hand-over record before Health starts. */
	app_handover_read();

	/* ZBT-001/ZBT-002: the vector table must be the image's own table. */
	if (SCB->VTOR != (0x08000000UL + CONFIG_FLASH_LOAD_OFFSET)) {
		k_panic();
	}

	(void)app_wdt_init();

	/* LED-005: latch LD3 for the last fault or watchdog reset. */
	if (app_fault_is_valid()) {
		(void)led_post_error(APP_ERROR_FAULT);
	}
	if (app_reset_was_error()) {
		(void)led_post_error(APP_ERROR_WATCHDOG);
	}

	/* COM-001/002: USART3 must be running before the health check. */
	if (!hwlink_uart_init()) {
		k_panic();
	}

	return 0;
}
