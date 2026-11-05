/*
 * Event log (SRS FR-07)
 *
 * Ring buffer of events consumed by the comm module (send + mark sent).
 * TODO(persist): back the ring with a flash partition (NVS/LittleFS).
 */
#ifndef FG_EVENT_LOG_H_
#define FG_EVENT_LOG_H_

#include <stdbool.h>
#include <stdint.h>

#define FG_LOG_SIZE 64

enum fg_event_type {
	FG_EV_BOOT = 1,
	FG_EV_STATE,     /* a = new state, b = cause */
	FG_EV_FAULT,     /* a = sensor, b = kind | (active << 7) */
	FG_EV_CARD,      /* a = authorized, b = 0, uid in data */
	FG_EV_TRAPPED,   /* a = trapped */
};

struct fg_event {
	uint32_t seq;
	uint32_t ts_ms;
	uint8_t type;
	uint8_t a;
	uint8_t b;
	uint8_t uid[4];
};

void fg_event_log_add(uint8_t type, uint8_t a, uint8_t b, const uint8_t *uid);

/* Oldest event not yet acknowledged by the comm module */
bool fg_event_log_peek_unsent(struct fg_event *out);
void fg_event_log_mark_sent(uint32_t seq);

/* Iterate the last n events (newest last) for the shell */
int fg_event_log_get(int index_from_oldest, struct fg_event *out);
int fg_event_log_count(void);

const char *fg_event_type_name(uint8_t type);

#endif /* FG_EVENT_LOG_H_ */
