/*
 * Unit tests of the fusion state machine (host, native_sim)
 *
 * Time is simulated: one fusion_step() per sample period (1 s), DHT22 every 2 s.
 * Timing assertions follow NFR-01 (TCVN 5738:2021): flame <= 5 s,
 * smoke/gas <= 30 s, heat <= 120 s.
 */
#include <zephyr/ztest.h>
#include "fusion/fusion.h"

#define SAMPLE_MS 1000
#define WARMUP_MS 5000

static const struct fg_config cfg = {
	.sample_period_ms = SAMPLE_MS, .callin_period_s = 60,
	.th_light_delta_mv = 800, .th_gas_rise_mv = 400,
	.th_gas_warn_mv = 1500, .th_gas_danger_mv = 2500,
	.th_temp_max_x10 = 570, .th_temp_rise_x10 = 30,
	.confirm_window_ms = 4000,
};

struct env {
	int32_t light, gas;
	int16_t temp;
	bool light_ok, gas_ok, temp_ok;
};

static struct fusion_ctx ctx;
static struct env env;
static uint32_t now;

static void step(void)
{
	struct fusion_input in = {
		.now_ms = now, .light_mv = env.light, .gas_mv = env.gas, .temp_x10 = env.temp,
		.light_ok = env.light_ok, .gas_ok = env.gas_ok, .temp_ok = env.temp_ok,
		.temp_new = (now % 2000) == 0,
	};

	fusion_step(&ctx, &in);
	now += SAMPLE_MS;
}

/* Run until state == target or limit; returns elapsed ms or UINT32_MAX */
static uint32_t run_until(enum fg_state target, uint32_t limit_ms)
{
	uint32_t start = now;

	while (now - start <= limit_ms) {
		step();
		if (ctx.state == target) {
			return now - SAMPLE_MS - start;
		}
	}
	return UINT32_MAX;
}

static void run_for(uint32_t ms)
{
	uint32_t end = now + ms;

	while (now < end) {
		step();
	}
}

static void *setup(void)
{
	return NULL;
}

static void before(void *f)
{
	now = 1000;
	env = (struct env){.light = 1000, .gas = 600, .temp = 280,
			   .light_ok = true, .gas_ok = true, .temp_ok = true};
	fusion_init(&ctx, &cfg, now, WARMUP_MS);
	/* boot, warm up and settle the baselines */
	zassert_not_equal(run_until(FG_STATE_NORMAL, 10000), UINT32_MAX);
	run_for(30000);
	zassert_equal(ctx.state, FG_STATE_NORMAL);
}

ZTEST_SUITE(fusion, NULL, setup, before, NULL, NULL);

ZTEST(fusion, test_flame_confirmed_by_smoke_within_5s)
{
	env.light += 1500; /* flame flash */
	env.gas += 600;    /* smoke */
	uint32_t t = run_until(FG_STATE_ALARM_FIRE, 10000);

	zassert_true(t <= 5000, "flame detection took %u ms", t);
	zassert_equal(ctx.cause, FG_CAUSE_FIRE_CONFIRMED);
}

ZTEST(fusion, test_flame_confirmed_by_heat_rise)
{
	env.light += 1500;
	uint32_t start = now;

	/* temperature +1.0 C per second */
	while (ctx.state != FG_STATE_ALARM_FIRE && now - start < 10000) {
		env.temp += 10;
		step();
	}
	zassert_equal(ctx.state, FG_STATE_ALARM_FIRE);
	zassert_true(now - start <= 6000, "took %u ms", now - start);
}

ZTEST(fusion, test_light_only_is_not_an_alarm)
{
	env.light += 1500; /* room light switched on and left on */
	zassert_not_equal(run_until(FG_STATE_PRE_ALARM, 3000), UINT32_MAX);
	run_for(120000);
	zassert_true(ctx.state == FG_STATE_PRE_ALARM || ctx.state == FG_STATE_NORMAL);
	zassert_false(fg_state_is_alarm(ctx.state));
	/* baseline absorbs the new light level -> back to NORMAL */
	zassert_equal(ctx.state, FG_STATE_NORMAL);
}

ZTEST(fusion, test_flash_of_light_returns_to_normal)
{
	env.light += 1500;
	step();
	env.light -= 1500;
	zassert_equal(run_until(FG_STATE_NORMAL, 10000) != UINT32_MAX, true);
	zassert_equal(ctx.cause, FG_CAUSE_NOT_CONFIRMED);
}

ZTEST(fusion, test_gas_leak_within_30s)
{
	env.gas = 1800; /* above warning, below danger, no flame */
	uint32_t t = run_until(FG_STATE_ALARM_GAS, 40000);

	zassert_true(t <= 30000, "gas detection took %u ms", t);
	zassert_equal(ctx.cause, FG_CAUSE_GAS_CONFIRMED);
}

ZTEST(fusion, test_gas_danger_direct)
{
	env.gas = 2800;
	zassert_equal(run_until(FG_STATE_ALARM_GAS, 2000), 0);
	zassert_equal(ctx.cause, FG_CAUSE_GAS_DANGER);
}

ZTEST(fusion, test_heat_absolute_within_120s)
{
	uint32_t start = now;

	/* slow heating 0.2 C/s from 28 C: below rate-of-rise, reaches 57 C */
	while (ctx.state != FG_STATE_ALARM_FIRE && now - start < 200000) {
		env.temp += 2;
		step();
	}
	zassert_equal(ctx.state, FG_STATE_ALARM_FIRE);
	zassert_equal(ctx.cause, FG_CAUSE_HEAT_ABSOLUTE);
}

ZTEST(fusion, test_override_and_realarm)
{
	env.gas = 2800;
	run_until(FG_STATE_ALARM_GAS, 2000);
	zassert_true(fusion_override(&ctx, now));
	zassert_equal(ctx.state, FG_STATE_NORMAL);
	/* condition still present: alarm again */
	zassert_true(run_until(FG_STATE_ALARM_GAS, 5000) != UINT32_MAX);
	/* condition gone: override sticks */
	env.gas = 600;
	zassert_true(fusion_override(&ctx, now));
	run_for(30000);
	zassert_equal(ctx.state, FG_STATE_NORMAL);
}

ZTEST(fusion, test_degraded_without_ldr)
{
	env.light_ok = false; /* LDR isolated by the fault manager */
	uint32_t start = now;

	env.gas += 600;
	while (ctx.state != FG_STATE_ALARM_FIRE && now - start < 10000) {
		env.temp += 10;
		step();
	}
	zassert_equal(ctx.state, FG_STATE_ALARM_FIRE);
}

ZTEST(fusion, test_gas_ignites_upgrades_to_fire)
{
	env.gas = 2800;
	run_until(FG_STATE_ALARM_GAS, 2000);
	env.light += 1500;
	env.gas += 400;
	zassert_true(run_until(FG_STATE_ALARM_FIRE, 5000) != UINT32_MAX);
}

ZTEST(fusion, test_drill)
{
	zassert_true(fusion_drill(&ctx, now, 10));
	zassert_equal(ctx.state, FG_STATE_DRILL);
	zassert_true(run_until(FG_STATE_NORMAL, 12000) != UINT32_MAX);
	zassert_equal(ctx.cause, FG_CAUSE_DRILL_END);
}

ZTEST(fusion, test_real_fire_during_drill)
{
	fusion_drill(&ctx, now, 60);
	env.gas = 2800;
	zassert_true(run_until(FG_STATE_ALARM_GAS, 2000) != UINT32_MAX);
}
