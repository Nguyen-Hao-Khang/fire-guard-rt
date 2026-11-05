/*
 * DHT22 temperature/humidity (heat detector role)
 *
 * Period: max(sample_period_ms, 2 s); DHT22 cannot be read faster.
 * Three consecutive read failures -> implausible-data fault.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "app/config.h"
#include "safety/fault_mgr.h"

LOG_MODULE_REGISTER(fg_dht, LOG_LEVEL_INF);

#define DHT_FAIL_LIMIT 3

void fg_dht_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(fg_dht));
	int fails = 0;

	fg_wdt_register(FG_TH_DHT, CONFIG_FG_SENSOR_WDT_MS, false);

	if (!device_is_ready(dev)) {
		LOG_ERR("DHT22 not ready");
	}

	while (1) {
		struct fg_config cfg;
		struct fg_dht_msg msg = {.ts_ms = fg_now_ms()};
		struct sensor_value t, h;

		fg_config_get(&cfg);

		if (device_is_ready(dev) && sensor_sample_fetch(dev) == 0 &&
		    sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &t) == 0 &&
		    sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &h) == 0) {
			msg.temp_x10 = (int16_t)(t.val1 * 10 + t.val2 / 100000);
			msg.hum_x10 = (int16_t)(h.val1 * 10 + h.val2 / 100000);
			msg.ok = true;
			fails = 0;
		} else if (fails < DHT_FAIL_LIMIT) {
			fails++;
		}

		fg_fault_report(FG_SENSOR_DHT, FG_FAULT_IMPLAUSIBLE, fails >= DHT_FAIL_LIMIT);
		if (msg.ok) {
			zbus_chan_pub(&chan_dht, &msg, K_MSEC(50));
		}
		fg_sleep_feed(FG_TH_DHT, MAX(cfg.sample_period_ms, FG_DHT_MIN_PERIOD_MS));
	}
}
