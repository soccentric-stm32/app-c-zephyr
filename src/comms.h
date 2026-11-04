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

bool comms_rx_running(void);
void comms_post_button_event(uint8_t press, uint8_t pattern, uint16_t period);

#endif /* ZDEMO_COMMS_H */
