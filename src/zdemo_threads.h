/*
 * zdemo_threads.h - statically defined application threads
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-001, ZRT-005
 */

/**
 * @file zdemo_threads.h
 * @brief Statically defined application thread handles.
 *
 * Exposes the four Zephyr threads (Health, Comms, Button, LED) created
 * with K_THREAD_DEFINE in the corresponding .c files so other code can
 * reference them (e.g., for stack inspection in Status).
 *
 * SRS: ZRT-001, ZRT-005
 */

#ifndef ZDEMO_THREADS_H
#define ZDEMO_THREADS_H

#include <zephyr/kernel.h>

/**< Thread id of the Health / supervision thread. */
extern const k_tid_t zdemo_health_thread;
/**< Thread id of the Comms (HWLINK UART) thread. */
extern const k_tid_t zdemo_comms_thread;
/**< Thread id of the Button debounce thread. */
extern const k_tid_t zdemo_button_thread;
/**< Thread id of the LED pattern thread. */
extern const k_tid_t zdemo_led_thread;

#endif /* ZDEMO_THREADS_H */
