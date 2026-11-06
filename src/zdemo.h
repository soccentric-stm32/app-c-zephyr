/*
 * zdemo.h - shared constants for the Zephyr demo application
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (Zephyr board nucleo_f767zi)
 * SRS:     SRS-ZEPHYR-F767
 */

/**
 * @file zdemo.h
 * @brief Shared constants for the Zephyr demo application.
 *
 * Firmware identity, thread priorities/stacks, LED ids and patterns,
 * UART baud rates, latched error bits, and the health-state codes used
 * across the LED, Button, Comms and Health threads.
 *
 * SRS: SRS-ZEPHYR-F767
 */

#ifndef ZDEMO_H
#define ZDEMO_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

/** Firmware name reported in HelloReply and Status (overridable). */
#ifndef APP_FIRMWARE_NAME
#define APP_FIRMWARE_NAME "zephyr-app"
#endif
/** Firmware version string (overridable). */
#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.1.0"
#endif
/** Git commit id at build time (overridable). */
#ifndef APP_GIT_COMMIT
#define APP_GIT_COMMIT "unknown"
#endif
/** Build date at build time (overridable). */
#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "unknown"
#endif

/* ZRT-001: Health highest, Comms high, Button and LED normal. */
/**< Priority of the Health thread (highest). */
#define ZDEMO_PRIO_HEALTH  2
/**< Priority of the Comms thread. */
#define ZDEMO_PRIO_COMMS   3
/**< Priority of the Button thread. */
#define ZDEMO_PRIO_BUTTON  4
/**< Priority of the LED thread (lowest). */
#define ZDEMO_PRIO_LED     5

/**< Health thread stack size, in bytes. */
#define ZDEMO_HEALTH_STACK 2048
/**< Comms thread stack size, in bytes. */
#define ZDEMO_COMMS_STACK  4096
/**< Button thread stack size, in bytes. */
#define ZDEMO_BUTTON_STACK 1024
/**< LED thread stack size, in bytes. */
#define ZDEMO_LED_STACK    1024

/* LED identifiers and patterns ([DEMO] Appendix A, [LINK]). */
/** User LED 1 id (LD1, user-controllable). */
#define APP_LED_ID_1       1U
/** User LED 2 id (LD2, user-controllable). */
#define APP_LED_ID_2       2U
/** Error/diagnostic LED id (LD3, latched error state). */
#define APP_LED_ID_3       3U

/**< LED off (never lit). */
#define APP_PATTERN_OFF        0U
/**< LED steadily on. */
#define APP_PATTERN_ON         1U
/**< LED blinking (on/off) at the configured period. */
#define APP_PATTERN_BLINK      2U
/**< Heartbeat: short on, long off, short on, pause. */
#define APP_PATTERN_HEARTBEAT  3U
/**< Fast: five short sub-blinks within the period. */
#define APP_PATTERN_FAST       4U
/**< Chase: LED1 then LED2 lit alternately. */
#define APP_PATTERN_CHASE      5U
/**< Morse "SOS" (... --- ...). */
#define APP_PATTERN_MORSE_SOS  6U
/**< Sentinel: number of defined patterns. */
#define APP_PATTERN_COUNT      7U

/**< Minimum LED blink period in ms. */
#define APP_LED_MIN_PERIOD     50U
/**< Maximum LED blink period in ms. */
#define APP_LED_MAX_PERIOD     5000U
/**< Default LED blink period in ms. */
#define APP_LED_DEFAULT_PERIOD 1000U

/**< Default HWLINK UART baud rate (bits/s). */
#define APP_UART_DEFAULT_BAUD  115200U
/**< Fast HWLINK UART baud rate (bits/s). */
#define APP_UART_FAST_BAUD     921600U

/* Latched diagnostic state shown on LD3. */
/**< Error bit: a fatal fault from a previous boot is latched. */
#define APP_ERROR_FAULT      (1UL << 0U)
/**< Error bit: a watchdog reset is latched. */
#define APP_ERROR_WATCHDOG   (1UL << 1U)
/**< Error bit: the health check has failed. */
#define APP_ERROR_HEALTH     (1UL << 2U)

/* Health state values used by Status. */
/**< Health: no trial is in progress; check not required. */
#define APP_HEALTH_NOT_REQUIRED 0U
/**< Health: a trial boot is currently running. */
#define APP_HEALTH_RUNNING      1U
/**< Health: the boot health check passed. */
#define APP_HEALTH_PASSED       2U
/**< Health: the boot health check failed. */
#define APP_HEALTH_FAILED       3U

/** @see implementation in main.c (heapless: reports 0/0). */
void app_heap_stats(uint32_t *free_bytes, uint32_t *min_free_bytes);

#endif /* ZDEMO_H */
