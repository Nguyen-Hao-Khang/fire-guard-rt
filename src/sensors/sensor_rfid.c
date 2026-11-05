/*
 * RC522 commander-card reader
 *
 * Polls every 200 ms, publishes chan_rfid only on a card *arrival* edge.
 * Reader not answering VersionReg (checked every 5 s) -> implausible fault.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "drivers/rc522.h"
#include "safety/fault_mgr.h"
#include "sensors/sensors.h"

LOG_MODULE_REGISTER(fg_rfid, LOG_LEVEL_INF);

#define POLL_MS          200
#define HEALTH_CHECK_MS  5000
#define MAX_COMMANDERS   8

static uint8_t commanders[MAX_COMMANDERS][4];
static int n_commanders;

static void parse_commanders(void)
{
	const char *p = CONFIG_FG_COMMANDER_UIDS;

	while (*p && n_commanders < MAX_COMMANDERS) {
		char hex[9] = {0};
		int len = 0;

		while (*p == ',' || *p == ' ') {
			p++;
		}
		while (*p && *p != ',' && len < 8) {
			hex[len++] = *p++;
		}
		if (len == 8) {
			uint32_t v = strtoul(hex, NULL, 16);

			commanders[n_commanders][0] = v >> 24;
			commanders[n_commanders][1] = v >> 16;
			commanders[n_commanders][2] = v >> 8;
			commanders[n_commanders][3] = v;
			n_commanders++;
		}
		while (*p && *p != ',') {
			p++;
		}
	}
}

bool fg_rfid_is_commander(const uint8_t *uid, uint8_t len)
{
	if (len != 4) {
		return false;
	}
	for (int i = 0; i < n_commanders; i++) {
		if (memcmp(commanders[i], uid, 4) == 0) {
			return true;
		}
	}
	return false;
}

/* Inject a card (shell / tests). Same path as a real card */
void fg_rfid_inject(const uint8_t *uid, uint8_t len)
{
	struct fg_rfid_msg msg = {.ts_ms = fg_now_ms(), .uid_len = MIN(len, RC522_UID_MAX)};

	memcpy(msg.uid, uid, msg.uid_len);
	msg.authorized = fg_rfid_is_commander(uid, len);
	zbus_chan_pub(&chan_rfid, &msg, K_MSEC(50));
}

void fg_rfid_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	bool present = false;
	uint8_t misses = 0;
	bool reader_ok;
	uint32_t last_check = fg_now_ms();

	if (n_commanders == 0) {
		parse_commanders();
	}
	fg_wdt_register(FG_TH_RFID, CONFIG_FG_SENSOR_WDT_MS, false);

	reader_ok = rc522_init() == 0;
	fg_fault_report(FG_SENSOR_RFID, FG_FAULT_IMPLAUSIBLE, !reader_ok);

	while (1) {
		uint8_t uid[RC522_UID_MAX];
		uint32_t now = fg_now_ms();

		if (now - last_check >= HEALTH_CHECK_MS) {
			int v = rc522_version();

			last_check = now;
			if (!reader_ok && v > 0 && v != 0xFF) {
				reader_ok = rc522_init() == 0;
			} else if (reader_ok && (v <= 0 || v == 0xFF)) {
				reader_ok = false;
			}
			fg_fault_report(FG_SENSOR_RFID, FG_FAULT_IMPLAUSIBLE, !reader_ok);
		}

		if (reader_ok) {
			int n = rc522_read_uid(uid);

			if (n > 0) {
				misses = 0;
				if (!present) {
					present = true;
					LOG_INF("card %02X%02X%02X%02X", uid[0], uid[1], uid[2],
						uid[3]);
					fg_rfid_inject(uid, n);
				}
			} else if (n == 0 && ++misses >= 3) {
				/* a card that stays in the field may skip some REQA rounds */
				present = false;
			}
		}
		fg_sleep_feed(FG_TH_RFID, POLL_MS);
	}
}
