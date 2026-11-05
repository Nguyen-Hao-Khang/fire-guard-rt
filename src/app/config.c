/*
 * Runtime configuration
 *
 * TODO(persist): store accepted values in NVS through the settings subsystem.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/config.h"

LOG_MODULE_REGISTER(fg_config, LOG_LEVEL_INF);

bool fg_config_validate(const void *msg, size_t msg_size)
{
	const struct fg_config *c = msg;

	ARG_UNUSED(msg_size);

	return c->sample_period_ms >= FG_SAMPLE_MIN_MS &&
	       c->sample_period_ms <= FG_SAMPLE_MAX_MS &&
	       c->callin_period_s >= FG_CALLIN_MIN_S &&
	       c->callin_period_s <= FG_CALLIN_MAX_S &&
	       c->th_light_delta_mv > 0 && c->th_gas_rise_mv > 0 &&
	       c->th_gas_warn_mv > 0 && c->th_gas_danger_mv > c->th_gas_warn_mv &&
	       c->th_temp_max_x10 > 0 && c->th_temp_rise_x10 > 0 &&
	       c->confirm_window_ms >= 1000 && c->confirm_window_ms <= 4000;
}

void fg_config_get(struct fg_config *out)
{
	zbus_chan_read(&chan_config, out, K_FOREVER);
}

struct cfg_field {
	const char *key;
	size_t offset;
	bool is_u16;
};

#define F_U16(k) {#k, offsetof(struct fg_config, k), true}
#define F_I16(k) {#k, offsetof(struct fg_config, k), false}

static const struct cfg_field fields[] = {
	F_U16(sample_period_ms), F_U16(callin_period_s),  F_I16(th_light_delta_mv),
	F_I16(th_gas_rise_mv),   F_I16(th_gas_warn_mv),   F_I16(th_gas_danger_mv),
	F_I16(th_temp_max_x10),  F_I16(th_temp_rise_x10), F_U16(confirm_window_ms),
};

int fg_config_set(const char *key, int32_t value)
{
	struct fg_config c;

	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		if (strcmp(fields[i].key, key) != 0) {
			continue;
		}
		if (value < INT16_MIN || value > UINT16_MAX) {
			return -EINVAL;
		}
		fg_config_get(&c);
		if (fields[i].is_u16) {
			*(uint16_t *)((uint8_t *)&c + fields[i].offset) = (uint16_t)value;
		} else {
			*(int16_t *)((uint8_t *)&c + fields[i].offset) = (int16_t)value;
		}
		/* zbus runs fg_config_validate(); -ENOMSG on rejection */
		int err = zbus_chan_pub(&chan_config, &c, K_MSEC(100));

		if (err) {
			LOG_WRN("config %s=%d rejected (%d)", key, value, err);
			return -EINVAL;
		}
		LOG_INF("config %s=%d", key, value);
		return 0;
	}
	return -ENOENT;
}
