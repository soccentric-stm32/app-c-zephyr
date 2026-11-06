/*
 * flash_confirm.c - program the single CONFIRMED metadata record
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI, dual-bank flash
 * SRS:     ZBT-006, ZFN-004 (HLT-002, HLT-005), ZSAFE-001/002
 *
 * Appendix B of SRS-BOOT-F767 defines a 32-byte record: magic in word 0,
 * CRC-32/ISO-HDLC over the first 28 bytes in the last word.  The metadata
 * sector is erased by BOOT before the trial; the application never erases.
 */

#include <soc.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include "boot/boot_contract.h"
#include "boot/flash_confirm.h"

/* Number of 32-bit words in one 32-byte CONFIRMED record. */
#define CONFIRMED_WORDS (APP_META_RECORD_SIZE / 4U)

BUILD_ASSERT(PARTITION_ADDRESS(app_meta_partition) == APP_RUNNING_META_ADDR,
	     "app_meta_partition must alias the running slot metadata sector");
BUILD_ASSERT(PARTITION_SIZE(app_meta_partition) == APP_META_SIZE,
	     "app_meta_partition must cover exactly one metadata sector");
BUILD_ASSERT(APP_META_CONFIRMED_OFFSET + APP_META_RECORD_SIZE <= APP_META_SIZE,
	     "CONFIRMED record must lie inside app_meta_partition");

/** Flash device covering the running slot's metadata sector. */
static const struct device *const flash_dev =
	PARTITION_DEVICE(app_meta_partition);
/** Flash offset of the CONFIRMED record within the metadata partition. */
static const off_t record_offset =
	PARTITION_OFFSET(app_meta_partition) + APP_META_CONFIRMED_OFFSET;

/**
 * @brief Invalidate the cache line covering the CONFIRMED record.
 *
 * The STM32F7 flash driver flushes the D-cache for erase but not for a
 * byte-programming write, so every read must first invalidate the one
 * metadata record to see the actual flash contents.
 */
static void record_cache_invalidate(void)
{
	SCB_InvalidateDCache_by_Addr(
		(uint32_t *)(APP_RUNNING_META_ADDR + APP_META_CONFIRMED_OFFSET),
		APP_META_RECORD_SIZE);
}

/**
 * @brief Check CONFIRMED-record magic and trailing CRC-32.
 *
 * @param record Pointer to 8 words of flash data.
 * @return true if magic and CRC are both correct.
 */
static bool record_valid(const uint32_t *record)
{
	if (record[0] != APP_META_CONFIRMED_MAGIC) {
		return false;
	}
	return crc32_ieee((const uint8_t *)record, APP_META_RECORD_SIZE - 4U) ==
	       record[CONFIRMED_WORDS - 1U];
}

/**
 * @brief Check whether the CONFIRMED record is all-erased (0xFF).
 *
 * @param record Pointer to 8 words of flash data.
 * @return true if every word is the erased state.
 */
static bool record_erased(const uint32_t *record)
{
	for (uint32_t i = 0U; i < CONFIRMED_WORDS; i++) {
		if (record[i] != 0xFFFFFFFFUL) {
			return false;
		}
	}
	return true;
}

/**
 * @brief Confirm the running slot by programming the CONFIRMED record.
 *
 * If the record is already valid it is a no-op. If erased, the one fixed
 * record location is programmed (magic + CRC) and verified by re-reading.
 * The application never erases; BOOT erases the metadata sector before
 * a trial.
 *
 * @return true when a valid CONFIRMED record is present afterwards,
 *         false on flash errors or an unexpected (non-erased) record.
 */
bool app_flash_confirm_running_slot(void)
{
	uint32_t record[CONFIRMED_WORDS];

	if (!device_is_ready(flash_dev)) {
		return false;
	}

	record_cache_invalidate();
	if (flash_read(flash_dev, record_offset, record, sizeof(record)) != 0) {
		return false;
	}
	if (record_valid(record)) {
		return true;
	}
	if (!record_erased(record)) {
		return false;
	}

	memset(record, 0, sizeof(record));
	record[0] = APP_META_CONFIRMED_MAGIC;
	record[CONFIRMED_WORDS - 1U] =
		crc32_ieee((const uint8_t *)record, APP_META_RECORD_SIZE - 4U);

	/* ZSAFE-002: the address is fixed here, never supplied by a caller. */
	if (flash_write(flash_dev, record_offset, record, sizeof(record)) != 0) {
		return false;
	}

	record_cache_invalidate();

	uint32_t verify[CONFIRMED_WORDS];

	if (flash_read(flash_dev, record_offset, verify, sizeof(verify)) != 0) {
		return false;
	}
	return record_valid(verify);
}
