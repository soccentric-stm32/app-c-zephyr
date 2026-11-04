/*
 * zdemo.h - shared constants for the Zephyr demo application
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (Zephyr board nucleo_f767zi)
 * SRS:     SRS-ZEPHYR-F767
 */

#ifndef ZDEMO_H
#define ZDEMO_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef APP_FIRMWARE_NAME
#define APP_FIRMWARE_NAME "zephyr-app"
#endif
#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.1.0"
#endif
#ifndef APP_GIT_COMMIT
#define APP_GIT_COMMIT "unknown"
#endif
#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "unknown"
#endif

/* ZRT-001: Health highest, Comms high, Button and LED normal. */
#define ZDEMO_PRIO_HEALTH  2
#define ZDEMO_PRIO_COMMS   3
#define ZDEMO_PRIO_BUTTON  4
#define ZDEMO_PRIO_LED     5

#define ZDEMO_HEALTH_STACK 2048
#define ZDEMO_COMMS_STACK  4096
#define ZDEMO_BUTTON_STACK 1024
#define ZDEMO_LED_STACK    1024

/* LED identifiers and patterns ([DEMO] Appendix A, [LINK]). */
#define APP_LED_ID_1       1U
#define APP_LED_ID_2       2U
#define APP_LED_ID_3       3U

#define APP_PATTERN_OFF        0U
#define APP_PATTERN_ON         1U
#define APP_PATTERN_BLINK      2U
#define APP_PATTERN_HEARTBEAT  3U
#define APP_PATTERN_FAST       4U
#define APP_PATTERN_CHASE      5U
#define APP_PATTERN_MORSE_SOS  6U
#define APP_PATTERN_COUNT      7U

#define APP_LED_MIN_PERIOD     50U
#define APP_LED_MAX_PERIOD     5000U
#define APP_LED_DEFAULT_PERIOD 1000U

#define APP_UART_DEFAULT_BAUD  115200U
#define APP_UART_FAST_BAUD     921600U

/* Latched diagnostic state shown on LD3. */
#define APP_ERROR_FAULT      (1UL << 0U)
#define APP_ERROR_WATCHDOG   (1UL << 1U)
#define APP_ERROR_HEALTH     (1UL << 2U)

/* Health state values used by Status. */
#define APP_HEALTH_NOT_REQUIRED 0U
#define APP_HEALTH_RUNNING      1U
#define APP_HEALTH_PASSED       2U
#define APP_HEALTH_FAILED       3U

void app_heap_stats(uint32_t *free_bytes, uint32_t *min_free_bytes);

#endif /* ZDEMO_H */
