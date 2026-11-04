/*
 * uart_hwlink.c - interrupt-driven USART3 transport for HWLINK
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI (PD8 TX, PD9 RX)
 * SRS:     ZCON-005, ZFN-003 (COM-001, COM-002)
 *
 * RX is collected by the UART ISR into a 4 KB software ring; TX is also
 * interrupt-driven and only the Comms thread ever waits for completion.
 * The Zephyr console, shell and log backends are disabled on this UART, so
 * every byte belongs to HWLINK (COM-007).
 */

#include <soc.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "hwlink.h"
#include "uart_hwlink.h"
#include "zdemo.h"

#define UART_RX_RING_SIZE 4096U
#define UART_TX_TIMEOUT   200U

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(usart3));

RING_BUF_DECLARE(uart_rx_ring, UART_RX_RING_SIZE);

static K_SEM_DEFINE(uart_tx_sem, 0, 1);

static uint8_t uart_tx_buf[HWLINK_MAX_ENCODED];
static volatile uint16_t uart_tx_len;
static volatile uint16_t uart_tx_pos;
static volatile bool uart_rx_enabled;
static uint32_t uart_baud = APP_UART_DEFAULT_BAUD;

static void uart_clear_errors(void)
{
	uint32_t isr = USART3->ISR;

	if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE |
		    USART_ISR_PE)) != 0U) {
		volatile uint32_t dr = USART3->RDR;

		(void)dr;
		USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF |
			      USART_ICR_NCF | USART_ICR_PECF;
	}
}

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	(void)uart_irq_update(dev);

	uart_clear_errors();

	while (uart_irq_rx_ready(dev)) {
		uint8_t chunk[32];
		int got = uart_fifo_read(dev, chunk, sizeof(chunk));

		if (got <= 0) {
			break;
		}
		(void)ring_buf_put(&uart_rx_ring, chunk, (uint32_t)got);
	}

	if (uart_irq_tx_ready(dev)) {
		int sent = uart_fifo_fill(dev, &uart_tx_buf[uart_tx_pos],
					  (int)(uart_tx_len - uart_tx_pos));

		if (sent > 0) {
			uart_tx_pos += (uint16_t)sent;
		}
		if (uart_tx_pos >= uart_tx_len) {
			uart_irq_tx_disable(dev);
			k_sem_give(&uart_tx_sem);
		}
	}
}

static void uart_rx_start(void)
{
	ring_buf_reset(&uart_rx_ring);
	uart_irq_rx_enable(uart);
	uart_irq_err_enable(uart);
	uart_rx_enabled = true;
}

bool hwlink_uart_init(void)
{
	if (!device_is_ready(uart)) {
		return false;
	}

	const struct uart_config cfg = {
		.baudrate = APP_UART_DEFAULT_BAUD,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	if (uart_configure(uart, &cfg) != 0) {
		return false;
	}

	uart_irq_callback_set(uart, uart_isr);
	uart_rx_start();
	uart_baud = APP_UART_DEFAULT_BAUD;
	return true;
}

bool hwlink_uart_set_baud(uint32_t baud)
{
	if (baud != APP_UART_DEFAULT_BAUD && baud != APP_UART_FAST_BAUD) {
		return false;
	}
	if (baud == uart_baud) {
		return true;
	}

	struct uart_config cfg;
	uint32_t old_baud = uart_baud;

	if (uart_config_get(uart, &cfg) != 0) {
		return false;
	}

	uart_irq_err_disable(uart);
	uart_irq_rx_disable(uart);
	uart_rx_enabled = false;

	cfg.baudrate = baud;
	if (uart_configure(uart, &cfg) != 0) {
		/* Try to return to the previous rate. */
		cfg.baudrate = old_baud;
		(void)uart_configure(uart, &cfg);
		uart_rx_start();
		return false;
	}

	uart_baud = baud;
	uart_rx_start();
	return true;
}

uint32_t hwlink_uart_baud(void)
{
	return uart_baud;
}

uint32_t hwlink_uart_read(uint8_t *dst, uint32_t max_len)
{
	return ring_buf_get(&uart_rx_ring, dst, max_len);
}

bool hwlink_uart_rx_running(void)
{
	return uart_rx_enabled && device_is_ready(uart);
}

bool hwlink_uart_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
	if (len == 0U || data == NULL) {
		return true;
	}
	if (len > sizeof(uart_tx_buf)) {
		return false;
	}

	/* Drop a stale completion, then hand the frame to the ISR. */
	(void)k_sem_take(&uart_tx_sem, K_NO_WAIT);
	memcpy(uart_tx_buf, data, len);
	uart_tx_len = len;
	uart_tx_pos = 0U;
	uart_irq_tx_enable(uart);

	if (k_sem_take(&uart_tx_sem, K_MSEC(timeout_ms)) != 0) {
		uart_irq_tx_disable(uart);
		return false;
	}

	/* DBT-005/APP-006: wait until the last stop bit has left the shift
	 * register, not merely until the DMA/FIFO has accepted the byte. */
	uint32_t start = k_uptime_get_32();

	while ((USART3->ISR & USART_ISR_TC) == 0U) {
		if ((k_uptime_get_32() - start) > 10U) {
			break;
		}
	}
	USART3->ICR = USART_ICR_TCCF;

	return true;
}
