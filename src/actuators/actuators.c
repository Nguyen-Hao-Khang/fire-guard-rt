/*
 * Actuator thread (highest application priority)
 *
 * Relay and fan are switched as soon as a new chan_state message arrives
 * (event driven: this is the "reaction" path of NFR-02). Buzzer and LED
 * patterns, and the gas-proportional fan speed, are rendered on a 50 ms
 * tick. Fan and buzzer are LEDC PWM outputs.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "app/config.h"
#include "actuators/actuators.h"
#include "safety/fault_mgr.h"

LOG_MODULE_REGISTER(fg_act, LOG_LEVEL_INF);

#define TICK_MS 50

/* Fan duty (%) per state; ALARM_GAS scales with the gas level */
#define FAN_DRILL_PCT    50
#define FAN_GAS_MIN_PCT  60
#define FAN_FULL_PCT     100

ZBUS_SUBSCRIBER_DEFINE(act_sub, 4);
ZBUS_CHAN_ADD_OBS(chan_state, act_sub, 1);

static const struct gpio_dt_spec relay = GPIO_DT_SPEC_GET(DT_ALIAS(fg_relay), gpios);
static const struct pwm_dt_spec fan = PWM_DT_SPEC_GET(DT_ALIAS(fg_fan));
static const struct pwm_dt_spec buzzer = PWM_DT_SPEC_GET(DT_ALIAS(fg_buzzer));
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(fg_led_r), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(fg_led_g), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(fg_led_b), gpios);

static atomic_t beep_req;
static uint8_t fan_pct = 0xFF;   /* last applied duty, 0xFF = unknown */
static bool buzzer_on = true;
static struct fg_latency_stats lat;
static struct k_spinlock lat_lock;

static void set_fan(uint8_t pct)
{
	if (pct == fan_pct) {
		return;
	}
	fan_pct = pct;
	pwm_set_pulse_dt(&fan, (uint32_t)((uint64_t)fan.period * pct / 100U));
}

static void set_buzzer(bool on)
{
	if (on == buzzer_on) {
		return;
	}
	buzzer_on = on;
	/* passive buzzer: 50 % square wave tone; active buzzer: DC level */
	uint32_t pulse = IS_ENABLED(CONFIG_FG_BUZZER_ACTIVE) ? buzzer.period : buzzer.period / 2;

	pwm_set_pulse_dt(&buzzer, on ? pulse : 0);
}

/* Fan duty for the given state, scaled with the gas level in ALARM_GAS */
static uint8_t fan_duty(enum fg_state s)
{
	if (s == FG_STATE_ALARM_FIRE) {
		return FAN_FULL_PCT;
	}
	if (s == FG_STATE_DRILL) {
		return FAN_DRILL_PCT;
	}
	if (s != FG_STATE_ALARM_GAS) {
		return 0;
	}

	struct fg_adc_msg adc;
	struct fg_config cfg;

	/* never wait in the reaction path: full speed if data is busy */
	if (zbus_chan_read(&chan_adc, &adc, K_NO_WAIT) != 0 ||
	    zbus_chan_read(&chan_config, &cfg, K_NO_WAIT) != 0 || !adc.gas_ok) {
		return FAN_FULL_PCT;
	}
	int32_t span = cfg.th_gas_danger_mv - cfg.th_gas_warn_mv;
	int32_t pct = FAN_GAS_MIN_PCT +
		      (adc.gas_mv - cfg.th_gas_warn_mv) * (FAN_FULL_PCT - FAN_GAS_MIN_PCT) / span;

	return (uint8_t)CLAMP(pct, FAN_GAS_MIN_PCT, FAN_FULL_PCT);
}

int fg_act_init(void)
{
	const struct gpio_dt_spec *outs[] = {&relay, &led_r, &led_g, &led_b};

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		if (!gpio_is_ready_dt(outs[i])) {
			return -ENODEV;
		}
		/* Safe default: relay de-energised (mains on), everything off */
		gpio_pin_configure_dt(outs[i], GPIO_OUTPUT_INACTIVE);
	}
	if (!pwm_is_ready_dt(&fan) || !pwm_is_ready_dt(&buzzer)) {
		return -ENODEV;
	}
	set_fan(0);
	set_buzzer(false);
	return 0;
}

void fg_act_beep(enum fg_beep b)
{
	atomic_set(&beep_req, b);
}

void fg_act_latency(struct fg_latency_stats *out)
{
	K_SPINLOCK(&lat_lock) {
		*out = lat;
	}
}

void fg_act_latency_reset(void)
{
	K_SPINLOCK(&lat_lock) {
		lat = (struct fg_latency_stats){0};
	}
}

static void record_latency(uint32_t decision_cyc)
{
	uint32_t us = k_cyc_to_us_floor32(k_cycle_get_32() - decision_cyc);

	K_SPINLOCK(&lat_lock) {
		lat.last_us = us;
		lat.sum_us += us;
		lat.min_us = lat.count ? MIN(lat.min_us, us) : us;
		lat.max_us = MAX(lat.max_us, us);
		lat.count++;
	}
}

/* Mains relay + fan: applied immediately on state change */
static void apply_power(const struct fg_state_msg *st)
{
	bool alarm = fg_state_is_alarm(st->state);

	gpio_pin_set_dt(&relay, alarm);   /* energised = mains cut */
	set_fan(fan_duty(st->state));
}

static void set_rgb(bool r, bool g, bool b)
{
	gpio_pin_set_dt(&led_r, r);
	gpio_pin_set_dt(&led_g, g);
	gpio_pin_set_dt(&led_b, b);
}

/* t = ms since boot; returns true during the "on" part of a period */
static bool phase(uint32_t t, uint32_t period, uint32_t on)
{
	return (t % period) < on;
}

static void render(const struct fg_state_msg *st, uint32_t t, uint32_t *beep_until,
		   enum fg_beep *beep)
{
	bool buzz = false;
	bool fault = st->faults != 0;

	switch (st->state) {
	case FG_STATE_INIT:
	case FG_STATE_WARMUP:
		set_rgb(false, false, phase(t, 1000, 500));
		break;
	case FG_STATE_NORMAL:
		if (fault) {
			bool on = phase(t, 1000, 500);

			set_rgb(on, on, false);               /* yellow blink */
			buzz = phase(t, 30000, 100);          /* maintenance chirp */
		} else {
			set_rgb(false, true, false);          /* green */
		}
		break;
	case FG_STATE_PRE_ALARM: {
		bool on = phase(t, 500, 250);

		set_rgb(on, on, false);                       /* amber blink 2 Hz */
		buzz = phase(t, 5000, 100);
		break;
	}
	case FG_STATE_ALARM_FIRE:
	case FG_STATE_ALARM_GAS: {
		uint32_t period = st->trapped ? 200 : 500;  /* faster if someone trapped */
		bool red = phase(t, period, period / 2);

		set_rgb(red, !red, false);                    /* red / green = exit */
		buzz = (st->state == FG_STATE_ALARM_FIRE) ? true : phase(t, 1000, 500);
		break;
	}
	case FG_STATE_DRILL: {
		bool on = phase(t, 1000, 500);

		set_rgb(on, false, on);                       /* purple */
		buzz = phase(t, 1000, 100);
		break;
	}
	default:
		break;
	}

	/* Feedback beeps override the pattern for a short time */
	enum fg_beep req = atomic_set(&beep_req, FG_BEEP_NONE);

	if (req != FG_BEEP_NONE) {
		*beep = req;
		*beep_until = t + 600;
	}
	if (*beep != FG_BEEP_NONE) {
		if ((int32_t)(t - *beep_until) >= 0) {
			*beep = FG_BEEP_NONE;
		} else {
			uint32_t left = *beep_until - t;

			buzz = (*beep == FG_BEEP_DENIED) ? (left > 400 || (left > 100 && left < 250))
							 : left > 400;
		}
	}
	set_buzzer(buzz);
	if (st->state == FG_STATE_ALARM_GAS) {
		set_fan(fan_duty(st->state));
	}
}

void fg_act_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct fg_state_msg st = {.state = FG_STATE_INIT};
	const struct zbus_channel *chan;
	uint32_t beep_until = 0;
	enum fg_beep beep = FG_BEEP_NONE;

	fg_wdt_register(FG_TH_ACT, CONFIG_FG_CORE_WDT_MS, true);
	/* Publishers inherit this thread priority while notifying it */
	zbus_obs_attach_to_thread(&act_sub);

	while (1) {
		bool entered_alarm = false;

		fg_wdt_feed(FG_TH_ACT);
		fg_hang_point(FG_TH_ACT);
		if (zbus_sub_wait(&act_sub, &chan, K_MSEC(TICK_MS)) == 0) {
			uint8_t prev = st.state;

			if (zbus_chan_read(&chan_state, &st, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			apply_power(&st);
			entered_alarm = fg_state_is_alarm(st.state) && !fg_state_is_alarm(prev);
		}
		render(&st, k_uptime_get_32(), &beep_until, &beep);
		if (entered_alarm) {
			/* relay, fan and buzzer are all driven at this point */
			record_latency(st.decision_cyc);
		}
	}
}
