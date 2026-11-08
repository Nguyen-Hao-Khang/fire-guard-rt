/*
 * Event log thread + RAM ring buffer
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "app/channels.h"
#include "app/storage.h"
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
static uint32_t saved_seq;      /* highest seq written to storage */
static K_MUTEX_DEFINE(log_lock);

void fg_event_log_add(uint8_t type, uint8_t a, uint8_t b, const uint8_t *uid)
{
	k_mutex_lock(&log_lock, K_FOREVER);
	struct fg_event *e = &ring[next_seq % FG_LOG_SIZE];

	*e = (struct fg_event){.seq = next_seq, .ts_ms = fg_now_ms(),
			       .boot = fg_storage_boot_count(), .type = type, .a = a, .b = b};
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

	/* skip slots that were not restored from storage */
	while (s < next_seq && ring[s % FG_LOG_SIZE].seq != s) {
		s++;
	}
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
		ret = (out->seq == s) ? 0 : 1;
	}
	k_mutex_unlock(&log_lock);
	return ret;
}

#if defined(CONFIG_ZTEST)
void fg_event_log_reset_ram(void)
{
	memset(ring, 0, sizeof(ring));
	next_seq = 1;
	sent_seq = 0;
	saved_seq = 0;
}
#endif

void fg_event_log_restore_head(const struct fg_event_log_head *h)
{
	next_seq = MAX(h->next_seq, 1U);
	sent_seq = MIN(h->sent_seq, next_seq - 1);
	saved_seq = next_seq - 1;
}

void fg_event_log_restore_entry(const struct fg_event *e)
{
	if (e->seq != 0) {
		ring[e->seq % FG_LOG_SIZE] = *e;
	}
}

/*
 * Mirror new events to storage. Runs in the log thread (priority 9): a
 * flash write stalls the cache of both cores, so it must never run in the
 * safety chain. Its duration is a blocking term in the response-time
 * analysis.
 */
void fg_event_log_flush(void)
{
	if (!IS_ENABLED(CONFIG_FG_PERSIST)) {
		return;
	}

	struct fg_event e;
	struct fg_event_log_head h;
	char key[8];

	k_mutex_lock(&log_lock, K_FOREVER);
	uint32_t from = MAX(saved_seq + 1, oldest_seq());
	uint32_t to = next_seq;

	h.next_seq = next_seq;
	h.sent_seq = sent_seq;
	k_mutex_unlock(&log_lock);

	for (uint32_t s = from; s < to; s++) {
		k_mutex_lock(&log_lock, K_FOREVER);
		e = ring[s % FG_LOG_SIZE];
		k_mutex_unlock(&log_lock);
		snprintf(key, sizeof(key), "ev/%u", s % FG_LOG_SIZE);
		if (fg_storage_save(key, &e, sizeof(e)) != 0) {
			LOG_WRN("event %u not saved", s);
			return;
		}
	}
	if (from < to) {
		fg_storage_save("evh", &h, sizeof(h));
		saved_seq = to - 1;
	}
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

	/* Publishers inherit this thread priority while notifying it */
	zbus_obs_attach_to_thread(&log_sub);

	while (1) {
		fg_event_log_flush();
		if (zbus_sub_wait(&log_sub, &chan, K_FOREVER) != 0) {
			continue;
		}
		if (chan == &chan_state) {
			struct fg_state_msg st;

			if (zbus_chan_read(chan, &st, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
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

			if (zbus_chan_read(chan, &f, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			fg_event_log_add(FG_EV_FAULT, f.sensor, f.kind | (f.active << 7), NULL);
		} else if (chan == &chan_rfid) {
			struct fg_rfid_msg c;

			if (zbus_chan_read(chan, &c, FG_CHAN_READ_TIMEOUT) != 0) {
				continue;
			}
			fg_event_log_add(FG_EV_CARD, c.authorized, 0, c.uid);
		}
	}
}
