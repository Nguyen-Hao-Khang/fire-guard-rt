/*
 * Thread table
 *
 * Threads are created with K_FOREVER so they can be (re)created by the
 * fault manager. In the AMP stage the safety rows run on APP_CPU and the
 * comm row on PRO_CPU; the table is the input of the RTA spreadsheet.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/threads.h"

LOG_MODULE_REGISTER(fg_threads, LOG_LEVEL_INF);

#define STACK_ACT    1536
#define STACK_FAULT  1536
#define STACK_FUSION 2048
#define STACK_ADC    1536
#define STACK_DHT    1536
#define STACK_ULTRA  1536
#define STACK_RFID   1536
#define STACK_LOG    1536
#define STACK_COMM   4096

K_THREAD_STACK_DEFINE(stk_act, STACK_ACT);
K_THREAD_STACK_DEFINE(stk_fault, STACK_FAULT);
K_THREAD_STACK_DEFINE(stk_fusion, STACK_FUSION);
K_THREAD_STACK_DEFINE(stk_adc, STACK_ADC);
K_THREAD_STACK_DEFINE(stk_dht, STACK_DHT);
K_THREAD_STACK_DEFINE(stk_ultra, STACK_ULTRA);
K_THREAD_STACK_DEFINE(stk_rfid, STACK_RFID);
K_THREAD_STACK_DEFINE(stk_log, STACK_LOG);
#if defined(CONFIG_FG_COMM)
K_THREAD_STACK_DEFINE(stk_comm, STACK_COMM);
#endif

struct fg_thread_desc {
	const char *name;
	k_thread_entry_t entry;
	k_thread_stack_t *stack;
	size_t stack_size;
	int prio;
	int sensor; /* owning sensor, -1 = none */
	struct k_thread thread;
	k_tid_t tid;
};

#define DESC(_n, _e, _s, _p, _sen)                                                   \
	{ .name = _n, .entry = _e, .stack = _s, .stack_size = K_THREAD_STACK_SIZEOF(_s), \
	  .prio = _p, .sensor = _sen }

static struct fg_thread_desc table[FG_TH_COUNT] = {
	[FG_TH_ACT] = DESC("fg_act", fg_act_thread, stk_act, 1, -1),
	[FG_TH_FAULT] = DESC("fg_fault", fg_fault_thread, stk_fault, 2, -1),
	[FG_TH_FUSION] = DESC("fg_fusion", fg_fusion_thread, stk_fusion, 3, -1),
	[FG_TH_ADC] = DESC("fg_adc", fg_adc_thread, stk_adc, 4, FG_SENSOR_LIGHT),
	[FG_TH_DHT] = DESC("fg_dht", fg_dht_thread, stk_dht, 5, FG_SENSOR_DHT),
	[FG_TH_ULTRA] = DESC("fg_ultra", fg_ultra_thread, stk_ultra, 6, FG_SENSOR_ULTRASONIC),
	[FG_TH_RFID] = DESC("fg_rfid", fg_rfid_thread, stk_rfid, 7, FG_SENSOR_RFID),
	[FG_TH_LOG] = DESC("fg_log", fg_log_thread, stk_log, 9, -1),
#if defined(CONFIG_FG_COMM)
	[FG_TH_COMM] = DESC("fg_comm", fg_comm_thread, stk_comm, 10, -1),
#endif
};

static void create(struct fg_thread_desc *d)
{
	d->tid = k_thread_create(&d->thread, d->stack, d->stack_size, d->entry, NULL, NULL,
				 NULL, d->prio, 0, K_FOREVER);
	k_thread_name_set(d->tid, d->name);
	k_thread_start(d->tid);
}

void fg_threads_start(void)
{
	for (int i = 0; i < FG_TH_COUNT; i++) {
		create(&table[i]);
	}
	LOG_INF("%d threads started", FG_TH_COUNT);
}

int fg_thread_restart(enum fg_thread_id id)
{
	if (id >= FG_TH_COUNT) {
		return -EINVAL;
	}
	k_thread_abort(table[id].tid);
	create(&table[id]);
	return 0;
}

int fg_thread_of_sensor(enum fg_sensor s)
{
	/* LDR and MQ-2 share the ADC thread */
	if (s == FG_SENSOR_GAS) {
		s = FG_SENSOR_LIGHT;
	}
	for (int i = 0; i < FG_TH_COUNT; i++) {
		if (table[i].sensor == (int)s) {
			return i;
		}
	}
	return -1;
}

k_tid_t fg_thread_tid(enum fg_thread_id id)
{
	return (id < FG_TH_COUNT) ? table[id].tid : NULL;
}

const char *fg_thread_name(enum fg_thread_id id)
{
	return (id < FG_TH_COUNT) ? table[id].name : "?";
}
