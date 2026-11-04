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

/* Queue operations from other threads (ZRT-002). */
int led_post_set(uint8_t led, uint8_t pattern, uint16_t period, bool force);
int led_post_set_all(uint8_t pattern, uint16_t period);
int led_post_button(uint8_t press);       /* 1 = short, 2 = long */
int led_post_clear_error(void);
int led_post_health_failed(void);
int led_post_error(uint32_t flags);

#endif /* ZDEMO_LED_H */
