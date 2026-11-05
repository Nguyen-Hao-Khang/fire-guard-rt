/*
 * Thread table (priorities, stacks) and lifecycle
 *
 * Zephyr: lower number = higher priority. The safety chain occupies the
 * highest preemptive priorities; communication runs below all of them.
 */
#ifndef FG_THREADS_H_
#define FG_THREADS_H_

#include <zephyr/kernel.h>
#include "app/channels.h"

enum fg_thread_id {
	FG_TH_ACT = 0,     /* actuators: relay/fan/buzzer/LED         prio 1 */
	FG_TH_FAULT,       /* fault manager                            prio 2 */
	FG_TH_FUSION,      /* fusion + state machine                   prio 3 */
	FG_TH_ADC,         /* LDR + MQ-2                               prio 4 */
	FG_TH_DHT,         /* DHT22                                    prio 5 */
	FG_TH_ULTRA,       /* HC-SR04                                  prio 6 */
	FG_TH_RFID,        /* RC522                                    prio 7 */
	FG_TH_LOG,         /* event log                                prio 9 */
#if defined(CONFIG_FG_COMM)
	FG_TH_COMM,        /* Wi-Fi + MQTT CoreIoT                     prio 10 */
#endif
	FG_TH_COUNT,
};

/* Thread entry points (implemented in their modules) */
void fg_act_thread(void *p1, void *p2, void *p3);
void fg_fault_thread(void *p1, void *p2, void *p3);
void fg_fusion_thread(void *p1, void *p2, void *p3);
void fg_adc_thread(void *p1, void *p2, void *p3);
void fg_dht_thread(void *p1, void *p2, void *p3);
void fg_ultra_thread(void *p1, void *p2, void *p3);
void fg_rfid_thread(void *p1, void *p2, void *p3);
void fg_log_thread(void *p1, void *p2, void *p3);
void fg_comm_thread(void *p1, void *p2, void *p3);

void fg_threads_start(void);

/* (Re)create a thread from its descriptor; used by the fault manager */
int fg_thread_restart(enum fg_thread_id id);

/* Thread that owns a given sensor, or -1 */
int fg_thread_of_sensor(enum fg_sensor s);

k_tid_t fg_thread_tid(enum fg_thread_id id);
const char *fg_thread_name(enum fg_thread_id id);

#endif /* FG_THREADS_H_ */
