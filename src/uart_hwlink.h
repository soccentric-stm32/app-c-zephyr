/*
 * uart_hwlink.h - interrupt-driven USART3 transport for HWLINK
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (PD8 TX, PD9 RX)
 * SRS:     ZCON-005, ZFN-003 (COM-001, COM-002)
 */

#ifndef ZDEMO_UART_HWLINK_H
#define ZDEMO_UART_HWLINK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configure USART3 for HWLINK and start RX collection.
 *
 * Sets 8N1, no flow control, and enables the RX/error interrupts; all
 * other Zephyr users of this UART (console, shell, log) are disabled so
 * every byte is HWLINK (COM-007).
 *
 * @return true on success, false if the device is unavailable.
 */
bool hwlink_uart_init(void);

/**
 * @brief Change the UART baud rate (only DEFAULT or FAST is accepted).
 *
 * @param baud Requested rate in bits/s.
 * @return true on success, false if the rate is not a legal value.
 */
bool hwlink_uart_set_baud(uint32_t baud);

/**
 * @brief Return the currently active baud rate.
 *
 * @return Baud rate in bits/s.
 */
uint32_t hwlink_uart_baud(void);

/**
 * @brief Drain received bytes from the software ring.
 *
 * @param dst     Destination buffer.
 * @param max_len Maximum number of bytes to return.
 * @return Number of bytes placed in dst (0 if the ring is empty).
 */
uint32_t hwlink_uart_read(uint8_t *dst, uint32_t max_len);

/**
 * @brief Query whether RX is currently enabled and collecting.
 *
 * @return true if the RX path is up, false otherwise.
 */
bool hwlink_uart_rx_running(void);

/**
 * @brief Transmit one encoded frame and block until it has left the UART.
 *
 * Only the Comms thread calls this; it waits for TX completion with the
 * given timeout so the next frame (or a baud change) starts clean.
 *
 * @param data        Frame bytes to send.
 * @param len         Number of bytes to send.
 * @param timeout_ms  Maximum time to wait for completion.
 * @return true if the whole frame was transmitted, false on timeout.
 */
bool hwlink_uart_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

#endif /* ZDEMO_UART_HWLINK_H */
