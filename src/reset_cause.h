/*
 * reset_cause.h - reset cause captured through the hwinfo API
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZGEN-005 (DGEN-005)
 */

#ifndef ZDEMO_RESET_CAUSE_H
#define ZDEMO_RESET_CAUSE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Reset-cause information captured at boot.
 *
 * Populated by @ref app_reset_cause_capture; read-only thereafter.
 */
extern uint32_t app_reset_cause_flags;

/** @brief Short human-readable text for the most recent reset cause. */
extern const char *app_reset_cause_text;

/**
 * @brief Capture the reset cause and set app_reset_cause_text accordingly.
 *
 * Clears the sticky watchdog / reset flags on the SoC after sampling.
 * Call once, early in boot (before clearing the cause bits).
 */
void app_reset_cause_capture(void);

/**
 * @brief Test whether the most recent reset was caused by an error
 *        condition (watchdog, fault, or power glitch).
 *
 * @return true if the reset was the result of an error event.
 */
bool app_reset_was_error(void);

#endif /* ZDEMO_RESET_CAUSE_H */
