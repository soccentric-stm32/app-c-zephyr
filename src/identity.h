/*
 * identity.h - factory provisioning record reader (A-03)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-04
 * Target:  STM32F767ZIT6
 * SRS:     identity contract PRV1, factory A-02/A-03/A-05/A-06
 */

#ifndef IDENTITY_H
#define IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Reload and validate the PRV1 record from the identity sector.
 *
 * Tries the physical bank-2 address, then the swapped-in alias.
 *
 * @return true if a valid UID-bound record was found.
 */
bool app_identity_refresh(void);

/**
 * @brief Check whether a valid, UID-bound identity record is present.
 *
 * Triggers a lazy first load if none has occurred yet.
 *
 * @return true if identity is available.
 */
bool app_identity_valid(void);

/** @brief Device serial number; empty string when no record present. */
const char *app_identity_serial(void);
/** @brief Product name; empty string when no record present. */
const char *app_identity_product(void);
/** @brief Hardware revision string; empty string when absent. */
const char *app_identity_hw_revision(void);
/** @brief Provisioning profile; empty string when absent. */
const char *app_identity_profile(void);
/** @brief Record revision; 0 when absent. */
uint16_t app_identity_revision(void);

/**
 * @brief Copy the provisioned EUI-48 (6-byte MAC) into the caller buffer.
 *
 * @param mac Destination buffer of at least 6 bytes.
 * @return true on success, false when the MAC is absent.
 */
bool app_identity_mac(uint8_t mac[6]);

/**
 * @brief Render a compact key=value detail line for HelloReply (A-05).
 *
 * @param out      Destination buffer.
 * @param capacity Size of the destination buffer.
 * @return true if identity was rendered, false otherwise.
 */
bool app_identity_detail(char *out, size_t capacity);

#endif /* IDENTITY_H */
