/*
 * Multi-sensor fusion and system state machine
 *
 * Indicators (computed every sample):
 *   LIGHT      light rises suddenly above the adaptive baseline   (flame)
 *   SMOKE      MQ-2 rises above its adaptive baseline              (smoke)
 *   GAS_WARN   MQ-2 above absolute warning level                   (gas leak)
 *   GAS_DANGER MQ-2 above absolute danger level -> direct ALARM_GAS
 *   HEAT_RISE  temperature rise over ~10 s above threshold        (heat)
 *   HEAT_ABS   temperature above absolute level -> direct ALARM_FIRE
 *
 * ALARM_FIRE: HEAT_ABS, or LIGHT confirmed by SMOKE or HEAT_RISE inside the
 *             confirmation window (if the LDR is faulty: SMOKE + HEAT_RISE).
 * ALARM_GAS : GAS_DANGER, or GAS_WARN for 3 consecutive samples without
 *             any LIGHT inside the window.
 * One unconfirmed indicator only leads to PRE_ALARM.
 */
#include <string.h>
#include <zephyr/sys/util.h>
#include "fusion/fusion.h"

#define EMA_SHIFT       5      /* alpha = 1/32 -> ~30 s time constant at 1 Hz */
#define GAS_WARN_COUNT  3
#define HEAT_SPAN_MS    10000
#define HYST_PCT        80     /* indicator releases below 80 % of threshold */

static bool recent(uint32_t seen, uint32_t now, uint32_t window)
{
	return seen != 0 && (now - seen) <= window;
}

/* Hysteresis: once active, the indicator stays until value < HYST_PCT % of th */
static bool hyst(bool was_active, int32_t value, int32_t th)
{
	return was_active ? (value * 100 >= th * HYST_PCT) : (value >= th);
}

static void set_state(struct fusion_ctx *ctx, enum fg_state s, enum fg_cause cause)
{
	ctx->prev_state = ctx->state;
	ctx->state = s;
	ctx->cause = cause;
}

static void reset_windows(struct fusion_ctx *ctx)
{
	ctx->seen_light_ms = 0;
	ctx->seen_smoke_ms = 0;
	ctx->seen_heat_ms = 0;
	ctx->gas_warn_count = 0;
	ctx->indicators = 0;
}

void fusion_set_config(struct fusion_ctx *ctx, const struct fg_config *cfg)
{
	ctx->cfg = *cfg;
}

void fusion_init(struct fusion_ctx *ctx, const struct fg_config *cfg, uint32_t now_ms,
		 uint32_t warmup_ms)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->cfg = *cfg;
	ctx->state = FG_STATE_INIT;
	ctx->prev_state = FG_STATE_INIT;
	ctx->warmup_end_ms = now_ms + warmup_ms;
}

static void temp_push(struct fusion_ctx *ctx, uint32_t now, int16_t t)
{
	ctx->temp_hist[ctx->temp_head].ts_ms = now;
	ctx->temp_hist[ctx->temp_head].temp_x10 = t;
	ctx->temp_head = (ctx->temp_head + 1) % FUSION_TEMP_HIST;
	if (ctx->temp_count < FUSION_TEMP_HIST) {
		ctx->temp_count++;
	}
}

/* Temperature rise against the oldest sample that is at most HEAT_SPAN_MS old */
static int16_t temp_rise(const struct fusion_ctx *ctx, uint32_t now, int16_t t)
{
	int16_t ref = t;
	uint32_t best_age = 0;

	for (uint8_t i = 0; i < ctx->temp_count; i++) {
		uint32_t age = now - ctx->temp_hist[i].ts_ms;

		if (age <= HEAT_SPAN_MS && age >= best_age) {
			best_age = age;
			ref = ctx->temp_hist[i].temp_x10;
		}
	}
	return t - ref;
}

static void update_baseline(struct fusion_ctx *ctx, const struct fusion_input *in)
{
	if (!ctx->baseline_valid) {
		ctx->base_light_x16 = in->light_mv << 4;
		ctx->base_gas_x16 = in->gas_mv << 4;
		ctx->baseline_valid = true;
		return;
	}
	if (in->light_ok) {
		ctx->base_light_x16 += ((in->light_mv << 4) - ctx->base_light_x16) >> EMA_SHIFT;
	}
	if (in->gas_ok) {
		ctx->base_gas_x16 += ((in->gas_mv << 4) - ctx->base_gas_x16) >> EMA_SHIFT;
	}
}

static uint32_t compute_indicators(struct fusion_ctx *ctx, const struct fusion_input *in,
				   bool warmup)
{
	const struct fg_config *c = &ctx->cfg;
	uint32_t old = ctx->indicators;
	uint32_t ind = 0;
	int32_t dl = in->light_mv - (ctx->base_light_x16 >> 4);
	int32_t dg = in->gas_mv - (ctx->base_gas_x16 >> 4);

	if (in->light_ok && hyst(old & FUSION_IND_LIGHT, dl, c->th_light_delta_mv)) {
		ind |= FUSION_IND_LIGHT;
	}
	if (in->gas_ok && !warmup) {
		if (hyst(old & FUSION_IND_SMOKE, dg, c->th_gas_rise_mv)) {
			ind |= FUSION_IND_SMOKE;
		}
		if (hyst(old & FUSION_IND_GAS_WARN, in->gas_mv, c->th_gas_warn_mv)) {
			ind |= FUSION_IND_GAS_WARN;
		}
		if (in->gas_mv >= c->th_gas_danger_mv) {
			ind |= FUSION_IND_GAS_DANGER;
		}
	}
	if (in->temp_ok) {
		if (temp_rise(ctx, in->now_ms, in->temp_x10) >= c->th_temp_rise_x10) {
			ind |= FUSION_IND_HEAT_RISE;
		}
		if (hyst(old & FUSION_IND_HEAT_ABS, in->temp_x10, c->th_temp_max_x10)) {
			ind |= FUSION_IND_HEAT_ABS;
		}
	}
	return ind;
}

bool fusion_step(struct fusion_ctx *ctx, const struct fusion_input *in)
{
	const uint32_t now = in->now_ms;
	const uint32_t w = ctx->cfg.confirm_window_ms;
	enum fg_state before = ctx->state;
	bool warmup = (int32_t)(now - ctx->warmup_end_ms) < 0;

	if (ctx->state == FG_STATE_INIT) {
		set_state(ctx, FG_STATE_WARMUP, FG_CAUSE_BOOT);
	}
	if (!ctx->baseline_valid) {
		update_baseline(ctx, in);
	}

	uint32_t ind = compute_indicators(ctx, in, warmup);

	ctx->indicators = ind;
	if (in->temp_ok && in->temp_new) {
		temp_push(ctx, now, in->temp_x10);
	}
	if (ind & FUSION_IND_LIGHT) {
		ctx->seen_light_ms = now;
	}
	if (ind & FUSION_IND_SMOKE) {
		ctx->seen_smoke_ms = now;
	}
	if (ind & FUSION_IND_HEAT_RISE) {
		ctx->seen_heat_ms = now;
	}
	ctx->gas_warn_count = (ind & FUSION_IND_GAS_WARN) ?
				      MIN(ctx->gas_warn_count + 1, 255) : 0;

	bool r_light = recent(ctx->seen_light_ms, now, w);
	bool r_smoke = recent(ctx->seen_smoke_ms, now, w);
	bool r_heat = recent(ctx->seen_heat_ms, now, w);

	bool fire_abs = ind & FUSION_IND_HEAT_ABS;
	bool fire = fire_abs || (r_light && (r_smoke || r_heat)) ||
		    (!in->light_ok && r_smoke && r_heat);
	bool gas_danger = ind & FUSION_IND_GAS_DANGER;
	bool gas = gas_danger || (ctx->gas_warn_count >= GAS_WARN_COUNT && !r_light);
	bool any = (ind & (FUSION_IND_LIGHT | FUSION_IND_SMOKE | FUSION_IND_GAS_WARN |
			   FUSION_IND_HEAT_RISE)) != 0;

	switch (ctx->state) {
	case FG_STATE_WARMUP:
	case FG_STATE_NORMAL:
	case FG_STATE_PRE_ALARM:
	case FG_STATE_DRILL:
		if (fire) {
			set_state(ctx, FG_STATE_ALARM_FIRE,
				  fire_abs ? FG_CAUSE_HEAT_ABSOLUTE : FG_CAUSE_FIRE_CONFIRMED);
		} else if (gas) {
			set_state(ctx, FG_STATE_ALARM_GAS,
				  gas_danger ? FG_CAUSE_GAS_DANGER : FG_CAUSE_GAS_CONFIRMED);
		} else if (ctx->state == FG_STATE_WARMUP) {
			if (!warmup) {
				set_state(ctx, FG_STATE_NORMAL, FG_CAUSE_WARMUP_DONE);
			}
		} else if (ctx->state == FG_STATE_NORMAL) {
			if (any) {
				ctx->pre_alarm_start_ms = now;
				set_state(ctx, FG_STATE_PRE_ALARM,
					  (ind & FUSION_IND_LIGHT)       ? FG_CAUSE_LIGHT
					  : (ind & FUSION_IND_HEAT_RISE) ? FG_CAUSE_HEAT
									 : FG_CAUSE_GAS);
			}
		} else if (ctx->state == FG_STATE_PRE_ALARM) {
			if (!any && (now - ctx->pre_alarm_start_ms) >= w) {
				set_state(ctx, FG_STATE_NORMAL, FG_CAUSE_NOT_CONFIRMED);
			}
		} else if ((int32_t)(now - ctx->drill_end_ms) >= 0) {
			set_state(ctx, FG_STATE_NORMAL, FG_CAUSE_DRILL_END);
		}
		break;
	case FG_STATE_ALARM_GAS:
		/* upgrade: a gas leak that ignites becomes a fire */
		if (fire) {
			set_state(ctx, FG_STATE_ALARM_FIRE, FG_CAUSE_FIRE_CONFIRMED);
		}
		break;
	case FG_STATE_ALARM_FIRE:
	default:
		break; /* latched until commander override */
	}

	/* Baselines track the environment only while nothing is going on */
	if (ctx->state == FG_STATE_NORMAL || ctx->state == FG_STATE_PRE_ALARM ||
	    ctx->state == FG_STATE_WARMUP) {
		update_baseline(ctx, in);
	}

	return ctx->state != before;
}

bool fusion_override(struct fusion_ctx *ctx, uint32_t now_ms)
{
	ARG_UNUSED(now_ms);

	if (!fg_state_is_alarm(ctx->state) && ctx->state != FG_STATE_DRILL) {
		return false;
	}
	reset_windows(ctx);
	set_state(ctx, FG_STATE_NORMAL, FG_CAUSE_OVERRIDE);
	return true;
}

bool fusion_drill(struct fusion_ctx *ctx, uint32_t now_ms, uint16_t duration_s)
{
	if (duration_s == 0) {
		if (ctx->state != FG_STATE_DRILL) {
			return false;
		}
		set_state(ctx, FG_STATE_NORMAL, FG_CAUSE_DRILL_END);
		return true;
	}
	if (ctx->state != FG_STATE_NORMAL) {
		return false;
	}
	ctx->drill_end_ms = now_ms + (uint32_t)duration_s * 1000U;
	set_state(ctx, FG_STATE_DRILL, FG_CAUSE_DRILL_START);
	return true;
}

bool fusion_restore(struct fusion_ctx *ctx, enum fg_state state, uint32_t now_ms)
{
	ARG_UNUSED(now_ms);

	if (!fg_state_is_alarm(state)) {
		return false;
	}
	set_state(ctx, state, FG_CAUSE_RESTORED);
	return true;
}
