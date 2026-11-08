/*
 * Response-time measurement and analysis for the safety threads
 *
 * Each safety thread marks the start and end of every job. A job's
 * execution time C is the CPU time the thread itself consumed (scheduler
 * usage counters, preemption excluded); its response time R is the wall
 * time from release to completion. Blocking sections that stall every
 * thread (flash writes) are recorded separately as the blocking term B.
 */
#ifndef FG_RT_STATS_H_
#define FG_RT_STATS_H_

#include <stdint.h>
#include "app/threads.h"

#if defined(CONFIG_FG_RT_STATS)

/* Start a job; release_cyc = 0 means "released now" */
void fg_rt_begin(enum fg_thread_id th, uint32_t release_cyc);

/* Move the release point of the running job (e.g. to a publish time) */
void fg_rt_set_release(enum fg_thread_id th, uint32_t release_cyc);

/* End the running job, if any */
void fg_rt_end(enum fg_thread_id th);

/* Record a section that blocked the whole CPU */
void fg_rt_blocking(uint32_t cycles);

#else

static inline void fg_rt_begin(enum fg_thread_id th, uint32_t release_cyc)
{
}

static inline void fg_rt_set_release(enum fg_thread_id th, uint32_t release_cyc)
{
}

static inline void fg_rt_end(enum fg_thread_id th)
{
}

static inline void fg_rt_blocking(uint32_t cycles)
{
}

#endif /* CONFIG_FG_RT_STATS */

#endif /* FG_RT_STATS_H_ */
