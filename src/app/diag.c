/*
 * Periodic runtime diagnostics
 *
 * Printed on demand ("fg threads") or periodically from the fault manager
 * thread (priority 2), which stays visible even when lower priority
 * threads, including the shell, are starved. Each line shows a thread, its
 * priority, state and CPU share since the previous report.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "app/channels.h"
#include "app/diag.h"

#define MAX_THREADS 24

struct usage_slot {
	const struct k_thread *thread;
	uint64_t cycles;
};

static struct usage_slot slots[MAX_THREADS];
static uint64_t total_prev;
static uint64_t total_now;
static uint32_t next_report_ms;

static struct usage_slot *slot_of(const struct k_thread *t)
{
	for (int i = 0; i < MAX_THREADS; i++) {
		if (slots[i].thread == t) {
			return &slots[i];
		}
	}
	for (int i = 0; i < MAX_THREADS; i++) {
		if (slots[i].thread == NULL) {
			slots[i].thread = t;
			return &slots[i];
		}
	}
	return NULL;
}

static void print_thread(const struct k_thread *cthread, void *user_data)
{
	struct k_thread *t = (struct k_thread *)cthread;
	k_thread_runtime_stats_t rt;
	struct usage_slot *s = slot_of(t);
	uint64_t delta = 0;
	uint64_t span = total_now - total_prev;
	const char *name = k_thread_name_get(t);
	char state[32];

	ARG_UNUSED(user_data);

	if (k_thread_runtime_stats_get(t, &rt) != 0) {
		return;
	}
	if (s) {
		delta = rt.execution_cycles - s->cycles;
		s->cycles = rt.execution_cycles;
	}
	k_thread_state_str(t, state, sizeof(state));
	printk("  %-14s prio %3d  %-10s cpu %3u%%\n", name ? name : "?", k_thread_priority_get(t),
	       state, span ? (unsigned int)(delta * 100U / span) : 0U);
}

void fg_diag_report(void)
{
	k_thread_runtime_stats_t all;
	struct fg_state_msg st = {0};
	struct fg_adc_msg adc = {0};
	struct fg_dht_msg dht = {0};

	k_thread_runtime_stats_all_get(&all);
	total_now = all.execution_cycles;

	zbus_chan_read(&chan_state, &st, FG_CHAN_READ_TIMEOUT);
	zbus_chan_read(&chan_adc, &adc, FG_CHAN_READ_TIMEOUT);
	zbus_chan_read(&chan_dht, &dht, FG_CHAN_READ_TIMEOUT);

	printk("[diag %u ms] state %s, adc ts %u (light %d gas %d), dht ts %u ok %d\n",
	       k_uptime_get_32(), fg_state_name(st.state), adc.ts_ms, adc.light_mv, adc.gas_mv,
	       dht.ts_ms, dht.ok);
	k_thread_foreach_unlocked(print_thread, NULL);
	total_prev = total_now;
}

void fg_diag_tick(uint32_t now_ms)
{
	if (CONFIG_FG_DIAG_PERIOD_MS == 0 || (int32_t)(now_ms - next_report_ms) < 0) {
		return;
	}
	next_report_ms = now_ms + CONFIG_FG_DIAG_PERIOD_MS;
	fg_diag_report();
}
