/*
 * Event log thread + RAM ring buffer
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "safety/event_log.h"

LOG_MODULE_REGISTER(fg_log, LOG_LEVEL_INF);

ZBUS_SUBSCRIBER_DEFINE(log_sub, 16);
ZBUS_CHAN_ADD_OBS(chan_state, log_sub, 5);
ZBUS_CHAN_ADD_OBS(chan_fault, log_sub, 5);
ZBUS_CHAN_ADD_OBS(chan_rfid, log_sub, 5);
ZBUS_CHAN_ADD_OBS(chan_presence, log_sub, 5);

static struct fg_event ring[FG_LOG_SIZE];
static uint32_t next_seq = 1;   /* seq of the next event to write */
static uint32_t sent_seq;       /* highest seq acknowledged */
static K_MUTEX_DEFINE(log_lock);

void fg_event_log_add(uint8_t type, uint8_t a, uint8_t b, const uint8_t *uid)
{
	k_mutex_lock(&log_lock, K_FOREVER);
	struct fg_event *e = &ring[next_seq % FG_LOG_SIZE];

	*e = (struct fg_event){.seq = next_seq, .ts_ms = fg_now_ms(), .type = type,
			       .a = a, .b = b};
	if (uid) {
		memcpy(e->uid, uid, sizeof(e->uid));
	}
	next_seq++;
	k_mutex_unlock(&log_lock);
}

static uint32_t oldest_seq(void)
{
	return next_seq > FG_LOG_SIZE ? next_seq - FG_LOG_SIZE : 1;
}

bool fg_event_log_peek_unsent(struct fg_event *out)
{
	bool found = false;

	k_mutex_lock(&log_lock, K_FOREVER);
	uint32_t s = MAX(sent_seq + 1, oldest_seq());

	if (s < next_seq) {
		*out = ring[s % FG_LOG_SIZE];
		found = true;
	}
	k_mutex_unlock(&log_lock);
	return found;
}

void fg_event_log_mark_sent(uint32_t seq)
{
	k_mutex_lock(&log_lock, K_FOREVER);
	sent_seq = MAX(sent_seq, seq);
	k_mutex_unlock(&log_lock);
}

int fg_event_log_count(void)
{
	return (int)(next_seq - oldest_seq());
}

int fg_event_log_get(int idx, struct fg_event *out)
{
	int ret = -1;

	k_mutex_lock(&log_lock, K_FOREVER);
	uint32_t s = oldest_seq() + idx;

	if (idx >= 0 && s < next_seq) {
		*out = ring[s % FG_LOG_SIZE];
		ret = 0;
	}
	k_mutex_unlock(&log_lock);
	return ret;
}

const char *fg_event_type_name(uint8_t type)
{
	switch (type) {
	case FG_EV_BOOT: return "boot";
	case FG_EV_STATE: return "state";
	case FG_EV_FAULT: return "fault";
	case FG_EV_CARD: return "card";
	case FG_EV_TRAPPED: return "trapped";
	default: return "?";
	}
}

void fg_log_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;
	uint8_t last_state = 0xFF;
	bool last_trapped = false;

	while (1) {
		if (zbus_sub_wait(&log_sub, &chan, K_FOREVER) != 0) {
			continue;
		}
		if (chan == &chan_state) {
			struct fg_state_msg st;

			zbus_chan_read(chan, &st, K_NO_WAIT);
			if (st.state != last_state) {
				last_state = st.state;
				fg_event_log_add(FG_EV_STATE, st.state, st.cause, NULL);
			}
			if (st.trapped != last_trapped) {
				last_trapped = st.trapped;
				fg_event_log_add(FG_EV_TRAPPED, st.trapped, 0, NULL);
			}
		} else if (chan == &chan_fault) {
			struct fg_fault_msg f;

			zbus_chan_read(chan, &f, K_NO_WAIT);
			fg_event_log_add(FG_EV_FAULT, f.sensor, f.kind | (f.active << 7), NULL);
		} else if (chan == &chan_rfid) {
			struct fg_rfid_msg c;

			zbus_chan_read(chan, &c, K_NO_WAIT);
			fg_event_log_add(FG_EV_CARD, c.authorized, 0, c.uid);
		}
	}
}
