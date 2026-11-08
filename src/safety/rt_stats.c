/*
 * Response-time measurement and analysis for the safety threads
 *
 * "fg rt"  prints measured C and R per thread.
 * "fg rta" runs a fixed-priority response-time analysis on the measured
 *          worst-case execution times:
 *
 *            R_i = C_i + B + sum_{j in hp(i)} ceil(R_i / T_j) * C_j
 *
 *          iterated until it converges or exceeds the deadline D_i.
 *
 * T is the minimum inter-arrival time of each thread's jobs: sensor
 * threads wake at least every 250 ms to feed the task watchdog, so every
 * wake-up is a job and C_max covers the heaviest one (the sensor read).
 */
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "app/threads.h"
#include "safety/rt_stats.h"

#define RTA_MARGIN_PCT 120   /* C used in the analysis = 1.2 x measured max */
#define RTA_MAX_ITER   64

struct rt_model {
	uint16_t t_ms;   /* minimum inter-arrival time */
	uint16_t d_ms;   /* relative deadline */
};

/* Safety threads in decreasing priority order, see app/threads.c */
static const struct rt_model model[] = {
	[FG_TH_ACT] = {50, 50},       /* state events + 50 ms render tick */
	[FG_TH_FAULT] = {250, 250},   /* fault queue + 250 ms housekeeping */
	[FG_TH_FUSION] = {100, 100},  /* sensor messages, at most one per 100 ms */
	[FG_TH_ADC] = {250, 250},
	[FG_TH_DHT] = {250, 250},
	[FG_TH_ULTRA] = {200, 200},
	[FG_TH_RFID] = {200, 200},
};

#define N_SAFETY ARRAY_SIZE(model)

struct rt_slot {
	uint32_t jobs;
	uint32_t c_max;
	uint32_t r_max;
	uint64_t c_sum;
	uint32_t release;
	uint64_t exec_start;
	bool active;
};

static struct rt_slot slots[N_SAFETY];
static uint32_t blocking_max;
static struct k_spinlock lock;

static uint64_t own_cycles(void)
{
	k_thread_runtime_stats_t s;

	k_thread_runtime_stats_get(k_current_get(), &s);
	return s.execution_cycles;
}

void fg_rt_begin(enum fg_thread_id th, uint32_t release_cyc)
{
	if (th >= N_SAFETY) {
		return;
	}
	struct rt_slot *s = &slots[th];

	s->release = release_cyc ? release_cyc : k_cycle_get_32();
	s->exec_start = own_cycles();
	s->active = true;
}

void fg_rt_set_release(enum fg_thread_id th, uint32_t release_cyc)
{
	if (th < N_SAFETY && slots[th].active) {
		slots[th].release = release_cyc;
	}
}

void fg_rt_end(enum fg_thread_id th)
{
	if (th >= N_SAFETY || !slots[th].active) {
		return;
	}
	struct rt_slot *s = &slots[th];
	uint32_t c = (uint32_t)(own_cycles() - s->exec_start);
	uint32_t r = k_cycle_get_32() - s->release;

	K_SPINLOCK(&lock) {
		s->jobs++;
		s->c_sum += c;
		s->c_max = MAX(s->c_max, c);
		s->r_max = MAX(s->r_max, r);
	}
	s->active = false;
}

void fg_rt_blocking(uint32_t cycles)
{
	K_SPINLOCK(&lock) {
		blocking_max = MAX(blocking_max, cycles);
	}
}

static uint32_t us(uint64_t cyc)
{
	return (uint32_t)k_cyc_to_us_ceil64(cyc);
}

static void reset(void)
{
	K_SPINLOCK(&lock) {
		for (size_t i = 0; i < N_SAFETY; i++) {
			slots[i].jobs = 0;
			slots[i].c_max = 0;
			slots[i].r_max = 0;
			slots[i].c_sum = 0;
		}
		blocking_max = 0;
	}
}

static int cmd_rt(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "reset") != 0) {
			shell_error(sh, "usage: rt [reset]");
			return -EINVAL;
		}
		reset();
		shell_print(sh, "statistics cleared");
		return 0;
	}

	shell_print(sh, "%-10s %4s %8s %10s %10s %10s", "thread", "prio", "jobs", "C_avg_us",
		    "C_max_us", "R_max_us");
	for (size_t i = 0; i < N_SAFETY; i++) {
		struct rt_slot s;

		K_SPINLOCK(&lock) {
			s = slots[i];
		}
		shell_print(sh, "%-10s %4d %8u %10u %10u %10u", fg_thread_name(i),
			    k_thread_priority_get(fg_thread_tid(i)), s.jobs,
			    s.jobs ? us(s.c_sum / s.jobs) : 0, us(s.c_max), us(s.r_max));
	}
	shell_print(sh, "blocking (flash write) max: %u us", us(blocking_max));
	return 0;
}

static int cmd_rta(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t c[N_SAFETY];
	uint32_t b;
	uint64_t util_ppm = 0;
	bool all_ok = true;

	K_SPINLOCK(&lock) {
		for (size_t i = 0; i < N_SAFETY; i++) {
			c[i] = us(slots[i].c_max) * RTA_MARGIN_PCT / 100U;
		}
		b = us(blocking_max);
	}

	shell_print(sh, "Fixed-priority RTA, C = %u%% of measured max, B = %u us", RTA_MARGIN_PCT,
		    b);
	shell_print(sh, "%-10s %6s %6s %8s %10s %10s %s", "thread", "T_ms", "D_ms", "C_us",
		    "R_bound_us", "R_meas_us", "result");

	for (size_t i = 0; i < N_SAFETY; i++) {
		uint32_t d_us = model[i].d_ms * 1000U;
		uint32_t r = c[i] + b;
		bool ok = false;

		util_ppm += (uint64_t)c[i] * 1000000U / (model[i].t_ms * 1000U);
		for (int it = 0; it < RTA_MAX_ITER && r <= d_us; it++) {
			uint32_t next = c[i] + b;

			for (size_t j = 0; j < i; j++) {
				uint32_t t_us = model[j].t_ms * 1000U;

				next += DIV_ROUND_UP(r, t_us) * c[j];
			}
			if (next == r) {
				ok = true;
				break;
			}
			r = next;
		}
		ok = ok && r <= d_us;
		all_ok &= ok;

		uint32_t r_meas;

		K_SPINLOCK(&lock) {
			r_meas = us(slots[i].r_max);
		}
		shell_print(sh, "%-10s %6u %6u %8u %10u %10u %s", fg_thread_name(i), model[i].t_ms,
			    model[i].d_ms, c[i], r, r_meas,
			    !ok ? "MISS" : (r_meas <= r ? "ok" : "ok (R_meas > bound)"));
	}
	shell_print(sh, "utilisation U = %u.%02u %%, all deadlines %s",
		    (uint32_t)(util_ppm / 10000U), (uint32_t)(util_ppm / 100U % 100U),
		    all_ok ? "met" : "NOT met");
	return 0;
}

SHELL_SUBCMD_ADD((fg), rt, NULL, "rt [reset]: measured execution and response times", cmd_rt,
		 1, 1);
SHELL_SUBCMD_ADD((fg), rta, NULL, "Response-time analysis on measured C", cmd_rta, 1, 0);
