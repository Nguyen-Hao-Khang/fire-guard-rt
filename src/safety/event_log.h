/*
 * Event log (SRS FR-07)
 *
 * Ring buffer of events consumed by the comm module (send + mark sent).
 * With CONFIG_FG_PERSIST the ring is mirrored to flash by the log thread,
 * so events survive a reset and unsent ones are delivered afterwards.
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
	uint32_t ts_ms;   /* uptime within the boot below */
	uint16_t boot;    /* boot counter from storage, 0 without storage */
	uint8_t type;
	uint8_t a;
	uint8_t b;
	uint8_t uid[4];
};

/* Persisted ring position */
struct fg_event_log_head {
	uint32_t next_seq;
	uint32_t sent_seq;
};

void fg_event_log_add(uint8_t type, uint8_t a, uint8_t b, const uint8_t *uid);

/* Oldest event not yet acknowledged by the comm module */
bool fg_event_log_peek_unsent(struct fg_event *out);
void fg_event_log_mark_sent(uint32_t seq);

/* Iterate the last n events (newest last) for the shell */
int fg_event_log_get(int index_from_oldest, struct fg_event *out);
int fg_event_log_count(void);

const char *fg_event_type_name(uint8_t type);

/* Write events not yet in storage (called by the log thread) */
void fg_event_log_flush(void);

/* Test hook: forget the RAM copy, as after a reset */
void fg_event_log_reset_ram(void);

/* Called by the storage module while loading */
void fg_event_log_restore_head(const struct fg_event_log_head *h);
void fg_event_log_restore_entry(const struct fg_event *e);

#endif /* FG_EVENT_LOG_H_ */
