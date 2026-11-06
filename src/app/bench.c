/*
 * Load generators and the automated alarm benchmark
 *
 * fg load cpu <pct>   busy-loop thread below every safety thread
 * fg load net <hz>    extra MQTT publications (CoreIoT builds)
 * fg bench <n>        n alarm cycles driven through the real data path:
 *                     the gas reading is overridden at the ADC thread,
 *                     fusion raises ALARM_GAS, a commander card clears it.
 *
 * Detection latency = override applied -> fusion decision.
 * Reaction latency  = fusion decision -> outputs driven (actuator stats).
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include "actuators/actuators.h"
#include "app/channels.h"
#include "app/config.h"
#include "sensors/sensors.h"

#define LOAD_PRIO     12
#define BENCH_PRIO    11
#define LOAD_WINDOW_MS 100
#define WAIT_STEP_MS  5

static atomic_t cpu_load_pct;
static atomic_t bench_cycles;
static K_SEM_DEFINE(bench_go, 0, 1);

static void load_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		uint32_t pct = (uint32_t)atomic_get(&cpu_load_pct);

		if (pct == 0) {
			k_msleep(LOAD_WINDOW_MS);
			continue;
		}
		k_busy_wait(pct * LOAD_WINDOW_MS * 10U); /* pct % of the window, in us */
		if (pct < 100) {
			k_msleep(LOAD_WINDOW_MS * (100 - pct) / 100);
		}
	}
}

K_THREAD_DEFINE(fg_load, 1024, load_thread, NULL, NULL, NULL, LOAD_PRIO, 0, 0);

static bool wait_state(bool alarm, uint32_t timeout_ms, struct fg_state_msg *st)
{
	for (uint32_t t = 0; t < timeout_ms; t += WAIT_STEP_MS) {
		if (zbus_chan_read(&chan_state, st, FG_CHAN_READ_TIMEOUT) == 0 &&
		    (alarm ? fg_state_is_alarm(st->state) : st->state == FG_STATE_NORMAL)) {
			return true;
		}
		k_msleep(WAIT_STEP_MS);
	}
	return false;
}

static void clear_alarm(struct fg_state_msg *st)
{
	static const uint8_t commander[4] = {0xDE, 0xAD, 0xBE, 0xEF};

	if (zbus_chan_read(&chan_state, st, FG_CHAN_READ_TIMEOUT) == 0 &&
	    st->state != FG_STATE_NORMAL) {
		fg_rfid_inject(commander, sizeof(commander));
	}
}

static void bench_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		k_sem_take(&bench_go, K_FOREVER);

		int n = (int)atomic_get(&bench_cycles);
		struct fg_config cfg;
		struct fg_state_msg st;
		uint32_t det_min = UINT32_MAX, det_max = 0, ok = 0;
		uint64_t det_sum = 0;
		struct fg_latency_stats lat;

		fg_config_get(&cfg);
		fg_act_latency_reset();
		printk("[bench] %d alarm cycles\n", n);

		for (int i = 0; i < n; i++) {
			/* start every cycle from NORMAL with a clean reading */
			fg_adc_override_gas(cfg.th_gas_warn_mv / 2);
			clear_alarm(&st);
			if (!wait_state(false, 10000, &st)) {
				printk("[bench] cycle %d: system did not return to NORMAL\n", i + 1);
				break;
			}
			k_msleep(1500);

			uint32_t t0 = k_cycle_get_32();

			fg_adc_override_gas(cfg.th_gas_danger_mv + 300);
			if (!wait_state(true, 10000, &st)) {
				printk("[bench] cycle %d: no alarm within 10 s\n", i + 1);
				break;
			}
			uint32_t det_us = k_cyc_to_us_floor32(st.decision_cyc - t0);

			det_min = MIN(det_min, det_us);
			det_max = MAX(det_max, det_us);
			det_sum += det_us;
			ok++;
			printk("[bench] cycle %d: detection %u ms\n", i + 1, det_us / 1000U);
		}

		fg_adc_override_gas(cfg.th_gas_warn_mv / 2);
		clear_alarm(&st);
		wait_state(false, 10000, &st);
		fg_adc_override_gas(-1);

		fg_act_latency(&lat);
		if (ok == 0) {
			printk("[bench] no complete cycle\n");
			continue;
		}
		printk("[bench] done: %u/%d cycles\n", ok, n);
		printk("[bench] detection ms: min %u avg %u max %u (gas limit 30000)\n",
		       det_min / 1000U, (uint32_t)(det_sum / ok / 1000U), det_max / 1000U);
		printk("[bench] reaction us: n %u min %u avg %u max %u (limit 1000000)\n", lat.count,
		       lat.min_us, lat.count ? (uint32_t)(lat.sum_us / lat.count) : 0, lat.max_us);
	}
}

K_THREAD_DEFINE(fg_bench, 1536, bench_thread, NULL, NULL, NULL, BENCH_PRIO, 0, 0);

static int cmd_load_cpu(const struct shell *sh, size_t argc, char **argv)
{
	long pct = strtol(argv[1], NULL, 10);

	if (pct < 0 || pct > 100) {
		shell_error(sh, "percent 0..100");
		return -EINVAL;
	}
	atomic_set(&cpu_load_pct, pct);
	shell_print(sh, "cpu load %ld %% at priority %d", pct, LOAD_PRIO);
	return 0;
}

static int cmd_load_net(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_FG_COMM)
	long hz = strtol(argv[1], NULL, 10);

	if (hz < 0 || hz > 50) {
		shell_error(sh, "rate 0..50 Hz");
		return -EINVAL;
	}
	fg_comm_set_load((uint32_t)hz);
	shell_print(sh, "extra MQTT publications: %ld Hz", hz);
	return 0;
#else
	shell_error(sh, "requires a build with comm.conf");
	return -ENOTSUP;
#endif
}

static int cmd_bench(const struct shell *sh, size_t argc, char **argv)
{
	long n = strtol(argv[1], NULL, 10);

	if (n < 1 || n > 100) {
		shell_error(sh, "cycles 1..100");
		return -EINVAL;
	}
	atomic_set(&bench_cycles, n);
	k_sem_give(&bench_go);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(load_cmds,
	SHELL_CMD_ARG(cpu, NULL, "cpu <percent>", cmd_load_cpu, 2, 0),
	SHELL_CMD_ARG(net, NULL, "net <hz>", cmd_load_net, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((fg), load, &load_cmds, "Background load generators", NULL, 1, 0);
SHELL_SUBCMD_ADD((fg), bench, NULL, "bench <cycles>: automated alarm latency benchmark",
		 cmd_bench, 2, 0);
