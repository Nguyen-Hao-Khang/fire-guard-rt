/*
 * Non-volatile storage for configuration and the event log
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include "app/config.h"
#include "app/storage.h"
#include "safety/event_log.h"

LOG_MODULE_REGISTER(fg_storage, LOG_LEVEL_INF);

#define ROOT "fg"

static uint16_t boot_count;

uint16_t fg_storage_boot_count(void)
{
	return boot_count;
}

static int handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "cfg", &next) && !next) {
		struct fg_config c;

		if (len != sizeof(c) || read_cb(cb_arg, &c, sizeof(c)) != sizeof(c)) {
			return -EINVAL;
		}
		fg_config_restore(&c);
		return 0;
	}
	if (settings_name_steq(name, "boot", &next) && !next) {
		if (len != sizeof(boot_count) ||
		    read_cb(cb_arg, &boot_count, sizeof(boot_count)) != sizeof(boot_count)) {
			return -EINVAL;
		}
		return 0;
	}
	if (settings_name_steq(name, "evh", &next) && !next) {
		struct fg_event_log_head h;

		if (len != sizeof(h) || read_cb(cb_arg, &h, sizeof(h)) != sizeof(h)) {
			return -EINVAL;
		}
		fg_event_log_restore_head(&h);
		return 0;
	}
	if (settings_name_steq(name, "ev", &next) && next) {
		struct fg_event e;

		/* entries written by an older firmware layout are skipped */
		if (len != sizeof(e) || read_cb(cb_arg, &e, sizeof(e)) != sizeof(e)) {
			return 0;
		}
		fg_event_log_restore_entry(&e);
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(fg, ROOT, NULL, handle_set, NULL, NULL);

int fg_storage_save(const char *key, const void *data, size_t len)
{
	char path[24];

	snprintf(path, sizeof(path), ROOT "/%s", key);
	return settings_save_one(path, data, len);
}

int fg_storage_delete(const char *key)
{
	char path[24];

	snprintf(path, sizeof(path), ROOT "/%s", key);
	return settings_delete(path);
}

int fg_storage_init(void)
{
	int err = settings_subsys_init();

	if (err) {
		LOG_ERR("settings init failed: %d", err);
		return err;
	}
	err = settings_load_subtree(ROOT);
	if (err) {
		LOG_WRN("settings load: %d", err);
	}

	boot_count++;
	fg_storage_save("boot", &boot_count, sizeof(boot_count));
	LOG_INF("storage ready, boot #%u, %d events restored", boot_count,
		fg_event_log_count());
	return 0;
}
