/*
 * flash_confirm.h - program the single CONFIRMED metadata record
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZBT-006, ZFN-004 (HLT-002, HLT-005), ZSAFE-001/002
 */

#ifndef ZDEMO_FLASH_CONFIRM_H
#define ZDEMO_FLASH_CONFIRM_H

#include <stdbool.h>

/* Program CONFIRMED for the running slot if absent. No erase is ever issued. */
bool app_flash_confirm_running_slot(void);

#endif /* ZDEMO_FLASH_CONFIRM_H */
