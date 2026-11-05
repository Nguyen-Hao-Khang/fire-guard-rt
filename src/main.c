/*
 * Entry point
 *
 * Boot order: actuators to a safe state -> task watchdog (+HW fallback)
 * -> start the thread table. main() then returns; all work is in threads.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include "app/threads.h"
#include "actuators/actuators.h"
#include "safety/event_log.h"
#include "safety/fault_mgr.h"

LOG_MODULE_REGISTER(fg_main, LOG_LEVEL_INF);

#define FG_VERSION "0.1.0"

int main(void)
{
	uint32_t cause = 0;
	int err;

	err = fg_act_init();
	if (err) {
		LOG_ERR("actuator init failed: %d", err);
	}

	(void)hwinfo_get_reset_cause(&cause);
	(void)hwinfo_clear_reset_cause();
	LOG_INF("FireGuard-RT v%s, reset cause 0x%08x%s", FG_VERSION, cause,
		(cause & RESET_WATCHDOG) ? " (WATCHDOG)" : "");
	fg_event_log_add(FG_EV_BOOT, (cause & RESET_WATCHDOG) ? 1 : 0, 0, NULL);

	err = fg_wdt_init();
	if (err) {
		LOG_ERR("task_wdt init failed: %d", err);
	}

	fg_threads_start();
	return 0;
}
