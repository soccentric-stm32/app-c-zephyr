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

#define APP_FAULT_MAGIC          0x4641554CUL  /* "FAUL" */
#define APP_FAULT_TASK_LEN       16U
#define APP_FAULT_FILE_LEN       32U

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

extern app_fault_record_t g_app_fault;

void app_fault_boot_start(void);
bool app_fault_is_valid(void);
bool app_fault_is_current_boot(void);
bool app_fault_is_previous_boot(void);
const char *app_fault_reason_text(app_fault_reason_t reason);
const char *app_fault_task(void);
const char *app_fault_file(void);
void app_fault_clear(void);

#endif /* ZDEMO_APP_FAULT_H */
