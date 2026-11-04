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

extern uint32_t app_reset_cause_flags;
extern const char *app_reset_cause_text;

void app_reset_cause_capture(void);
bool app_reset_was_error(void);

#endif /* ZDEMO_RESET_CAUSE_H */
