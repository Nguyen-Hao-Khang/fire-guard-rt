/*
 * LDR (flame light) + MQ-2 (smoke/gas) sampling on ADC1
 *
 * Period: sample_period_ms from chan_config (1..2 s).
 * Plausibility: a reading stuck at the ADC rails for >= 1 s is reported as
 * an implausible-data fault (broken wire / short).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "app/config.h"
#include "safety/fault_mgr.h"
#include "sensors/sensors.h"

LOG_MODULE_REGISTER(fg_adc, LOG_LEVEL_INF);

#define ADC_MAX_RAW     4095
#define RAIL_MARGIN     8
#define OVERSAMPLE      4
#define RAIL_FAULT_MS   1000
#define FULL_SCALE_MV   3300

static const struct adc_dt_spec adc_gas = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), gas);
static const struct adc_dt_spec adc_light =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), light);

static int read_mv(const struct adc_dt_spec *spec, int32_t *mv, int32_t *raw_avg)
{
	int16_t buf;
	int32_t acc = 0;
	struct adc_sequence seq = {.buffer = &buf, .buffer_size = sizeof(buf)};
	int err = adc_sequence_init_dt(spec, &seq);

	if (err) {
		return err;
	}
	for (int i = 0; i < OVERSAMPLE; i++) {
		err = adc_read_dt(spec, &seq);
		if (err) {
			return err;
		}
		acc += buf;
	}
	*raw_avg = acc / OVERSAMPLE;
	*mv = *raw_avg;
	return adc_raw_to_millivolts_dt(spec, mv);
}

static atomic_t gas_override = ATOMIC_INIT(-1);

void fg_adc_override_gas(int32_t mv)
{
	atomic_set(&gas_override, mv);
}

static bool at_rail(int32_t raw)
{
	return raw <= RAIL_MARGIN || raw >= (ADC_MAX_RAW - RAIL_MARGIN);
}

void fg_adc_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t gas_rail_since = 0;
	bool hw_ok = adc_is_ready_dt(&adc_gas) && adc_is_ready_dt(&adc_light) &&
		     adc_channel_setup_dt(&adc_gas) == 0 &&
		     adc_channel_setup_dt(&adc_light) == 0;

	if (!hw_ok) {
		LOG_ERR("ADC setup failed");
		fg_fault_report(FG_SENSOR_LIGHT, FG_FAULT_IMPLAUSIBLE, true);
		fg_fault_report(FG_SENSOR_GAS, FG_FAULT_IMPLAUSIBLE, true);
	}

	fg_wdt_register(FG_TH_ADC, CONFIG_FG_SENSOR_WDT_MS, false);

	while (1) {
		struct fg_config cfg;
		struct fg_adc_msg msg = {.ts_ms = fg_now_ms()};
		int32_t raw;

		fg_config_get(&cfg);

		if (hw_ok) {
			msg.light_ok = read_mv(&adc_light, &msg.light_mv, &raw) == 0;
			if (IS_ENABLED(CONFIG_FG_LDR_INVERT)) {
				msg.light_mv = FULL_SCALE_MV - msg.light_mv;
			}

			msg.gas_ok = read_mv(&adc_gas, &msg.gas_mv, &raw) == 0;
			if (msg.gas_ok && at_rail(raw)) {
				if (gas_rail_since == 0) {
					gas_rail_since = msg.ts_ms;
				} else if (msg.ts_ms - gas_rail_since >= RAIL_FAULT_MS) {
					msg.gas_ok = false;
				}
			} else {
				gas_rail_since = 0;
			}
			fg_fault_report(FG_SENSOR_GAS, FG_FAULT_IMPLAUSIBLE, !msg.gas_ok);

			int32_t forced = (int32_t)atomic_get(&gas_override);

			if (forced >= 0) {
				msg.gas_mv = forced;
				msg.gas_ok = true;
			}
			fg_fault_report(FG_SENSOR_LIGHT, FG_FAULT_IMPLAUSIBLE, !msg.light_ok);
		}

		zbus_chan_pub(&chan_adc, &msg, K_MSEC(50));
		fg_sleep_feed(FG_TH_ADC, cfg.sample_period_ms);
	}
}
