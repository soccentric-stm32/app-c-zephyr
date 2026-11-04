/*
 * health.h - task supervision, watchdog and CONFIRMED record
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-004, ZFN-004 (HLT-001..HLT-005)
 */

#ifndef ZDEMO_HEALTH_H
#define ZDEMO_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_TASK_LED = 0,
    HEALTH_TASK_BUTTON,
    HEALTH_TASK_COMMS,
    HEALTH_TASK_COUNT
} health_task_id_t;

void health_checkin(health_task_id_t task);
uint8_t health_state_get(void);
bool health_is_failed(void);

#endif /* ZDEMO_HEALTH_H */
