/*
 * handover.h - BOOT hand-over record in RTC_BKP2R
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZBT-004, ZBT-005
 */

#ifndef ZDEMO_HANDOVER_H
#define ZDEMO_HANDOVER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Decoded BOOT hand-over state.
 */
typedef struct {
    bool present;
    uint8_t slot;         /* APP_SLOT_* */
    uint8_t trial;        /* 0 unknown, 1 confirmed, 2 pending */
    uint8_t trial_count;
    uint32_t raw;
} app_handover_t;

/** Global decoded hand-over state, populated by @ref app_handover_read. */
extern app_handover_t g_handover;

/**
 * @brief Read and decode the BOOT hand-over record from RTC_BKP2R.
 *
 * On success g_handover.slot and trial fields are set; on a missing
 * record they are reset to the UNKNOWN state.
 */
void app_handover_read(void);

/**
 * @brief Set RTC_BKP1R so BOOT stays in the bootloader on the next reset.
 */
void app_handover_request_bootloader(void);

#endif /* ZDEMO_HANDOVER_H */
