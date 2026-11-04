/*
 * zdemo_threads.h - statically defined application threads
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-001, ZRT-005
 */

#ifndef ZDEMO_THREADS_H
#define ZDEMO_THREADS_H

#include <zephyr/kernel.h>

extern const k_tid_t zdemo_health_thread;
extern const k_tid_t zdemo_comms_thread;
extern const k_tid_t zdemo_button_thread;
extern const k_tid_t zdemo_led_thread;

#endif /* ZDEMO_THREADS_H */
