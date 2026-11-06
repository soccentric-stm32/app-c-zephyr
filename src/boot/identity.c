/*
 * identity.c - factory provisioning record reader (A-03)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-04
 * Target:  STM32F767ZIT6
 * SRS:     identity contract PRV1, factory A-02/A-03/A-05/A-06
 *
 * The factory writes a PRV1 record into bank 2 sector 13 (0x08104000, or
 * 0x08004000 while bank 2 is swapped in).  This reader checks the header,
 * both CRC-32 values and the chip UID binding, then exposes the serial,
 * product, revision, profile and MAC.  A blank or torn record is reported as
 * absent; it is never treated as valid identity.
 */

#include <string.h>
#include <soc.h>
#include <zephyr/sys/crc.h>
#include "boot/identity.h"

/** PRV1 record magic ('PRV1' little-endian). */
#define ID_MAGIC              0x31565250UL  /* "PRV1" little-endian */
/** Record format version we understand. */
#define ID_FORMAT             1U
/** Size of the fixed identity header, in bytes. */
#define ID_HEADER_SIZE        32U
/** Maximum TLV body we will scan, in bytes. */
#define ID_MAX_TLV            4096U

/** TLV type: device serial number. */
#define ID_T_SERIAL           0x01U
/** TLV type: product name. */
#define ID_T_PRODUCT          0x02U
/** TLV type: hardware revision. */
#define ID_T_HW_REV           0x03U
/** TLV type: manufacturing timestamp. */
#define ID_T_MFG_TIME         0x04U
/** TLV type: station/fixture identifier. */
#define ID_T_STATION_FIXTURE  0x05U
/** TLV type: provisioning profile. */
#define ID_T_PROFILE          0x06U
/** TLV type: firmware version at provisioning. */
#define ID_T_FIRMWARE_VERSION 0x07U
/** TLV type: MAC address (6 bytes). */
#define ID_T_MAC              0x10U

/** Physical address of the identity record in bank 2 sector 13. */
#define ID_IDENTITY_A         0x08104000UL
/** Same sector, as seen while bank 2 is swapped into the low alias. */
#define ID_IDENTITY_SWAPPED   0x08004000UL

/**
 * @brief Fixed 32-byte PRV1 identity header.
 *
 * The header is CRC-32 protected (header_crc32) and bound to the chip
 * UID (12 bytes) so a record copied to another chip is rejected.
 */
typedef struct {
    uint32_t magic;
    uint16_t format;
    uint16_t revision;
    uint32_t length;
    uint32_t tlv_crc32;
    uint8_t uid[12];
    uint32_t header_crc32;
} id_header_t;

/**
 * @brief Decoded, validated identity fields kept in RAM.
 */
typedef struct {
    bool valid;
    uint16_t revision;
    char serial[33];
    char product[33];
    char hw_revision[9];
    char profile[65];
    uint8_t mac[6];
    bool mac_valid;
} app_identity_t;

/** Cached decode state; valid after a successful app_identity_refresh(). */
static app_identity_t identity;
/** True once app_identity_refresh() has run at least once. */
static bool refreshed;

/**
 * @brief Compute the header CRC over the first 28 header bytes.
 *
 * @param raw Pointer to the 32-byte header.
 * @return CRC-32/ISO-HDLC value.
 */
static uint32_t header_crc(const uint8_t *raw)
{
    return crc32_ieee(raw, 28U);
}

/**
 * @brief Copy a bounded-length text field and force NUL termination.
 *
 * @param out    Destination buffer.
 * @param cap    Size of the destination buffer.
 * @param data   Source bytes.
 * @param length Number of source bytes to copy.
 */
static void copy_text(char *out, size_t cap, const uint8_t *data, uint8_t length)
{
    size_t n = length;
    if (n >= cap) {
        n = cap - 1U;
    }
    memcpy(out, data, n);
    out[n] = '\0';
}

/**
 * @brief Validate and parse one PRV1 record at a fixed address.
 *
 * Checks magic, format, length range, header CRC, the chip UID binding
 * and the TLV CRC, then decodes serial/product/hw-revision/profile/MAC.
 *
 * @param address Flash address of the record.
 * @return true if a complete, valid identity was parsed.
 */
static bool parse_from(uint32_t address)
{
    uint8_t raw[ID_HEADER_SIZE];
    const uint32_t *words = (const uint32_t *)address;

    SCB_InvalidateDCache_by_Addr((uint32_t *)address, ID_MAX_TLV);
    memcpy(raw, (const void *)address, sizeof(raw));
    if (words[0] != ID_MAGIC) {
        return false;
    }

    id_header_t header;
    memcpy(&header, raw, sizeof(header));
    if (header.format != ID_FORMAT || header.length == 0U ||
        header.length > ID_MAX_TLV || header.length < 2U) {
        return false;
    }
    if (header_crc(raw) != header.header_crc32) {
        return false;
    }

    /* UID binding: the record has the three UID words in flash byte order. */
    uint8_t uid[12];
    uint32_t uid_words[3] = {
        *(const volatile uint32_t *)0x1FF0F420U,
        *(const volatile uint32_t *)0x1FF0F424U,
        *(const volatile uint32_t *)0x1FF0F428U,
    };
    memcpy(uid, uid_words, sizeof(uid));
    if (memcmp(uid, header.uid, sizeof(uid)) != 0) {
        return false;
    }

    const uint8_t *tlv = (const uint8_t *)(address + ID_HEADER_SIZE);
    if (crc32_ieee(tlv, header.length) != header.tlv_crc32) {
        return false;
    }

    memset(&identity, 0, sizeof(identity));
    identity.revision = header.revision;
    size_t offset = 0U;
    while (offset + 2U <= header.length) {
        uint8_t type = tlv[offset];
        uint8_t length = tlv[offset + 1U];
        offset += 2U;
        if (type == 0U) {
            break;
        }
        if (offset + length > header.length) {
            return false;
        }
        const uint8_t *value = &tlv[offset];
        switch (type) {
        case ID_T_SERIAL:
            copy_text(identity.serial, sizeof(identity.serial), value, length);
            break;
        case ID_T_PRODUCT:
            copy_text(identity.product, sizeof(identity.product), value, length);
            break;
        case ID_T_HW_REV:
            copy_text(identity.hw_revision, sizeof(identity.hw_revision), value, length);
            break;
        case ID_T_PROFILE:
            copy_text(identity.profile, sizeof(identity.profile), value, length);
            break;
        case ID_T_MAC:
            if (length == 6U) {
                memcpy(identity.mac, value, 6U);
                identity.mac_valid = true;
            }
            break;
        default:
            break;
        }
        offset += length;
    }

    if (identity.serial[0] == '\0' || identity.product[0] == '\0') {
        return false;
    }
    identity.valid = true;
    return true;
}

/**
 * @brief (Re)load the identity record from both candidate addresses.
 *
 * Tries the physical bank-2 address first, then the swapped-in alias.
 *
 * @return true if a valid identity was found.
 */
bool app_identity_refresh(void)
{
    refreshed = true;
    memset(&identity, 0, sizeof(identity));
    if (parse_from(ID_IDENTITY_A)) {
        return true;
    }
    return parse_from(ID_IDENTITY_SWAPPED);
}

/**
 * @brief Ensure the identity has been loaded once; return its validity.
 *
 * @return true if a valid identity is cached.
 */
static bool ensure_loaded(void)
{
    if (!refreshed) {
        (void)app_identity_refresh();
    }
    return identity.valid;
}

/**
 * @brief Check whether a valid identity record is present.
 */
bool app_identity_valid(void)
{
    return ensure_loaded();
}

/**
 * @brief Device serial number (empty string if no identity).
 *
 * @return NUL-terminated serial.
 */
const char *app_identity_serial(void)
{
    return ensure_loaded() ? identity.serial : "";
}

/**
 * @brief Product name (empty string if no identity).
 *
 * @return NUL-terminated product name.
 */
const char *app_identity_product(void)
{
    return ensure_loaded() ? identity.product : "";
}

/**
 * @brief Hardware revision string (empty string if no identity).
 *
 * @return NUL-terminated revision.
 */
const char *app_identity_hw_revision(void)
{
    return ensure_loaded() ? identity.hw_revision : "";
}

/**
 * @brief Provisioning profile string (empty string if no identity).
 *
 * @return NUL-terminated profile.
 */
const char *app_identity_profile(void)
{
    return ensure_loaded() ? identity.profile : "";
}

/**
 * @brief Record revision number.
 *
 * @return Revision, or 0 if no identity is present.
 */
uint16_t app_identity_revision(void)
{
    return ensure_loaded() ? identity.revision : 0U;
}

/**
 * @brief Copy the 6-byte factory MAC into the caller buffer.
 *
 * @param mac Destination buffer of at least 6 bytes.
 * @return true on success, false if no MAC is recorded.
 */
bool app_identity_mac(uint8_t mac[6])
{
    if (!ensure_loaded() || !identity.mac_valid) {
        return false;
    }
    memcpy(mac, identity.mac, 6U);
    return true;
}

/**
 * @brief Append a C string to an output buffer, NUL-safe.
 *
 * @param out   Output buffer.
 * @param cap   Size of the output buffer.
 * @param pos   Current position in out.
 * @param text  String to append.
 * @return New position (always NUL-terminates before returning).
 */
static size_t append_text(char *out, size_t cap, size_t pos, const char *text)
{
    while (text != NULL && *text != '\0' && pos + 1U < cap) {
        out[pos++] = *text++;
    }
    out[pos] = '\0';
    return pos;
}

/**
 * @brief Append an unsigned decimal number to an output buffer.
 *
 * @param out    Output buffer.
 * @param cap    Size of the output buffer.
 * @param pos    Current position in out.
 * @param value  Value to append.
 * @return New position (always NUL-terminates before returning).
 */
static size_t append_uint(char *out, size_t cap, size_t pos, unsigned value)
{
    char digits[10];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U && pos + 1U < cap) {
        out[pos++] = digits[--count];
    }
    out[pos] = '\0';
    return pos;
}

/**
 * @brief Append the cached MAC as colon-separated hex (XX:XX:XX:XX:XX:XX).
 *
 * @param out   Output buffer.
 * @param cap   Size of the output buffer.
 * @param pos   Current position in out.
 * @return New position (always NUL-terminates before returning).
 */
static size_t append_mac(char *out, size_t cap, size_t pos)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0U; i < 6U && pos + 3U < cap; i++) {
        if (i != 0U) {
            out[pos++] = ':';
        }
        out[pos++] = hex[(identity.mac[i] >> 4U) & 0xFU];
        out[pos++] = hex[identity.mac[i] & 0xFU];
    }
    out[pos] = '\0';
    return pos;
}

/**
 * @brief Render the identity as a compact single-line summary.
 *
 * On success the output looks like: PRV1 serial=<s>;mac=<m>;hw=<h>;
 * product=<p>;profile=<f>;revision=<n>
 *
 * @param out      Destination buffer.
 * @param capacity Size of the destination buffer.
 * @return true if an identity was available and rendered,
 *         false on no identity (out contains "no identity").
 */
bool app_identity_detail(char *out, size_t capacity)
{
    if (out == NULL || capacity == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!ensure_loaded()) {
        (void)append_text(out, capacity, 0U, "no identity");
        return false;
    }
    size_t pos = 0U;
    pos = append_text(out, capacity, pos, "PRV1 serial=");
    pos = append_text(out, capacity, pos, identity.serial);
    pos = append_text(out, capacity, pos, ";mac=");
    if (identity.mac_valid) {
        pos = append_mac(out, capacity, pos);
    }
    pos = append_text(out, capacity, pos, ";hw=");
    pos = append_text(out, capacity, pos, identity.hw_revision);
    pos = append_text(out, capacity, pos, ";product=");
    pos = append_text(out, capacity, pos, identity.product);
    pos = append_text(out, capacity, pos, ";profile=");
    pos = append_text(out, capacity, pos, identity.profile);
    pos = append_text(out, capacity, pos, ";revision=");
    (void)append_uint(out, capacity, pos, identity.revision);
    return true;
}
