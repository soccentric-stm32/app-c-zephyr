/*
 * fatal.c - fatal error handler (ZGEN-004)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (Zephyr board nucleo_f767zi)
 * SRS:     ZGEN-004 - LD3 on for about 2 s, then reset
 *
 * Saving the reason to a no-init area for the Status message comes with
 * the comms thread.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>

/* LD3 red (led2 alias), reserved for errors as in the C demo (LED-005). */
static const struct gpio_dt_spec led3 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(esf);

	if (gpio_is_ready_dt(&led3)) {
		(void)gpio_pin_configure_dt(&led3, GPIO_OUTPUT_ACTIVE);
	}
	k_busy_wait(2 * USEC_PER_SEC);
	sys_reboot(SYS_REBOOT_COLD);
}
