/*
 * HC-SR04 "person still in the danger zone" detection
 *
 * Active only while the system is in ALARM/DRILL. Movement = distance change
 * larger than MOVE_MM between consecutive readings; MOVE_COUNT movements
 * within MOVE_WINDOW_MS set trapped=true, NO_MOVE_CLEAR_MS without movement
 * clears it. Five consecutive failed readings -> implausible-data fault.
 */
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "safety/fault_mgr.h"

LOG_MODULE_REGISTER(fg_ultra, LOG_LEVEL_INF);

#define SCAN_PERIOD_MS    200
#define MOVE_MM           150
#define MOVE_COUNT        3
#define MOVE_WINDOW_MS    3000
#define NO_MOVE_CLEAR_MS  10000
#define FAIL_LIMIT        5

void fg_ultra_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(fg_ultrasonic));
	int32_t prev_mm = -1;
	uint32_t moves[MOVE_COUNT] = {0};
	uint8_t mi = 0;
	uint32_t last_move = 0;
	bool trapped = false;
	int fails = 0;

	fg_wdt_register(FG_TH_ULTRA, CONFIG_FG_SENSOR_WDT_MS, false);

	while (1) {
		struct fg_state_msg st;
		uint32_t now = fg_now_ms();

		zbus_chan_read(&chan_state, &st, K_MSEC(10));
		bool active = fg_state_is_alarm(st.state) || st.state == FG_STATE_DRILL;

		if (!active) {
			if (trapped) {
				trapped = false;
			}
			prev_mm = -1;
			fails = 0;
			fg_sleep_feed(FG_TH_ULTRA, 250);
			continue;
		}

		struct sensor_value d;
		int32_t mm = -1;

		if (device_is_ready(dev) && sensor_sample_fetch(dev) == 0 &&
		    sensor_channel_get(dev, SENSOR_CHAN_DISTANCE, &d) == 0) {
			mm = d.val1 * 1000 + d.val2 / 1000;
			fails = 0;
		} else if (fails < FAIL_LIMIT) {
			fails++;
		}
		fg_fault_report(FG_SENSOR_ULTRASONIC, FG_FAULT_IMPLAUSIBLE, fails >= FAIL_LIMIT);

		if (mm >= 0) {
			if (prev_mm >= 0 && abs(mm - prev_mm) > MOVE_MM) {
				moves[mi] = now;
				mi = (mi + 1) % MOVE_COUNT;
				last_move = now;
			}
			prev_mm = mm;
		}

		bool all_recent = true;

		for (int i = 0; i < MOVE_COUNT; i++) {
			all_recent &= moves[i] != 0 && (now - moves[i]) <= MOVE_WINDOW_MS;
		}

		bool new_trapped = trapped;

		if (all_recent) {
			new_trapped = true;
		} else if (trapped && (now - last_move) > NO_MOVE_CLEAR_MS) {
			new_trapped = false;
		}

		if (new_trapped != trapped) {
			struct fg_presence_msg msg = {
				.ts_ms = now, .distance_mm = mm, .trapped = new_trapped,
			};

			trapped = new_trapped;
			LOG_WRN("person in danger zone: %s", trapped ? "YES" : "no");
			zbus_chan_pub(&chan_presence, &msg, K_MSEC(50));
		}
		fg_sleep_feed(FG_TH_ULTRA, SCAN_PERIOD_MS);
	}
}
