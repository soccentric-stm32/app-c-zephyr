/*
 * button.h - user button B1 (PC13), GPIO interrupt + 30 ms debounce
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZFN-002 (BTN-001..BTN-004)
 */

/**
 * @file button.h
 * @brief Public interface placeholder for user button B1 (PC13).
 *
 * The button driver (implementation in button.c) handles GPIO interrupt
 * and 30 ms debounce internally and communicates with other threads
 * through the LED message queue, so this header currently exposes no
 * public API.
 */
#ifndef ZDEMO_BUTTON_H
#define ZDEMO_BUTTON_H

#endif /* ZDEMO_BUTTON_H */
