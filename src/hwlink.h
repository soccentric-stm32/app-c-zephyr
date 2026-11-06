/*
 * hwlink.h - HWLINK framing: COBS, CRC-32 and the 8-byte header
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  STM32F767ZIT6
 * SRS:     HWLINK LNK-001..LNK-005 (SRS-GUI-F767 Appendix A)
 */

#ifndef HWLINK_H
#define HWLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**< HWLINK protocol major version. */
#define HWLINK_PROTOCOL_MAJOR     1U
/**< HWLINK protocol minor version. */
#define HWLINK_PROTOCOL_MINOR     1U
/**< Maximum raw frame size in bytes. */
#define HWLINK_MAX_FRAME          2048U
/**< Maximum COBS-encoded size (frame + 1 overhead byte per 254 + 2 guard). */
#define HWLINK_MAX_ENCODED        (HWLINK_MAX_FRAME + (HWLINK_MAX_FRAME / 254U) + 2U)

/**< Frame flag: this frame is a response to a request. */
#define HWLINK_FLAG_RESPONSE      (1U << 0U)
/**< Frame flag: this frame is an unsolicited event. */
#define HWLINK_FLAG_EVENT         (1U << 1U)

/**
 * @brief Build the unencoded frame (proto, flags, seq, payload, crc32).
 *
 * @param payload      Payload bytes to include.
 * @param payload_len  Length of payload in bytes.
 * @param flags        Frame flags.
 * @param seq          Sequence number.
 * @param raw          Destination buffer.
 * @param raw_capacity Size of the destination buffer.
 * @param raw_len      Receives the total frame length.
 * @return true on success.
 */
bool hwlink_build_raw(const uint8_t *payload, size_t payload_len,
                      uint8_t flags, uint16_t seq,
                      uint8_t *raw, size_t raw_capacity, size_t *raw_len);

/**
 * @brief COBS-encode a raw frame and append the terminating 0x00 delimiter.
 *
 * @param raw          Raw frame bytes.
 * @param raw_len      Length of the raw frame.
 * @param out          Destination buffer (capacity must cover HWLINK_MAX_ENCODED).
 * @param out_capacity Size of the destination buffer.
 * @param out_len      Receives the encoded length including the delimiter.
 * @return true on success.
 */
bool hwlink_cobs_encode(const uint8_t *raw, size_t raw_len,
                        uint8_t *out, size_t out_capacity, size_t *out_len);

/**
 * @brief COBS-decode one frame (without the delimiter).
 *
 * @param in           Encoded bytes (no delimiter).
 * @param in_len       Length of the encoded body.
 * @param out          Destination buffer.
 * @param out_capacity Size of the destination buffer.
 * @param out_len      Receives the decoded length.
 * @return 0 on success, -1 on error.
 */
int hwlink_cobs_decode(const uint8_t *in, size_t in_len,
                       uint8_t *out, size_t out_capacity, size_t *out_len);

/**
 * @brief Validate the header and CRC of an unencoded frame and return its payload.
 *
 * @param raw              Raw frame bytes.
 * @param raw_len          Length of the raw frame.
 * @param payload          Destination for the payload.
 * @param payload_capacity Size of the payload buffer.
 * @param payload_len      Receives the payload length.
 * @param flags            Receives the frame flags.
 * @param seq              Receives the sequence number.
 * @return true if the frame is valid.
 */
bool hwlink_decode_raw(const uint8_t *raw, size_t raw_len,
                       uint8_t *payload, size_t payload_capacity,
                       size_t *payload_len, uint8_t *flags, uint16_t *seq);

#endif /* HWLINK_H */
