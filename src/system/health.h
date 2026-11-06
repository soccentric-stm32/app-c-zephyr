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

/**
 * @brief Supervised task identities (one event bit each).
 */
typedef enum {
    HEALTH_TASK_LED = 0,  /**< LED pattern thread. */
    HEALTH_TASK_BUTTON,   /**< Button debounce thread. */
    HEALTH_TASK_COMMS,    /**< HWLINK comms thread. */
    HEALTH_TASK_COUNT     /**< Sentinel: number of supervised tasks. */
} health_task_id_t;

/**
 * @brief Post a health check-in bit for one supervised task.
 *
 * Must be called at least once every second by each supervised thread.
 *
 * @param task The calling task's identity.
 */
void health_checkin(health_task_id_t task);

/**
 * @brief Return the current health state code (APP_HEALTH_*).
 */
uint8_t health_state_get(void);

/**
 * @brief Check whether the boot health check has produced a FAILED verdict.
 */
bool health_is_failed(void);

#endif /* ZDEMO_HEALTH_H */
