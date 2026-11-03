/*
 * zephyr-app - Zephyr demo application entry point
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (Zephyr board nucleo_f767zi), started by BOOT
 * SRS:     ZBT-001, ZGEN-003, ZRT-003, ZFN-001 (LED-002, LED-006)
 *
 * Implemented so far: the image runs from the bootloader slot, the clock
 * tree and caches come from the board devicetree and Kconfig, and the main
 * thread runs the HEARTBEAT pattern on LD1. Still to come, section by
 * section: health (watchdog + CONFIRMED), comms (USART3 + HWLINK), button,
 * the hand-over record and the image file.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* LD1 green (led0 alias of the board devicetree). */
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* HEARTBEAT ([DEMO] Appendix A): two 50 ms flashes 200 ms apart in a 1 s period. */
#define HEARTBEAT_PERIOD_MS 1000
#define HEARTBEAT_FLASH_MS  50
#define HEARTBEAT_GAP_MS    150

int main(void)
{
	if (!gpio_is_ready_dt(&led1) ||
	    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE) != 0) {
		k_panic();
	}

	int64_t next = k_uptime_get();

	while (1) {
		gpio_pin_set_dt(&led1, 1);
		k_msleep(HEARTBEAT_FLASH_MS);
		gpio_pin_set_dt(&led1, 0);
		k_msleep(HEARTBEAT_GAP_MS);
		gpio_pin_set_dt(&led1, 1);
		k_msleep(HEARTBEAT_FLASH_MS);
		gpio_pin_set_dt(&led1, 0);

		/* LED-006: the period is anchored to absolute time, not the delays. */
		next += HEARTBEAT_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
	}

	return 0;
}
