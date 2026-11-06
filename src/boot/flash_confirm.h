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

/**
 * @brief Confirm the running slot by programming the CONFIRMED record.
 *
 * Programs the single CONFIRMED metadata record for the running slot if
 * it is absent (erased); no erase is ever issued. Idempotent when the
 * record is already valid.
 *
 * @return true on success (a valid CONFIRMED record is present),
 *         false on flash errors or an unexpected record state.
 */
bool app_flash_confirm_running_slot(void);

#endif /* ZDEMO_FLASH_CONFIRM_H */
