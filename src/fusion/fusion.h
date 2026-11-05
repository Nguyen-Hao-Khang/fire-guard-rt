/*
 * Multi-sensor fusion and system state machine
 *
 * Pure C (no kernel calls): every function takes the current time as input,
 * so the logic can be unit-tested on native_sim and its WCET measured.
 */
#ifndef FG_FUSION_H_
#define FG_FUSION_H_

#include <stdbool.h>
#include <stdint.h>
#include "app/channels.h"

#define FUSION_TEMP_HIST 8

struct fusion_input {
	uint32_t now_ms;
	int32_t light_mv;
	int32_t gas_mv;
	int16_t temp_x10;
	bool light_ok;
	bool gas_ok;
	bool temp_ok;
	bool temp_new;      /* a new DHT22 sample arrived in this step */
};

/* Indicator flags, exported for telemetry/debug */
#define FUSION_IND_LIGHT      BIT(0)
#define FUSION_IND_SMOKE      BIT(1)
#define FUSION_IND_GAS_WARN   BIT(2)
#define FUSION_IND_GAS_DANGER BIT(3)
#define FUSION_IND_HEAT_RISE  BIT(4)
#define FUSION_IND_HEAT_ABS   BIT(5)

struct fusion_ctx {
	struct fg_config cfg;
	enum fg_state state;
	enum fg_state prev_state;
	enum fg_cause cause;
	uint32_t indicators;

	int32_t base_light_x16;   /* EMA baselines, fixed point x16 */
	int32_t base_gas_x16;
	bool baseline_valid;

	uint32_t seen_light_ms;   /* last time each indicator was active, 0 = never */
	uint32_t seen_smoke_ms;
	uint32_t seen_heat_ms;
	uint8_t gas_warn_count;

	uint32_t warmup_end_ms;
	uint32_t pre_alarm_start_ms;
	uint32_t drill_end_ms;

	struct {
		uint32_t ts_ms;
		int16_t temp_x10;
	} temp_hist[FUSION_TEMP_HIST];
	uint8_t temp_head;
	uint8_t temp_count;
};

void fusion_init(struct fusion_ctx *ctx, const struct fg_config *cfg, uint32_t now_ms,
		 uint32_t warmup_ms);

void fusion_set_config(struct fusion_ctx *ctx, const struct fg_config *cfg);

/* Evaluate one step; returns true when ctx->state changed (cause in ctx->cause) */
bool fusion_step(struct fusion_ctx *ctx, const struct fusion_input *in);

/* Commander card accepted: ALARM/DRILL -> NORMAL. Returns true if state changed */
bool fusion_override(struct fusion_ctx *ctx, uint32_t now_ms);

/* Remote drill request. Returns true if state changed */
bool fusion_drill(struct fusion_ctx *ctx, uint32_t now_ms, uint16_t duration_s);

/* Re-enter an alarm state after a watchdog reset */
bool fusion_restore(struct fusion_ctx *ctx, enum fg_state state, uint32_t now_ms);

/* Current indicator flags (FUSION_IND_*) of the running fusion thread */
uint32_t fg_fusion_indicators(void);

#endif /* FG_FUSION_H_ */
