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

bool hwlink_uart_init(void);
bool hwlink_uart_set_baud(uint32_t baud);
uint32_t hwlink_uart_baud(void);

/* Drain received bytes from the software ring. */
uint32_t hwlink_uart_read(uint8_t *dst, uint32_t max_len);
bool hwlink_uart_rx_running(void);

/* Transmit one encoded frame and wait until it has left the UART. */
bool hwlink_uart_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

#endif /* ZDEMO_UART_HWLINK_H */
