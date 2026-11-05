/*
 * Actuators: relay, fan, buzzer, RGB LED (SRS FR-03)
 */
#ifndef FG_ACTUATORS_H_
#define FG_ACTUATORS_H_

#include <stdint.h>

enum fg_beep {
	FG_BEEP_NONE = 0,
	FG_BEEP_ACCEPTED,   /* commander card accepted */
	FG_BEEP_IGNORED,    /* commander card, nothing to cancel */
	FG_BEEP_DENIED,     /* unknown card */
};

int fg_act_init(void);

/* One-shot feedback beep (non-blocking, played by the actuator thread) */
void fg_act_beep(enum fg_beep b);

/* Reaction latency (ALARM decision -> outputs applied), microseconds */
struct fg_latency_stats {
	uint32_t count;
	uint32_t min_us;
	uint32_t max_us;
	uint32_t last_us;
	uint64_t sum_us;
};

void fg_act_latency(struct fg_latency_stats *out);
void fg_act_latency_reset(void);

#endif /* FG_ACTUATORS_H_ */
