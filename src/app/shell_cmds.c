/*
 * Diagnostic shell ("fg ..."), used for tests and demo
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "app/channels.h"
#include "app/config.h"
#include "app/diag.h"
#include "app/threads.h"
#include "actuators/actuators.h"
#include "fusion/fusion.h"
#include "safety/event_log.h"
#include "safety/fault_mgr.h"
#include "sensors/sensors.h"

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct fg_state_msg st;
	struct fg_adc_msg adc;
	struct fg_dht_msg dht;
	uint8_t faults = fg_fault_mask();

	zbus_chan_read(&chan_state, &st, K_MSEC(50));
	zbus_chan_read(&chan_adc, &adc, K_MSEC(50));
	zbus_chan_read(&chan_dht, &dht, K_MSEC(50));

	shell_print(sh, "state      : %s (cause %s)%s", fg_state_name(st.state),
		    fg_cause_name(st.cause), st.trapped ? " [PERSON IN ZONE]" : "");
	shell_print(sh, "light      : %d mV%s", adc.light_mv, adc.light_ok ? "" : " (invalid)");
	shell_print(sh, "gas        : %d mV%s", adc.gas_mv, adc.gas_ok ? "" : " (invalid)");
	shell_print(sh, "temp/hum   : %d.%d C / %d.%d %%%s", dht.temp_x10 / 10,
		    abs(dht.temp_x10 % 10), dht.hum_x10 / 10, dht.hum_x10 % 10,
		    dht.ok ? "" : " (no data)");
	shell_print(sh, "indicators : 0x%02x", fg_fusion_indicators());
	for (int s = 0; s < FG_SENSOR_COUNT; s++) {
		shell_print(sh, "sensor %-10s: %s", fg_sensor_name(s),
			    (faults & FG_FAULT_BIT(s)) ? "FAULT" : "ok");
	}
	return 0;
}

static int thread_by_name(const char *n)
{
	static const struct {
		const char *name;
		enum fg_thread_id id;
	} map[] = {
		{"adc", FG_TH_ADC},       {"dht", FG_TH_DHT},     {"ultra", FG_TH_ULTRA},
		{"rfid", FG_TH_RFID},     {"fusion", FG_TH_FUSION}, {"act", FG_TH_ACT},
	};

	for (size_t i = 0; i < ARRAY_SIZE(map); i++) {
		if (strcmp(map[i].name, n) == 0) {
			return map[i].id;
		}
	}
	return -1;
}

static int cmd_hang(const struct shell *sh, size_t argc, char **argv)
{
	int th = thread_by_name(argv[1]);

	if (th < 0) {
		shell_error(sh, "thread: adc|dht|ultra|rfid|fusion|act");
		return -EINVAL;
	}
	fg_hang_inject(th);
	shell_print(sh, "%s will hang at its next checkpoint", fg_thread_name(th));
	return 0;
}

static int cmd_card(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t v = strtoul(argv[1], NULL, 16);
	uint8_t uid[4] = {v >> 24, v >> 16, v >> 8, v};

	fg_rfid_inject(uid, 4);
	shell_print(sh, "card %08X injected (%s)", v,
		    fg_rfid_is_commander(uid, 4) ? "commander" : "unknown");
	return 0;
}

static int cmd_drill(const struct shell *sh, size_t argc, char **argv)
{
	struct fg_cmd_msg cmd = {.kind = FG_CMD_DRILL_START, .duration_s = 30};

	if (argc > 1 && strcmp(argv[1], "stop") == 0) {
		cmd.kind = FG_CMD_DRILL_STOP;
	} else if (argc > 1) {
		cmd.duration_s = (uint16_t)strtoul(argv[1], NULL, 10);
	}
	return zbus_chan_pub(&chan_cmd, &cmd, K_MSEC(50));
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	int err = fg_config_set(argv[1], strtol(argv[2], NULL, 10));

	if (err == -ENOENT) {
		shell_error(sh, "unknown key");
	} else if (err) {
		shell_error(sh, "value rejected (out of range)");
	}
	return err;
}

static int cmd_config(const struct shell *sh, size_t argc, char **argv)
{
	struct fg_config c;

	if (argc > 1) {
		if (strcmp(argv[1], "reset") != 0) {
			shell_error(sh, "usage: config [reset]");
			return -EINVAL;
		}
		return fg_config_reset();
	}
	fg_config_get(&c);
	shell_print(sh, "sample_period_ms  %u", c.sample_period_ms);
	shell_print(sh, "callin_period_s   %u", c.callin_period_s);
	shell_print(sh, "th_light_delta_mv %d", c.th_light_delta_mv);
	shell_print(sh, "th_gas_rise_mv    %d", c.th_gas_rise_mv);
	shell_print(sh, "th_gas_warn_mv    %d", c.th_gas_warn_mv);
	shell_print(sh, "th_gas_danger_mv  %d", c.th_gas_danger_mv);
	shell_print(sh, "th_temp_max_x10   %d", c.th_temp_max_x10);
	shell_print(sh, "th_temp_rise_x10  %d", c.th_temp_rise_x10);
	shell_print(sh, "confirm_window_ms %u", c.confirm_window_ms);
	return 0;
}

static int cmd_latency(const struct shell *sh, size_t argc, char **argv)
{
	struct fg_latency_stats l;

	if (argc > 1 && strcmp(argv[1], "reset") == 0) {
		fg_act_latency_reset();
		return 0;
	}
	fg_act_latency(&l);
	shell_print(sh, "reaction latency (decision -> outputs): n=%u min=%u us max=%u us "
			"avg=%u us last=%u us",
		    l.count, l.min_us, l.max_us, l.count ? (uint32_t)(l.sum_us / l.count) : 0,
		    l.last_us);
	return 0;
}

static int __maybe_unused cmd_threads(const struct shell *sh, size_t argc, char **argv)
{
	fg_diag_report();
	return 0;
}

static int cmd_log(const struct shell *sh, size_t argc, char **argv)
{
	struct fg_event e;
	int n = fg_event_log_count();

	for (int i = 0; i < n; i++) {
		int r = fg_event_log_get(i, &e);

		if (r < 0) {
			break;
		}
		if (r > 0) {
			continue; /* slot not restored from storage */
		}
		shell_print(sh, "#%u boot %u %8u ms %-8s a=%u b=0x%02x", e.seq, e.boot, e.ts_ms,
			    fg_event_type_name(e.type), e.a, e.b);
	}
	return 0;
}

SHELL_SUBCMD_SET_CREATE(fg_cmds, (fg));
SHELL_CMD_REGISTER(fg, &fg_cmds, "FireGuard-RT commands", NULL);

SHELL_SUBCMD_ADD((fg), status, NULL, "System state, sensors, faults", cmd_status, 1, 0);
SHELL_SUBCMD_ADD((fg), config, NULL, "config [reset] (show or restore defaults)", cmd_config, 1,
		 1);
SHELL_SUBCMD_ADD((fg), set, NULL, "set <key> <value>", cmd_set, 3, 0);
SHELL_SUBCMD_ADD((fg), hang, NULL, "hang <adc|dht|ultra|rfid|fusion|act> (fault injection)",
		 cmd_hang, 2, 0);
SHELL_SUBCMD_ADD((fg), card, NULL, "card <uid hex8> (simulate RFID card)", cmd_card, 2, 0);
SHELL_SUBCMD_ADD((fg), drill, NULL, "drill [seconds|stop]", cmd_drill, 1, 1);
SHELL_SUBCMD_ADD((fg), latency, NULL, "latency [reset]", cmd_latency, 1, 1);
SHELL_SUBCMD_ADD((fg), log, NULL, "Dump event log", cmd_log, 1, 0);
SHELL_SUBCMD_COND_ADD(IS_ENABLED(CONFIG_FG_DIAG_STATS), (fg), threads, NULL,
		      "Thread CPU usage and data flow", cmd_threads, 1, 0);
