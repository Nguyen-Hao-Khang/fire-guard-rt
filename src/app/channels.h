/*
 * Zbus data channels (system data dictionary)
 *
 * Safety core (fusion, sensors, actuators) and comm core only exchange
 * data through these channels. In the AMP stage a bridge forwards the
 * "exported" channels over IPM, so producers/consumers do not change.
 */
#ifndef FG_CHANNELS_H_
#define FG_CHANNELS_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/zbus/zbus.h>

/* Sensor identifiers (also task-watchdog / fault index) */
enum fg_sensor {
	FG_SENSOR_LIGHT = 0,
	FG_SENSOR_GAS,
	FG_SENSOR_DHT,
	FG_SENSOR_ULTRASONIC,
	FG_SENSOR_RFID,
	FG_SENSOR_COUNT,
};

#define FG_FAULT_BIT(s) (1U << (s))

/* System state */
enum fg_state {
	FG_STATE_INIT = 0,
	FG_STATE_WARMUP,
	FG_STATE_NORMAL,
	FG_STATE_PRE_ALARM,
	FG_STATE_ALARM_FIRE,
	FG_STATE_ALARM_GAS,
	FG_STATE_DRILL,
	FG_STATE_COUNT,
};

static inline bool fg_state_is_alarm(enum fg_state s)
{
	return s == FG_STATE_ALARM_FIRE || s == FG_STATE_ALARM_GAS;
}

/* chan_adc: LDR + MQ-2, published every sample period */
struct fg_adc_msg {
	uint32_t ts_ms;
	int32_t light_mv;  /* higher = brighter (after optional inversion) */
	int32_t gas_mv;
	bool light_ok;
	bool gas_ok;
};

/* chan_dht: DHT22, published every max(sample, 2 s) */
struct fg_dht_msg {
	uint32_t ts_ms;
	int16_t temp_x10;  /* 0.1 degC */
	int16_t hum_x10;   /* 0.1 %RH */
	bool ok;
};

/* chan_presence: HC-SR04 result while in ALARM */
struct fg_presence_msg {
	uint32_t ts_ms;
	int32_t distance_mm;
	bool trapped;
};

/* chan_rfid: a card was presented (edge, not level) */
struct fg_rfid_msg {
	uint32_t ts_ms;
	uint8_t uid[10];
	uint8_t uid_len;
	bool authorized;
};

/* chan_state: output of fusion, consumed by actuators/comm/log */
enum fg_cause {
	FG_CAUSE_NONE = 0,
	FG_CAUSE_BOOT,
	FG_CAUSE_WARMUP_DONE,
	FG_CAUSE_LIGHT,
	FG_CAUSE_GAS,
	FG_CAUSE_HEAT,
	FG_CAUSE_FIRE_CONFIRMED,
	FG_CAUSE_GAS_CONFIRMED,
	FG_CAUSE_GAS_DANGER,
	FG_CAUSE_HEAT_ABSOLUTE,
	FG_CAUSE_NOT_CONFIRMED,
	FG_CAUSE_OVERRIDE,
	FG_CAUSE_DRILL_START,
	FG_CAUSE_DRILL_END,
	FG_CAUSE_RESTORED,
};

struct fg_state_msg {
	uint32_t ts_ms;
	uint32_t decision_cyc;   /* k_cycle_get_32() at decision, for latency */
	uint8_t state;           /* enum fg_state */
	uint8_t prev_state;
	uint8_t cause;           /* enum fg_cause */
	uint8_t faults;          /* FG_FAULT_BIT mask */
	bool trapped;
};

/* chan_fault: sensor fault raised / cleared */
enum fg_fault_kind {
	FG_FAULT_HANG = 1,       /* task watchdog expired */
	FG_FAULT_IMPLAUSIBLE,    /* wire broken, bus error, saturation */
};

struct fg_fault_msg {
	uint32_t ts_ms;
	uint8_t sensor;          /* enum fg_sensor */
	uint8_t kind;            /* enum fg_fault_kind */
	bool active;
};

/* chan_config: runtime parameters (web / CoreIoT shared attrs) */
struct fg_config {
	uint16_t sample_period_ms;   /* 1000..2000 */
	uint16_t callin_period_s;    /* 10..600 */
	int16_t th_light_delta_mv;   /* light surge vs baseline */
	int16_t th_gas_rise_mv;      /* smoke: rise vs baseline */
	int16_t th_gas_warn_mv;      /* gas leak warning (absolute) */
	int16_t th_gas_danger_mv;    /* gas leak danger (absolute, direct ALARM) */
	int16_t th_temp_max_x10;     /* absolute heat, direct ALARM */
	int16_t th_temp_rise_x10;    /* rate of rise over 10 s */
	uint16_t confirm_window_ms;
};

/* chan_cmd: remote commands (drill) */
enum fg_cmd_kind {
	FG_CMD_DRILL_START = 1,
	FG_CMD_DRILL_STOP,
};

struct fg_cmd_msg {
	uint8_t kind;
	uint16_t duration_s;
};

/*
 * Timeout for reading a channel after a notification. The publisher may
 * still hold the channel lock, so K_NO_WAIT can fail and leave the
 * destination buffer stale.
 */
#define FG_CHAN_READ_TIMEOUT K_MSEC(10)

ZBUS_CHAN_DECLARE(chan_adc, chan_dht, chan_presence, chan_rfid, chan_state,
		  chan_fault, chan_config, chan_cmd);

const char *fg_state_name(enum fg_state s);
const char *fg_sensor_name(enum fg_sensor s);
const char *fg_cause_name(enum fg_cause c);

static inline uint32_t fg_now_ms(void)
{
	return k_uptime_get_32();
}

#endif /* FG_CHANNELS_H_ */
