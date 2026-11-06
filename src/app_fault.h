/*
 * app_fault.h - no-init fault record and fatal paths
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZGEN-004 (DGEN-004), ZSAFE-004
 */

#ifndef ZDEMO_APP_FAULT_H
#define ZDEMO_APP_FAULT_H

#include <stdbool.h>
#include <stdint.h>

/** Magic value ('FAUL') identifying a valid non-volatile fault record. */
#define APP_FAULT_MAGIC          0x4641554CUL  /* "FAUL" */
/** Maximum length (including NUL) of the stored task name. */
#define APP_FAULT_TASK_LEN       16U
/** Maximum length (including NUL) of the stored source file name. */
#define APP_FAULT_FILE_LEN       32U

/**
 * @brief Reason categories for a fatal fault captured by this module.
 *
 * The reason is recorded in @ref app_fault_record_t so the reason can be
 * inspected on the following boot.
 */
typedef enum {
    APP_FAULT_NONE = 0,
    APP_FAULT_HARDFAULT,
    APP_FAULT_MEMMANAGE,
    APP_FAULT_BUSFAULT,
    APP_FAULT_USAGEFAULT,
    APP_FAULT_ASSERT,
    APP_FAULT_STACK_OVERFLOW,
    APP_FAULT_MALLOC_FAILED,
    APP_FAULT_STARTUP
} app_fault_reason_t;

/**
 * @brief No-init persistent record of the last fatal fault.
 *
 * Stored in no-init RAM so the record survives a reset. The \c magic
 * field distinguishes a valid record from power-on garbage.
 *
 * @note Do not reorder members without updating the reader/writer side;
 *       the record is consumed by the boot monitor for diagnostics.
 */
typedef struct {
    uint32_t magic;
    uint32_t reason;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t line;
    uint32_t boot_id;
    char task[APP_FAULT_TASK_LEN];
    char file[APP_FAULT_FILE_LEN];
} app_fault_record_t;

/** Persistent no-init fault record, shared with the fault capture code. */
extern app_fault_record_t g_app_fault;

/**
 * @brief Called at boot start to stamp this boot into the fault record.
 */
void app_fault_boot_start(void);

/**
 * @brief Check whether the fault record holds a valid (magic-checked) entry.
 *
 * @return true if the record magic is valid, false otherwise.
 */
bool app_fault_is_valid(void);

/**
 * @brief Check whether the recorded fault belongs to the current boot.
 *
 * @return true if the record was written during the running boot.
 */
bool app_fault_is_current_boot(void);

/**
 * @brief Check whether the recorded fault is from the previous boot.
 *
 * @return true if the record was written during the boot before this one.
 */
bool app_fault_is_previous_boot(void);

/**
 * @brief Convert a fault reason into a human-readable string.
 *
 * @param reason Fault reason to convert.
 * @return NUL-terminated, read-only description of @p reason.
 */
const char *app_fault_reason_text(app_fault_reason_t reason);

/**
 * @brief Get the task name stored in the fault record.
 *
 * @return NUL-terminated task name (may be empty).
 */
const char *app_fault_task(void);

/**
 * @brief Get the source file name stored in the fault record.
 *
 * @return NUL-terminated file name (may be empty).
 */
const char *app_fault_file(void);

/**
 * @brief Erase the fault record (clears magic and stored data).
 */
void app_fault_clear(void);

#endif /* ZDEMO_APP_FAULT_H */
