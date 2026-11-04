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

typedef struct {
    bool present;
    uint8_t slot;         /* APP_SLOT_* */
    uint8_t trial;        /* 0 unknown, 1 confirmed, 2 pending */
    uint8_t trial_count;
    uint32_t raw;
} app_handover_t;

extern app_handover_t g_handover;

void app_handover_read(void);
void app_handover_request_bootloader(void);

#endif /* ZDEMO_HANDOVER_H */
