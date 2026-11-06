/*
 * Fusion thread: glue between zbus channels and fusion.c
 *
 * Event driven: wakes on every channel it observes, evaluates the state
 * machine on each ADC sample, publishes chan_state on every change.
 */
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "app/config.h"
#include "fusion/fusion.h"
#include "safety/fault_mgr.h"
#include "actuators/actuators.h"

LOG_MODULE_REGISTER(fg_fusion, LOG_LEVEL_INF);

ZBUS_SUBSCRIBER_DEFINE(fusion_sub, 16);
ZBUS_CHAN_ADD_OBS(chan_adc, fusion_sub, 3);
ZBUS_CHAN_ADD_OBS(chan_dht, fusion_sub, 3);
ZBUS_CHAN_ADD_OBS(chan_rfid, fusion_sub, 3);
ZBUS_CHAN_ADD_OBS(chan_presence, fusion_sub, 3);
ZBUS_CHAN_ADD_OBS(chan_fault, fusion_sub, 3);
ZBUS_CHAN_ADD_OBS(chan_config, fusion_sub, 3);
ZBUS_CHAN_ADD_OBS(chan_cmd, fusion_sub, 3);

/* Survives a warm/watchdog reset: lets the system re-enter ALARM (FR-06) */
#define RESTORE_MAGIC 0xF1AE5AFEU
static __noinit struct {
	uint32_t magic;
	uint32_t state;
} persist;

static struct fusion_ctx ctx;
static bool trapped;

static void persist_state(enum fg_state s)
{
	persist.magic = RESTORE_MAGIC;
	persist.state = fg_state_is_alarm(s) ? s : FG_STATE_NORMAL;
}

static void publish_state(void)
{
	struct fg_state_msg msg = {
		.ts_ms = fg_now_ms(),
		.decision_cyc = k_cycle_get_32(),
		.state = ctx.state,
		.prev_state = ctx.prev_state,
		.cause = ctx.cause,
		.faults = fg_fault_mask(),
		.trapped = trapped,
	};

	if (!fg_state_is_alarm(ctx.state) && ctx.state != FG_STATE_DRILL) {
		msg.trapped = trapped = false;
	}
	persist_state(ctx.state);
	zbus_chan_pub(&chan_state, &msg, K_MSEC(20));
}

static void log_transition(void)
{
	LOG_INF("%s -> %s (%s)", fg_state_name(ctx.prev_state), fg_state_name(ctx.state),
		fg_cause_name(ctx.cause));
}

void fg_fusion_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct fg_config cfg;
	struct fg_dht_msg dht = {0};
	uint32_t dht_used_ts = 0;
	const struct zbus_channel *chan;

	fg_config_get(&cfg);
	fusion_init(&ctx, &cfg, fg_now_ms(), CONFIG_FG_WARMUP_S * 1000U);
	fg_wdt_register(FG_TH_FUSION, CONFIG_FG_CORE_WDT_MS, true);
	/* Publishers inherit this thread priority while notifying it */
	zbus_obs_attach_to_thread(&fusion_sub);

	if (persist.magic == RESTORE_MAGIC && fusion_restore(&ctx, persist.state, fg_now_ms())) {
		LOG_WRN("restoring %s after reset", fg_state_name(ctx.state));
	}
	publish_state();

	while (1) {
		bool changed = false;

		fg_wdt_feed(FG_TH_FUSION);
		fg_hang_point(FG_TH_FUSION);
		if (zbus_sub_wait(&fusion_sub, &chan, K_MSEC(250)) != 0) {
			continue;
		}

		if (chan == &chan_adc) {
			struct fg_adc_msg adc;
			uint8_t faults = fg_fault_mask();

			if (zbus_chan_read(&chan_adc, &adc, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			struct fusion_input in = {
				.now_ms = adc.ts_ms,
				.light_mv = adc.light_mv,
				.gas_mv = adc.gas_mv,
				.light_ok = adc.light_ok && !(faults & FG_FAULT_BIT(FG_SENSOR_LIGHT)),
				.gas_ok = adc.gas_ok && !(faults & FG_FAULT_BIT(FG_SENSOR_GAS)),
				.temp_x10 = dht.temp_x10,
				.temp_ok = dht.ok && !(faults & FG_FAULT_BIT(FG_SENSOR_DHT)) &&
					   (adc.ts_ms - dht.ts_ms) < 10000,
				.temp_new = dht.ts_ms != dht_used_ts,
			};

			dht_used_ts = dht.ts_ms;
			changed = fusion_step(&ctx, &in);
		} else if (chan == &chan_dht) {
			if (zbus_chan_read(&chan_dht, &dht, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
		} else if (chan == &chan_rfid) {
			struct fg_rfid_msg card;

			if (zbus_chan_read(&chan_rfid, &card, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			if (card.authorized) {
				changed = fusion_override(&ctx, fg_now_ms());
				fg_act_beep(changed ? FG_BEEP_ACCEPTED : FG_BEEP_IGNORED);
			} else {
				LOG_WRN("unauthorized card rejected");
				fg_act_beep(FG_BEEP_DENIED);
			}
		} else if (chan == &chan_presence) {
			struct fg_presence_msg p;

			if (zbus_chan_read(&chan_presence, &p, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			trapped = p.trapped;
			publish_state();
		} else if (chan == &chan_fault) {
			publish_state(); /* fault mask is part of the state message */
		} else if (chan == &chan_config) {
			fg_config_get(&cfg);
			fusion_set_config(&ctx, &cfg);
		} else if (chan == &chan_cmd) {
			struct fg_cmd_msg cmd;

			if (zbus_chan_read(&chan_cmd, &cmd, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			changed = fusion_drill(&ctx, fg_now_ms(),
					       cmd.kind == FG_CMD_DRILL_START ? cmd.duration_s : 0);
		}

		if (changed) {
			log_transition();
			publish_state();
		}
	}
}

uint32_t fg_fusion_indicators(void)
{
	return ctx.indicators;
}
