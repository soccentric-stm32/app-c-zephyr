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

/* Feed as early as possible; BOOT starts the IWDG before hand-over. */
void app_wdt_feed(void);

/* Install a 4 s timeout (and feed again). */
bool app_wdt_init(void);

#endif /* ZDEMO_IWDG_H */
