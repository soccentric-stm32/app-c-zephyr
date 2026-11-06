/*
 * comms.h - HWLINK Comms thread
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZFN-003 (COM-001..COM-007), ZFN-005
 */

#ifndef ZDEMO_COMMS_H
#define ZDEMO_COMMS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Report whether the UART HWLINK RX path is running.
 *
 * @return true if RX has started and the ring is active.
 */
bool comms_rx_running(void);

/**
 * @brief Post a button event to the comms thread.
 *
 * If the HWLINK peer has identified itself, the event is also forwarded
 * to it as a ButtonEvent message.
 *
 * @param press   Press classification (1 = short, 2 = long).
 * @param pattern LED pattern associated with the press.
 * @param period  LED period in ms associated with the press.
 */
void comms_post_button_event(uint8_t press, uint8_t pattern, uint16_t period);

#endif /* ZDEMO_COMMS_H */
