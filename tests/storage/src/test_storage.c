/*
 * Persistence of configuration and event log across a simulated reset
 *
 * A reset is emulated by clearing the RAM copy and loading the settings
 * subtree again from the flash simulator.
 */
#include <zephyr/ztest.h>
#include <zephyr/settings/settings.h>
#include "app/config.h"
#include "app/storage.h"
#include "safety/event_log.h"

static void reload(void)
{
	fg_event_log_reset_ram();
	zassert_ok(settings_load_subtree("fg"));
}

static void *setup(void)
{
	zassert_ok(fg_storage_init());
	return NULL;
}

ZTEST_SUITE(storage, NULL, setup, NULL, NULL, NULL);

ZTEST(storage, test_config_survives_reset)
{
	const struct fg_config defaults = FG_CONFIG_DEFAULTS;
	struct fg_config c;

	zassert_ok(fg_config_set("callin_period_s", 120));
	zassert_ok(zbus_chan_pub(&chan_config, &defaults, K_MSEC(100)));
	fg_config_get(&c);
	zassert_equal(c.callin_period_s, 60);

	reload();
	fg_config_get(&c);
	zassert_equal(c.callin_period_s, 120, "stored value not restored");

	zassert_ok(fg_config_reset());
	reload();
	fg_config_get(&c);
	zassert_equal(c.callin_period_s, 60, "reset did not erase stored value");
}

ZTEST(storage, test_rejected_config_not_saved)
{
	struct fg_config c;

	zassert_equal(fg_config_set("sample_period_ms", 5000), -EINVAL);
	reload();
	fg_config_get(&c);
	zassert_equal(c.sample_period_ms, 1000);
}

ZTEST(storage, test_events_survive_reset)
{
	struct fg_event e;
	int before = fg_event_log_count();

	fg_event_log_add(FG_EV_STATE, 2, 1, NULL);
	fg_event_log_add(FG_EV_FAULT, 2, 0x82, NULL);
	fg_event_log_flush();

	reload();
	zassert_equal(fg_event_log_count(), before + 2);
	zassert_equal(fg_event_log_get(before + 1, &e), 0);
	zassert_equal(e.type, FG_EV_FAULT);
	zassert_equal(e.b, 0x82);
	zassert_equal(e.boot, fg_storage_boot_count());
}

ZTEST(storage, test_unsent_events_delivered_after_reset)
{
	struct fg_event e;

	/* acknowledge everything so far */
	while (fg_event_log_peek_unsent(&e)) {
		fg_event_log_mark_sent(e.seq);
	}
	fg_event_log_add(FG_EV_CARD, 1, 0, (const uint8_t[]){0xDE, 0xAD, 0xBE, 0xEF});
	fg_event_log_flush();

	reload();
	zassert_true(fg_event_log_peek_unsent(&e));
	zassert_equal(e.type, FG_EV_CARD);
	zassert_equal(e.uid[0], 0xDE);
}

ZTEST(storage, test_ring_wraps)
{
	struct fg_event e;

	for (int i = 0; i < FG_LOG_SIZE + 10; i++) {
		fg_event_log_add(FG_EV_STATE, i & 0xFF, 0, NULL);
	}
	fg_event_log_flush();

	reload();
	zassert_equal(fg_event_log_count(), FG_LOG_SIZE);
	zassert_equal(fg_event_log_get(FG_LOG_SIZE - 1, &e), 0);
	zassert_equal(e.a, (FG_LOG_SIZE + 9) & 0xFF);
}
