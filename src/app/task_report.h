/*
 * task_report.h - once-a-second "task <n> <name>: <purpose>" debug line
 *
 * Each of the four application threads calls TASK_REPORT() once per loop
 * iteration.  The macro returns at once unless a second has passed since that
 * thread last printed, so a thread that wakes every 10 ms still prints exactly
 * one line a second:
 *
 *     [  12034] C-ZEP I task 2 Button: debounces B1, short press cycles the LED pattern
 *
 * The including file must have LOG_MODULE_DECLARE(app).
 */
#ifndef TASK_REPORT_H
#define TASK_REPORT_H

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Per-thread state; zero-initialise it. */
struct task_report {
	uint32_t last_ms;
	bool primed;
};

#define TASK_REPORT(state, number, name, purpose)                              \
	do {                                                                   \
		uint32_t t_now_ = k_uptime_get_32();                           \
		if (!(state).primed || (t_now_ - (state).last_ms) >= 1000U) {  \
			(state).primed = true;                                 \
			(state).last_ms = t_now_;                              \
			LOG_INF("task %u %s: %s", (unsigned)(number), (name),  \
				(purpose));                                    \
		}                                                              \
	} while (0)

#endif /* TASK_REPORT_H */
