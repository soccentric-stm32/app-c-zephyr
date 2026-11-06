/*
 * health.c - task supervision, health check, watchdog and CONFIRMED record
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NUCLEO-F767ZI
 * SRS:     ZRT-004, ZFN-004 (HLT-001..HLT-005), ZGEN-004
 *
 * Health is the only thread that feeds the watchdog.  Every other thread
 * posts its bit in a k_event at least once per second.  If any bit is missing
 * for 2 s the watchdog is no longer fed and BOOT counts a failed trial.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app/zdemo.h"
#include "app/zdemo_threads.h"
#include "board/led.h"
#include "boot/flash_confirm.h"
#include "boot/handover.h"
#include "comms/comms.h"
#include "system/app_fault.h"
#include "system/health.h"
#include "system/iwdg.h"

LOG_MODULE_DECLARE(app);

/* Maximum time between comms loop iterations while waiting for events. */
#define HEALTH_LOOP_MS            20U
/* A task is considered dead if no check-in within this window. */
#define HEALTH_CHECKIN_TIMEOUT_MS 2000U
/* Time after boot at which the health check runs during a trial. */
#define HEALTH_CHECK_AT_MS        5000U
/* Time after boot after which a stuck check gives up and fails. */
#define HEALTH_STOP_KICK_MS       10000U
/* Maximum interval at which the watchdog is kicked. */
#define HEALTH_REFRESH_MS         100U
/* Minimum number of check-ins required before the check can pass. */
#define HEALTH_MIN_CHECKINS       5U

/** Event bit for the LED thread's regular check-in. */
#define HEALTH_EVT_LED    BIT(HEALTH_TASK_LED)
/** Event bit for the button thread's regular check-in. */
#define HEALTH_EVT_BUTTON BIT(HEALTH_TASK_BUTTON)
/** Event bit for the comms thread's regular check-in. */
#define HEALTH_EVT_COMMS  BIT(HEALTH_TASK_COMMS)
/** Bit mask of all supervised external tasks. */
#define HEALTH_EVT_MASK   (HEALTH_EVT_LED | HEALTH_EVT_BUTTON | HEALTH_EVT_COMMS)

/** Event object used to collect check-in bits from supervised threads. */
K_EVENT_DEFINE(health_events);

/** Per-task check-in counter (index HEALTH_TASK_COUNT = health itself). */
static uint32_t checkin_count[HEALTH_TASK_COUNT + 1U];
/** Per-task last check-in uptime (index HEALTH_TASK_COUNT = health itself). */
static uint32_t last_seen[HEALTH_TASK_COUNT + 1U];
/** Uptime at the start of the health thread. */
static uint32_t start_time;
/** Uptime of the last watchdog kick. */
static uint32_t last_kick;
/** Current health state (volatile; shared with readers). */
static volatile uint8_t health_state = APP_HEALTH_NOT_REQUIRED;
/** true once the one-shot health check has produced a verdict. */
static bool check_done;
/** true once a supervised task went silent. */
static bool supervision_lost;

/**
 * @brief Posted by supervised threads at least once per second.
 *
 * Safe from any task context (it only posts to an event).
 *
 * @param task Identity of the checking thread.
 */
void health_checkin(health_task_id_t task)
{
	if ((uint32_t)task < HEALTH_TASK_COUNT) {
		k_event_post(&health_events, BIT(task));
	}
}

/**
 * @brief Return the current health state code.
 *
 * @return One of the APP_HEALTH_* codes.
 */
uint8_t health_state_get(void)
{
	return health_state;
}

/**
 * @brief Check whether the app has failed its boot health check.
 *
 * @return true if the health verdict is FAILED.
 */
bool health_is_failed(void)
{
	return health_state == APP_HEALTH_FAILED;
}

/**
 * @brief Record a check-in tick for all pending tasks.
 *
 * Health's own liveness is counted implicitly on every call.
 *
 * @param bits Bit mask of tasks that have checked in since the last tick.
 * @param now  Current uptime in ms.
 */
static void checkin_account(uint32_t bits, uint32_t now)
{
	for (uint32_t i = 0U; i < HEALTH_TASK_COUNT; i++) {
		if ((bits & BIT(i)) != 0U) {
			checkin_count[i]++;
			last_seen[i] = now;
		}
	}

	/* Health itself is alive whenever this function runs. */
	checkin_count[HEALTH_TASK_COUNT]++;
	last_seen[HEALTH_TASK_COUNT] = now;
}

/**
 * @brief Flag supervision lost if any supervised task went silent.
 *
 * Once latched, the watchdog is no longer kicked and BOOT counts a
 * failed trial on the next reset.
 *
 * @param now Current uptime in ms.
 */
static void supervise(uint32_t now)
{
	if (supervision_lost) {
		return;
	}
	for (uint32_t i = 0U; i < (HEALTH_TASK_COUNT + 1U); i++) {
		if ((now - last_seen[i]) > HEALTH_CHECKIN_TIMEOUT_MS) {
			supervision_lost = true;
			return;
		}
	}
}

/**
 * @brief Determine whether the health check conditions are satisfied.
 *
 * @return true if every task has checked in enough times and no
 *         relevant fault is recorded for this trial.
 */
static bool health_conditions_met(void)
{
#ifdef CONFIG_ZDEMO_FAIL_HEALTH
	return false;
#else
	for (uint32_t i = 0U; i < (HEALTH_TASK_COUNT + 1U); i++) {
		if (checkin_count[i] < HEALTH_MIN_CHECKINS) {
			return false;
		}
	}
	if (app_fault_is_current_boot()) {
		return false;
	}
	/* A fault in the previous trial boot is a failed trial; an older
	 * fault from before the update is only reported in Status. */
	if (g_handover.trial_count > 1U && app_fault_is_previous_boot()) {
		return false;
	}

	/* No heap: there is nothing to monitor here (see main.c). */
	return comms_rx_running();
#endif
}

/**
 * @brief Record a FAILED health verdict and signal the LED thread.
 */
static void health_check_fail(void)
{
	check_done = true;
	health_state = APP_HEALTH_FAILED;
	(void)led_post_health_failed();
}

/**
 * @brief Record a PASSED health verdict.
 */
static void health_check_pass(void)
{
	check_done = true;
	health_state = APP_HEALTH_PASSED;
}

/**
 * @brief Main health loop.
 *
 * Supervises the other tasks, feeds the watchdog at a bounded rate, and
 * runs the one-shot boot health check (confirm the running slot, or fail
 * and stop kicking the watchdog) during trial boots.
 *
 * @param p1 Unused.
 * @param p2 Unused.
 * @param p3 Unused.
 */
static void health_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)k_thread_name_set(k_current_get(), "Health");

	uint32_t now = k_uptime_get_32();

	start_time = now;
	last_kick = now;
	for (uint32_t i = 0U; i < (HEALTH_TASK_COUNT + 1U); i++) {
		last_seen[i] = now;
		checkin_count[i] = 0U;
	}

	bool pending = g_handover.present && g_handover.trial == 2U;
	health_state = pending ? APP_HEALTH_RUNNING : APP_HEALTH_NOT_REQUIRED;

	for (;;) {
		uint32_t bits = k_event_wait(&health_events, HEALTH_EVT_MASK,
					     true, K_MSEC(HEALTH_LOOP_MS));

		now = k_uptime_get_32();
		checkin_account(bits, now);
		supervise(now);

		if (pending && !check_done &&
		    (now - start_time) >= HEALTH_CHECK_AT_MS) {
			if (health_conditions_met()) {
				if (app_flash_confirm_running_slot()) {
					/* The CONFIRMED record is in flash, so the
					 * slot is no longer on trial.  g_handover was
					 * decoded once at start-up; update it so
					 * Status reports CONFIRMED.  A single byte
					 * store, so Comms never sees a torn value. */
					g_handover.trial = 1U;
					health_check_pass();
					LOG_INF("health: passed, CONFIRMED written");
				} else {
					health_check_fail();
					LOG_ERR("health: CONFIRMED record write failed");
				}
			} else if ((now - start_time) >= HEALTH_STOP_KICK_MS) {
				health_check_fail();
				LOG_ERR("health: conditions not met, trial fails");
			}
		}

		bool kick = !supervision_lost;

		if (health_state == APP_HEALTH_FAILED &&
		    (now - start_time) >= HEALTH_STOP_KICK_MS) {
			kick = false;
		}

		if (kick && (now - last_kick) >= HEALTH_REFRESH_MS) {
			app_wdt_feed();
			last_kick = now;
		}
	}
}

/* Health/supervision thread: watchdog feeder + trial check verifier. */
K_THREAD_DEFINE(zdemo_health_thread, ZDEMO_HEALTH_STACK, health_thread, NULL,
		NULL, NULL, ZDEMO_PRIO_HEALTH, 0, 0);
