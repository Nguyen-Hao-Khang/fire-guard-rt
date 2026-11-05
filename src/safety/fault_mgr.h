/*
 * Task watchdog wrappers and fault manager (SRS FR-06)
 */
#ifndef FG_FAULT_MGR_H_
#define FG_FAULT_MGR_H_

#include <stdbool.h>
#include <stdint.h>
#include "app/channels.h"
#include "app/threads.h"

/* Initialise the task watchdog with the hardware watchdog as fallback */
int fg_wdt_init(void);

/*
 * Register the calling thread on its own task-watchdog channel.
 * Sensor threads: expiry -> fault manager isolates + restarts the thread.
 * Core threads (fusion, actuators, fault): expiry -> system reset.
 * Calling again (after a restart) replaces the previous channel.
 */
int fg_wdt_register(enum fg_thread_id th, uint32_t timeout_ms, bool reset_on_expiry);
void fg_wdt_feed(enum fg_thread_id th);

/* Sleep in slices <= 250 ms, feeding the watchdog and honouring hang injection */
void fg_sleep_feed(enum fg_thread_id th, uint32_t ms);

/* Fault injection for tests: next call to fg_hang_point() blocks forever */
void fg_hang_inject(enum fg_thread_id th);
void fg_hang_point(enum fg_thread_id th);

/* Raise/clear a sensor fault (deduplicated, published on chan_fault) */
void fg_fault_report(enum fg_sensor s, enum fg_fault_kind kind, bool active);

/* Current fault mask (FG_FAULT_BIT) */
uint8_t fg_fault_mask(void);

#endif /* FG_FAULT_MGR_H_ */
