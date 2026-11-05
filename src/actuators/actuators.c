/*
 * Actuator thread (highest application priority)
 *
 * Relay and fan are switched as soon as a new chan_state message arrives
 * (event driven: this is the "reaction" path of NFR-02). Buzzer and LED
 * patterns are rendered on a 50 ms tick.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "actuators/actuators.h"
#include "safety/fault_mgr.h"

LOG_MODULE_REGISTER(fg_act, LOG_LEVEL_INF);

#define TICK_MS 50

ZBUS_SUBSCRIBER_DEFINE(act_sub, 4);
ZBUS_CHAN_ADD_OBS(chan_state, act_sub, 1);

static const struct gpio_dt_spec relay = GPIO_DT_SPEC_GET(DT_ALIAS(fg_relay), gpios);
static const struct gpio_dt_spec fan = GPIO_DT_SPEC_GET(DT_ALIAS(fg_fan), gpios);
static const struct gpio_dt_spec buzzer = GPIO_DT_SPEC_GET(DT_ALIAS(fg_buzzer), gpios);
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(fg_led_r), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(fg_led_g), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(fg_led_b), gpios);

static atomic_t beep_req;
static struct fg_latency_stats lat;
static struct k_spinlock lat_lock;

int fg_act_init(void)
{
	const struct gpio_dt_spec *outs[] = {&relay, &fan, &buzzer, &led_r, &led_g, &led_b};

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		if (!gpio_is_ready_dt(outs[i])) {
			return -ENODEV;
		}
		/* Safe default: relay de-energised (mains on), everything off */
		gpio_pin_configure_dt(outs[i], GPIO_OUTPUT_INACTIVE);
	}
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
	gpio_pin_set_dt(&fan, alarm || st->state == FG_STATE_DRILL);
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
	gpio_pin_set_dt(&buzzer, buzz);
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

	while (1) {
		bool entered_alarm = false;

		fg_wdt_feed(FG_TH_ACT);
		if (zbus_sub_wait(&act_sub, &chan, K_MSEC(TICK_MS)) == 0) {
			uint8_t prev = st.state;

			zbus_chan_read(&chan_state, &st, K_NO_WAIT);
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
