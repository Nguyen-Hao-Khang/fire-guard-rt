/*
 * Task watchdog wrappers and fault manager
 *
 * Detection paths:
 *  - hang: a sensor thread misses its task-watchdog deadline (500 ms).
 *    The task_wdt callback runs in timer ISR context; it only deletes the
 *    channel and posts the thread id to the fault manager.
 *  - implausible data: reported by the sensor thread itself.
 * Reaction: isolate (fault bit -> fusion degraded mode), maintenance alert
 * (chan_fault), restart the thread up to CONFIG_FG_FAULT_RESTART_MAX times.
 * Core threads (fusion, actuators, fault manager) have no callback: expiry
 * reboots the system, backed by the hardware watchdog.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "safety/fault_mgr.h"

LOG_MODULE_REGISTER(fg_fault, LOG_LEVEL_INF);

#define RESTART_SETTLE_MS 2000
#define STABLE_RESET_MS   60000

static int wdt_ch[FG_TH_COUNT] = {[0 ... FG_TH_COUNT - 1] = -1};
static atomic_t hang_req;
static atomic_t mask_hang;
static atomic_t mask_impl;

K_MSGQ_DEFINE(hang_q, sizeof(uint8_t), 8, 1);

static struct {
	uint8_t retries;
	uint32_t settle_at;   /* clear the hang fault after this time if quiet, 0 = none */
	uint32_t last_hang;
} th_state[FG_TH_COUNT];

int fg_wdt_init(void)
{
	const struct device *hw = NULL;

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(watchdog0))
	hw = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	if (!device_is_ready(hw)) {
		LOG_WRN("HW watchdog not ready, task_wdt without fallback");
		hw = NULL;
	}
#endif
	return task_wdt_init(hw);
}

static void wdt_expired(int channel_id, void *user_data)
{
	uint8_t th = (uint8_t)(uintptr_t)user_data;

	/* Stop this channel from firing again; the fault thread takes over */
	task_wdt_delete(channel_id);
	(void)k_msgq_put(&hang_q, &th, K_NO_WAIT);
}

int fg_wdt_register(enum fg_thread_id th, uint32_t timeout_ms, bool reset_on_expiry)
{
	if (wdt_ch[th] >= 0) {
		task_wdt_delete(wdt_ch[th]);
	}
	wdt_ch[th] = task_wdt_add(timeout_ms, reset_on_expiry ? NULL : wdt_expired,
				  (void *)(uintptr_t)th);
	if (wdt_ch[th] < 0) {
		LOG_ERR("task_wdt_add(%s) failed: %d", fg_thread_name(th), wdt_ch[th]);
	}
	return wdt_ch[th];
}

void fg_wdt_feed(enum fg_thread_id th)
{
	if (wdt_ch[th] >= 0) {
		task_wdt_feed(wdt_ch[th]);
	}
}

void fg_hang_inject(enum fg_thread_id th)
{
	atomic_set_bit(&hang_req, th);
}

void fg_hang_point(enum fg_thread_id th)
{
	if (atomic_test_and_clear_bit(&hang_req, th)) {
		LOG_WRN("[inject] %s hangs now", fg_thread_name(th));
		k_sleep(K_FOREVER);
	}
}

void fg_sleep_feed(enum fg_thread_id th, uint32_t ms)
{
	while (ms > 0) {
		uint32_t slice = MIN(ms, 250U);

		fg_wdt_feed(th);
		fg_hang_point(th);
		k_msleep(slice);
		ms -= slice;
	}
	fg_wdt_feed(th);
}

uint8_t fg_fault_mask(void)
{
	return (uint8_t)(atomic_get(&mask_hang) | atomic_get(&mask_impl));
}

void fg_fault_report(enum fg_sensor s, enum fg_fault_kind kind, bool active)
{
	atomic_t *m = (kind == FG_FAULT_HANG) ? &mask_hang : &mask_impl;
	bool was = atomic_test_bit(m, s);

	if (was == active) {
		return;
	}
	if (active) {
		atomic_set_bit(m, s);
	} else {
		atomic_clear_bit(m, s);
	}

	struct fg_fault_msg msg = {
		.ts_ms = fg_now_ms(), .sensor = s, .kind = kind, .active = active,
	};

	LOG_WRN("sensor %s %s %s", fg_sensor_name(s),
		kind == FG_FAULT_HANG ? "HANG" : "IMPLAUSIBLE", active ? "FAULT" : "recovered");
	zbus_chan_pub(&chan_fault, &msg, K_MSEC(50));
}

static void report_thread_sensors(enum fg_thread_id th, bool active)
{
	for (int s = 0; s < FG_SENSOR_COUNT; s++) {
		if (fg_thread_of_sensor(s) == (int)th) {
			fg_fault_report(s, FG_FAULT_HANG, active);
		}
	}
}

static void handle_hang(enum fg_thread_id th, uint32_t now)
{
	LOG_ERR("task watchdog: %s missed its deadline", fg_thread_name(th));
	wdt_ch[th] = -1; /* already deleted in the callback */
	report_thread_sensors(th, true);
	th_state[th].last_hang = now;

	if (th_state[th].retries >= CONFIG_FG_FAULT_RESTART_MAX) {
		LOG_ERR("%s: restart budget exhausted, sensor stays isolated",
			fg_thread_name(th));
		th_state[th].settle_at = 0;
		return;
	}
	th_state[th].retries++;
	LOG_WRN("restarting %s (%u/%d)", fg_thread_name(th), th_state[th].retries,
		CONFIG_FG_FAULT_RESTART_MAX);
	fg_thread_restart(th);
	th_state[th].settle_at = now + RESTART_SETTLE_MS;
}

void fg_fault_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	fg_wdt_register(FG_TH_FAULT, CONFIG_FG_CORE_WDT_MS, true);

	while (1) {
		uint8_t th;
		uint32_t now;

		if (k_msgq_get(&hang_q, &th, K_MSEC(250)) == 0 && th < FG_TH_COUNT) {
			handle_hang(th, fg_now_ms());
		}
		now = fg_now_ms();

		for (int i = 0; i < FG_TH_COUNT; i++) {
			if (th_state[i].settle_at && (int32_t)(now - th_state[i].settle_at) >= 0) {
				th_state[i].settle_at = 0;
				report_thread_sensors(i, false);
			}
			if (th_state[i].retries && now - th_state[i].last_hang > STABLE_RESET_MS) {
				th_state[i].retries = 0;
			}
		}
		/* Also re-arms the HW watchdog after a task_wdt callback */
		fg_wdt_feed(FG_TH_FAULT);
	}
}
