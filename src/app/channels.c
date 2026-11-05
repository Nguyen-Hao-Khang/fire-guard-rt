/*
 * Zbus channel definitions
 *
 * Observers attach themselves in their own module with ZBUS_CHAN_ADD_OBS(),
 * so this file is the single place that lists every channel and its type.
 */
#include <zephyr/kernel.h>
#include "app/channels.h"
#include "app/config.h"

ZBUS_CHAN_DEFINE(chan_adc, struct fg_adc_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(chan_dht, struct fg_dht_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(chan_presence, struct fg_presence_msg, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(chan_rfid, struct fg_rfid_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(chan_state, struct fg_state_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = FG_STATE_INIT));

ZBUS_CHAN_DEFINE(chan_fault, struct fg_fault_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(chan_config, struct fg_config, fg_config_validate, NULL,
		 ZBUS_OBSERVERS_EMPTY, FG_CONFIG_DEFAULTS);

ZBUS_CHAN_DEFINE(chan_cmd, struct fg_cmd_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

static const char *const state_names[FG_STATE_COUNT] = {
	"INIT", "WARMUP", "NORMAL", "PRE_ALARM", "ALARM_FIRE", "ALARM_GAS", "DRILL",
};

static const char *const sensor_names[FG_SENSOR_COUNT] = {
	"light", "gas", "dht22", "ultrasonic", "rfid",
};

static const char *const cause_names[] = {
	[FG_CAUSE_NONE] = "none",
	[FG_CAUSE_BOOT] = "boot",
	[FG_CAUSE_WARMUP_DONE] = "warmup_done",
	[FG_CAUSE_LIGHT] = "light_surge",
	[FG_CAUSE_GAS] = "gas_rise",
	[FG_CAUSE_HEAT] = "heat_rise",
	[FG_CAUSE_FIRE_CONFIRMED] = "fire_confirmed",
	[FG_CAUSE_GAS_CONFIRMED] = "gas_confirmed",
	[FG_CAUSE_GAS_DANGER] = "gas_danger",
	[FG_CAUSE_HEAT_ABSOLUTE] = "heat_absolute",
	[FG_CAUSE_NOT_CONFIRMED] = "not_confirmed",
	[FG_CAUSE_OVERRIDE] = "commander_override",
	[FG_CAUSE_DRILL_START] = "drill_start",
	[FG_CAUSE_DRILL_END] = "drill_end",
	[FG_CAUSE_RESTORED] = "restored_after_reset",
};

const char *fg_state_name(enum fg_state s)
{
	return (s < FG_STATE_COUNT) ? state_names[s] : "?";
}

const char *fg_sensor_name(enum fg_sensor s)
{
	return (s < FG_SENSOR_COUNT) ? sensor_names[s] : "?";
}

const char *fg_cause_name(enum fg_cause c)
{
	return (c < ARRAY_SIZE(cause_names) && cause_names[c]) ? cause_names[c] : "?";
}
