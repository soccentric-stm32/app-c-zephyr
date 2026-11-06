/*
 * led.h - LED patterns and LD3 error display
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZFN-001 (LED-001..LED-007), ZFN-002 (BTN-002/003)
 */

#ifndef ZDEMO_LED_H
#define ZDEMO_LED_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief LED pattern / error-state setters callable from any task.
 *
 * All functions post a message to the LED thread and return immediately.
 * The LED thread owns the pattern state and LD3 error display, so these
 * calls never block on the pins directly.
 */

/**
 * @brief Set the pattern and period for one user LED.
 *
 * @param led     LED id (1 or 2; 3 requires force).
 * @param pattern Pattern id (APP_PATTERN_*).
 * @param period  Blink period in ms (APP_LED_MIN_PERIOD..APP_LED_MAX_PERIOD).
 * @param force   true to override the LD3 reservation guard.
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
int led_post_set(uint8_t led, uint8_t pattern, uint16_t period, bool force);

/**
 * @brief Set the pattern and period for both user LEDs at once.
 *
 * @param pattern Pattern id.
 * @param period  Blink period in ms.
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
int led_post_set_all(uint8_t pattern, uint16_t period);

/**
 * @brief Notify the LED thread of a button press.
 *
 * @param press 1 = short press, 2 = long press (long restores defaults).
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
int led_post_button(uint8_t press);

/**
 * @brief Clear an active error display (turn LD3 off / resume pattern).
 *
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
int led_post_clear_error(void);

/**
 * @brief Raise the health-failed indicator on LD3.
 *
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
int led_post_health_failed(void);

/**
 * @brief Raise a generic error indicator (LD3) with the given flags.
 *
 * @param flags APP_ERROR_* bit mask (see zdemo.h).
 * @return 0 on success, -MSGSYS_NOSPACE if the queue is full.
 */
int led_post_error(uint32_t flags);

#endif /* ZDEMO_LED_H */
