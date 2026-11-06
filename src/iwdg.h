/*
 * iwdg.h - watchdog service on the STM32 IWDG
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-004, ZBT-002, ZFN-004 (HLT-003)
 */

#ifndef ZDEMO_IWDG_H
#define ZDEMO_IWDG_H

#include <stdbool.h>

/**
 * @brief Feed the STM32 IWDG as early as possible.
 *
 * BOOT starts the IWDG before hand-over, so this must be callable from
 * PRE_KERNEL context. Safe no-op if the driver is not yet ready.
 */
void app_wdt_feed(void);

/**
 * @brief Install the 4 s IWDG timeout (once) and feed it again.
 *
 * @return true on success, false on driver or configuration error.
 */
bool app_wdt_init(void);

#endif /* ZDEMO_IWDG_H */
