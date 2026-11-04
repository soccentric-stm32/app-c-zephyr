/*
 * fatal.c - no-init fault record and the Zephyr fatal error handler
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZGEN-004 (DGEN-004), ZSAFE-004
 *
 * The record lives in Zephyr's .noinit section, so it survives a system
 * reset and can be reported in Status.  The fatal path uses only register
 * accesses and a busy wait; it must work before drivers are up and with
 * interrupts disabled.
 */

#include <soc.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/arch/arm/arch.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sys/reboot.h>

#include "app_fault.h"

app_fault_record_t g_app_fault __noinit;
static uint32_t app_boot_id __noinit;

#define FAULT_LED_PORT GPIOB
#define FAULT_LED_PIN  14U

static const char *const fault_reason_names[] = {
	"none",
	"hard_fault",
	"mem_manage",
	"bus_fault",
	"usage_fault",
	"assert",
	"stack_overflow",
	"malloc_failed",
	"startup"
};

static void fault_copy(char *dst, uint32_t dst_size, const char *src)
{
	uint32_t i = 0U;

	if (dst_size == 0U) {
		return;
	}
	if (src != NULL) {
		while ((i + 1U) < dst_size && src[i] != '\0') {
			dst[i] = src[i];
			i++;
		}
	}
	dst[i] = '\0';
}

void app_fault_boot_start(void)
{
	app_boot_id++;
	if (app_boot_id == 0U) {
		app_boot_id = 1U;
	}
}

bool app_fault_is_valid(void)
{
	return g_app_fault.magic == APP_FAULT_MAGIC &&
	       g_app_fault.reason <= (uint32_t)APP_FAULT_STARTUP;
}

bool app_fault_is_current_boot(void)
{
	return app_fault_is_valid() && g_app_fault.boot_id == app_boot_id;
}

bool app_fault_is_previous_boot(void)
{
	uint32_t previous = app_boot_id - 1U;

	if (previous == 0U) {
		previous = 0xFFFFFFFFUL;
	}
	return app_fault_is_valid() && g_app_fault.boot_id == previous;
}

const char *app_fault_reason_text(app_fault_reason_t reason)
{
	if ((uint32_t)reason >= (sizeof(fault_reason_names) /
				 sizeof(fault_reason_names[0]))) {
		return "unknown";
	}
	return fault_reason_names[reason];
}

const char *app_fault_task(void)
{
	return g_app_fault.task;
}

const char *app_fault_file(void)
{
	return g_app_fault.file;
}

static void app_fault_save(app_fault_reason_t reason, uint32_t cfsr,
			   uint32_t hfsr, uint32_t mmfar, uint32_t bfar,
			   uint32_t pc, uint32_t lr, uint32_t psr,
			   const char *task, const char *file, uint32_t line)
{
	/* Invalidate first so a reset in the middle of the copy is not
	 * mistaken for a complete record. */
	g_app_fault.magic = 0U;
	__DMB();

	g_app_fault.reason = (uint32_t)reason;
	g_app_fault.cfsr = cfsr;
	g_app_fault.hfsr = hfsr;
	g_app_fault.mmfar = mmfar;
	g_app_fault.bfar = bfar;
	g_app_fault.pc = pc;
	g_app_fault.lr = lr;
	g_app_fault.psr = psr;
	g_app_fault.line = line;
	g_app_fault.boot_id = app_boot_id;
	fault_copy(g_app_fault.task, sizeof(g_app_fault.task), task);
	fault_copy(g_app_fault.file, sizeof(g_app_fault.file), file);

	__DMB();
	g_app_fault.magic = APP_FAULT_MAGIC;
	__DSB();
}

void app_fault_clear(void)
{
	g_app_fault.magic = 0U;
	__DMB();
}

static void fault_led3(bool on)
{
	FAULT_LED_PORT->BSRR = on ? (1UL << FAULT_LED_PIN)
				  : ((1UL << FAULT_LED_PIN) << 16U);
}

static void fault_show_and_reset(void)
{
	__disable_irq();

	/* Register-level LED setup: DGEN-004 must work from any context. */
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
	__DSB();
	uint32_t moder = FAULT_LED_PORT->MODER;

	moder &= ~(3UL << (FAULT_LED_PIN * 2U));
	moder |= (1UL << (FAULT_LED_PIN * 2U));
	FAULT_LED_PORT->MODER = moder;
	__DSB();

	/* Three short flashes, a pause, repeat; roughly 2 s. */
	for (uint32_t cycle = 0U; cycle < 2U; cycle++) {
		for (uint32_t flash = 0U; flash < 3U; flash++) {
			fault_led3(true);
			k_busy_wait(100U * USEC_PER_MSEC);
			fault_led3(false);
			k_busy_wait(100U * USEC_PER_MSEC);
		}
		k_busy_wait(500U * USEC_PER_MSEC);
	}

	sys_reboot(SYS_REBOOT_COLD);
	for (;;) {
		__NOP();
	}
}

static app_fault_reason_t fatal_reason(unsigned int reason)
{
	/* Zephyr 4.x reports Cortex-M exceptions with arch-specific reasons
	 * (K_ERR_ARM_MEM_*, K_ERR_ARM_BUS_*, K_ERR_ARM_USAGE_*), not only
	 * K_ERR_CPU_EXCEPTION.  Without this mapping every MemManage,
	 * BusFault and UsageFault was saved as "startup" with CFSR=0. */
	if (reason >= K_ERR_ARM_MEM_GENERIC &&
	    reason <= K_ERR_ARM_MEM_FP_LAZY_STATE_PRESERVATION) {
		return APP_FAULT_MEMMANAGE;
	}
	if (reason >= K_ERR_ARM_BUS_GENERIC &&
	    reason <= K_ERR_ARM_BUS_FP_LAZY_STATE_PRESERVATION) {
		return APP_FAULT_BUSFAULT;
	}
	if (reason >= K_ERR_ARM_USAGE_GENERIC &&
	    reason <= K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION) {
		return reason == K_ERR_ARM_USAGE_STACK_OVERFLOW
			       ? APP_FAULT_STACK_OVERFLOW
			       : APP_FAULT_USAGEFAULT;
	}
	if (reason >= K_ERR_ARM_SECURE_GENERIC &&
	    reason <= K_ERR_ARM_SECURE_LAZY_STATE_ERROR) {
		return APP_FAULT_MEMMANAGE;
	}

	if (reason == K_ERR_CPU_EXCEPTION) {
		uint32_t cfsr = SCB->CFSR;

		if ((cfsr & SCB_CFSR_MEMFAULTSR_Msk) != 0U) {
			return APP_FAULT_MEMMANAGE;
		}
		if ((cfsr & SCB_CFSR_BUSFAULTSR_Msk) != 0U) {
			return APP_FAULT_BUSFAULT;
		}
		if ((cfsr & SCB_CFSR_USGFAULTSR_Msk) != 0U) {
			return APP_FAULT_USAGEFAULT;
		}
		return APP_FAULT_HARDFAULT;
	}

	switch (reason) {
	case K_ERR_STACK_CHK_FAIL:
		return APP_FAULT_STACK_OVERFLOW;
	case K_ERR_KERNEL_OOPS:
	case K_ERR_KERNEL_PANIC:
		return APP_FAULT_ASSERT;
	case K_ERR_SPURIOUS_IRQ:
	default:
		return APP_FAULT_STARTUP;
	}
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	const char *task = "kernel";
	uint32_t pc = 0U;
	uint32_t lr = 0U;
	uint32_t psr = 0U;

	if (k_is_in_isr() == false) {
		const char *name = k_thread_name_get(k_current_get());

		if (name != NULL && name[0] != '\0') {
			task = name;
		}
	}
	if (esf != NULL) {
		pc = esf->basic.pc;
		lr = esf->basic.lr;
		psr = esf->basic.xpsr;
	}

	app_fault_save(fatal_reason(reason), SCB->CFSR, SCB->HFSR,
		       SCB->MMFAR, SCB->BFAR, pc, lr, psr, task, "zephyr", 0U);
	fault_show_and_reset();
}
