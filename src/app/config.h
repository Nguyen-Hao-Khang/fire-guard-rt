/*
 * Runtime configuration (validated, published on chan_config)
 */
#ifndef FG_CONFIG_H_
#define FG_CONFIG_H_

#include <zephyr/zbus/zbus.h>
#include "app/channels.h"

/* Bounds, see SRS FR-01, FR-07, FR-08 */
#define FG_SAMPLE_MIN_MS   1000
#define FG_SAMPLE_MAX_MS   2000
#define FG_CALLIN_MIN_S    10
#define FG_CALLIN_MAX_S    600
#define FG_DHT_MIN_PERIOD_MS 2000

#define FG_CONFIG_DEFAULTS                                                     \
	ZBUS_MSG_INIT(.sample_period_ms = CONFIG_FG_SAMPLE_PERIOD_MS,          \
		      .callin_period_s = CONFIG_FG_CALLIN_PERIOD_S,            \
		      .th_light_delta_mv = 800, .th_gas_rise_mv = 400,         \
		      .th_gas_warn_mv = 1500, .th_gas_danger_mv = 2500,        \
		      .th_temp_max_x10 = 570, .th_temp_rise_x10 = 30,          \
		      .confirm_window_ms = CONFIG_FG_CONFIRM_WINDOW_MS)

/* zbus validator: rejects out-of-range configurations */
bool fg_config_validate(const void *msg, size_t msg_size);

/* Snapshot of the current configuration */
void fg_config_get(struct fg_config *out);

/*
 * Update one parameter by name (used by shell and CoreIoT attributes).
 * Returns 0 on success, -ENOENT unknown key, -EINVAL out of range.
 */
int fg_config_set(const char *key, int32_t value);

/* Apply a configuration loaded from storage (validated, not saved again) */
void fg_config_restore(const struct fg_config *c);

/* Restore the built-in defaults and erase the stored configuration */
int fg_config_reset(void);

#endif /* FG_CONFIG_H_ */
