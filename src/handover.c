/*
 * handover.c - BOOT hand-over record in RTC_BKP2R
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZBT-004, ZBT-005
 *
 * VBAT is tied to VDD on the NUCLEO-F767ZI, so the backup registers survive
 * a system reset (the BOOT hand-over) but not a power cycle.  The BOOT
 * contract is in boot_contract.h.
 */

#include <soc.h>
#include <zephyr/kernel.h>

#include "boot_contract.h"
#include "handover.h"

/** Boot hand-over state decoded from RTC_BKP2R (see handover.h). */
app_handover_t g_handover;

/**
 * @brief Enable backup-domain write access (PWR_CR1.DBP) and clock PWR.
 *
 * Zephyr does not expose a generic backup-register API for the F7, so
 * this small SoC helper touches only PWR and RTC registers.
 */
static void backup_domain_enable(void)
{
	/* The backup domain write protection is controlled by PWR_CR.DBP.
	 * Zephyr does not expose a generic backup-register API for the F7,
	 * so this small SoC helper touches only PWR and RTC.
	 */
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	(void)RCC->APB1ENR;
	PWR->CR1 |= PWR_CR1_DBP;
	__DSB();
}

/**
 * @brief Decode the BOOT hand-over record from RTC_BKP2R into g_handover.
 *
 * A missing record (no BOOT present) leaves @ref g_handover in the
 * UNKNOWN state. Trial bit and trial counter are unpacked per the
 * layout in boot_contract.h.
 */
void app_handover_read(void)
{
	backup_domain_enable();

	uint32_t raw = RTC->BKP2R;

	g_handover.raw = raw;
	g_handover.present = (raw & APP_HANDOVER_MAGIC_MASK) == APP_HANDOVER_MAGIC;
	if (!g_handover.present) {
		g_handover.slot = APP_SLOT_UNKNOWN;
		g_handover.trial = 0U;
		g_handover.trial_count = 0U;
		return;
	}

	g_handover.slot = (uint8_t)((raw >> APP_HANDOVER_SLOT_SHIFT) &
				    APP_HANDOVER_SLOT_MASK);
	if (g_handover.slot != APP_SLOT_A && g_handover.slot != APP_SLOT_B) {
		g_handover.slot = APP_SLOT_UNKNOWN;
		g_handover.present = false;
		g_handover.trial = 0U;
		g_handover.trial_count = 0U;
		return;
	}

	g_handover.trial = ((raw >> APP_HANDOVER_PENDING_BIT) & 1U) != 0U ? 2U : 1U;
	g_handover.trial_count = (uint8_t)((raw >> APP_HANDOVER_TRIAL_SHIFT) &
					   APP_HANDOVER_TRIAL_MASK);
}

/**
 * @brief Ask BOOT to stay in the bootloader on the next reset.
 *
 * Writes the "STAY" magic to RTC_BKP1R, which BOOT checks before
 * launching the active app slot.
 */
void app_handover_request_bootloader(void)
{
	backup_domain_enable();
	RTC->BKP1R = APP_STAY_IN_BOOT_MAGIC;
	__DSB();
}
