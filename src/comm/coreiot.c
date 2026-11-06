/*
 * Wi-Fi + MQTT client for CoreIoT (ThingsBoard device API)
 *
 * Runs at the lowest application priority. It only *reads* zbus channels
 * and publishes chan_config / chan_cmd after validation, so a slow or broken
 * network can never delay the safety chain.
 *
 *  telemetry  v1/devices/me/telemetry          adaptive period (FR-07)
 *  attributes v1/devices/me/attributes         shared attrs in, client attrs out
 *  attr req   v1/devices/me/attributes/request/1  (initial sync of shared attrs)
 *  rpc        v1/devices/me/rpc/request/{id}   startDrill / stopDrill
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>
#include "app/channels.h"
#include "app/config.h"
#include "safety/event_log.h"

LOG_MODULE_REGISTER(fg_comm, LOG_LEVEL_INF);

#define TOPIC_TELEMETRY  "v1/devices/me/telemetry"
#define TOPIC_ATTR       "v1/devices/me/attributes"
#define TOPIC_ATTR_REQ   "v1/devices/me/attributes/request/1"
#define TOPIC_ATTR_RESP  "v1/devices/me/attributes/response/+"
#define TOPIC_RPC_REQ    "v1/devices/me/rpc/request/+"
#define TOPIC_RPC_RESP   "v1/devices/me/rpc/response/"

#define BUF_SIZE 512

ZBUS_SUBSCRIBER_DEFINE(comm_sub, 8);
ZBUS_CHAN_ADD_OBS(chan_state, comm_sub, 9);

static K_SEM_DEFINE(net_up, 0, 1);
static struct net_mgmt_event_callback ip_cb;

static struct mqtt_client client;
static struct sockaddr_storage broker;
static uint8_t rx_buf[BUF_SIZE];
static uint8_t tx_buf[BUF_SIZE];
static uint8_t payload[BUF_SIZE];
static char client_id[24];
static bool connected;

/* Minimal JSON helpers */

static bool json_get_int(const char *json, const char *key, int32_t *out)
{
	char pat[40];
	const char *p;

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) {
		return false;
	}
	p = strchr(p + strlen(pat), ':');
	if (!p) {
		return false;
	}
	p++;
	while (*p == ' ' || *p == '"') {
		p++;
	}
	char *end;
	long v = strtol(p, &end, 10);

	if (end == p) {
		return false;
	}
	*out = (int32_t)v;
	return true;
}

/* Wi-Fi */

static void ip_handler(struct net_mgmt_event_callback *cb, uint64_t event,
		       struct net_if *iface)
{
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&net_up);
	}
}

static int wifi_connect(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_connect_req_params p = {
		.ssid = (const uint8_t *)CONFIG_FG_WIFI_SSID,
		.ssid_length = strlen(CONFIG_FG_WIFI_SSID),
		.psk = (const uint8_t *)CONFIG_FG_WIFI_PSK,
		.psk_length = strlen(CONFIG_FG_WIFI_PSK),
		.security = strlen(CONFIG_FG_WIFI_PSK) ? WIFI_SECURITY_TYPE_PSK
						       : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};

	LOG_INF("Wi-Fi: connecting to \"%s\"", CONFIG_FG_WIFI_SSID);
	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
}

/* MQTT */

static int publish(const char *topic, const char *msg, enum mqtt_qos qos)
{
	struct mqtt_publish_param p = {
		.message.topic.qos = qos,
		.message.topic.topic.utf8 = (const uint8_t *)topic,
		.message.topic.topic.size = strlen(topic),
		.message.payload.data = (uint8_t *)msg,
		.message.payload.len = strlen(msg),
		.message_id = sys_rand16_get(),
	};

	return connected ? mqtt_publish(&client, &p) : -ENOTCONN;
}

static void apply_attributes(const char *json)
{
	static const char *const keys[] = {
		"sample_period_ms", "callin_period_s", "th_light_delta_mv", "th_gas_rise_mv",
		"th_gas_warn_mv",   "th_gas_danger_mv", "th_temp_max_x10", "th_temp_rise_x10",
		"confirm_window_ms",
	};

	for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
		int32_t v;

		if (json_get_int(json, keys[i], &v)) {
			int err = fg_config_set(keys[i], v);
			char ack[96];

			/* report the accepted / rejected value back as a client attribute */
			snprintf(ack, sizeof(ack), "{\"cfg_%s\":\"%s\"}", keys[i],
				 err ? "rejected" : "applied");
			publish(TOPIC_ATTR, ack, MQTT_QOS_0_AT_MOST_ONCE);
		}
	}
}

static void handle_rpc(const char *topic, const char *json)
{
	const char *id = strrchr(topic, '/');
	char resp_topic[64];
	char resp[64];
	struct fg_cmd_msg cmd = {0};
	int32_t dur = 30;

	if (!id) {
		return;
	}
	if (strstr(json, "\"startDrill\"")) {
		if (!json_get_int(json, "duration_s", &dur)) {
			(void)json_get_int(json, "params", &dur);
		}
		cmd.kind = FG_CMD_DRILL_START;
		cmd.duration_s = CLAMP(dur, 5, 600);
	} else if (strstr(json, "\"stopDrill\"")) {
		cmd.kind = FG_CMD_DRILL_STOP;
	}

	int err = cmd.kind ? zbus_chan_pub(&chan_cmd, &cmd, K_MSEC(100)) : -ENOTSUP;

	snprintf(resp_topic, sizeof(resp_topic), "%s%s", TOPIC_RPC_RESP, id + 1);
	snprintf(resp, sizeof(resp), "{\"result\":\"%s\"}", err ? "rejected" : "ok");
	publish(resp_topic, resp, MQTT_QOS_0_AT_MOST_ONCE);
}

static void mqtt_evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connected = (evt->result == 0);
		LOG_INF("MQTT %s", connected ? "connected to CoreIoT" : "connection refused");
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		LOG_WRN("MQTT disconnected");
		break;
	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &evt->param.publish;
		char topic[96];
		size_t len = MIN(p->message.payload.len, sizeof(payload) - 1);

		snprintf(topic, sizeof(topic), "%.*s", p->message.topic.topic.size,
			 p->message.topic.topic.utf8);
		if (mqtt_read_publish_payload_blocking(c, payload, len) < 0) {
			break;
		}
		payload[len] = '\0';
		LOG_INF("rx %s %s", topic, payload);

		if (strncmp(topic, "v1/devices/me/rpc/request/", 26) == 0) {
			handle_rpc(topic, (const char *)payload);
		} else if (strncmp(topic, TOPIC_ATTR, strlen(TOPIC_ATTR)) == 0) {
			apply_attributes((const char *)payload);
		}
		break;
	}
	default:
		break;
	}
}

static int resolve_broker(void)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *res;
	int err = zsock_getaddrinfo(CONFIG_FG_COREIOT_HOST, NULL, &hints, &res);

	if (err) {
		LOG_ERR("DNS %s failed: %d", CONFIG_FG_COREIOT_HOST, err);
		return -EHOSTUNREACH;
	}
	struct sockaddr_in *b = (struct sockaddr_in *)&broker;

	*b = *(struct sockaddr_in *)res->ai_addr;
	b->sin_port = htons(CONFIG_FG_COREIOT_PORT);
	zsock_freeaddrinfo(res);
	return 0;
}

static int mqtt_start(void)
{
	static struct mqtt_utf8 user;
	struct mqtt_topic topics[] = {
		{.topic = {.utf8 = TOPIC_ATTR, .size = sizeof(TOPIC_ATTR) - 1}, .qos = 0},
		{.topic = {.utf8 = TOPIC_ATTR_RESP, .size = sizeof(TOPIC_ATTR_RESP) - 1}, .qos = 0},
		{.topic = {.utf8 = TOPIC_RPC_REQ, .size = sizeof(TOPIC_RPC_REQ) - 1}, .qos = 0},
	};
	struct mqtt_subscription_list subs = {
		.list = topics, .list_count = ARRAY_SIZE(topics), .message_id = 1,
	};
	int err;

	mqtt_client_init(&client);
	snprintf(client_id, sizeof(client_id), "fireguard-%08x", sys_rand32_get());
	user.utf8 = (const uint8_t *)CONFIG_FG_COREIOT_TOKEN;
	user.size = strlen(CONFIG_FG_COREIOT_TOKEN);

	client.broker = &broker;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = (const uint8_t *)client_id;
	client.client_id.size = strlen(client_id);
	client.user_name = &user;
	client.password = NULL;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;

	err = mqtt_connect(&client);
	if (err) {
		LOG_ERR("mqtt_connect: %d", err);
		return err;
	}

	/* wait for CONNACK */
	struct zsock_pollfd fd = {.fd = client.transport.tcp.sock, .events = ZSOCK_POLLIN};

	if (zsock_poll(&fd, 1, 5000) <= 0 || mqtt_input(&client) < 0 || !connected) {
		mqtt_abort(&client);
		return -ECONNREFUSED;
	}

	mqtt_subscribe(&client, &subs);
	publish(TOPIC_ATTR_REQ,
		"{\"sharedKeys\":\"sample_period_ms,callin_period_s,th_light_delta_mv,"
		"th_gas_rise_mv,th_gas_warn_mv,th_gas_danger_mv,th_temp_max_x10,"
		"th_temp_rise_x10,confirm_window_ms\"}",
		MQTT_QOS_0_AT_MOST_ONCE);
	publish(TOPIC_ATTR, "{\"fw_version\":\"0.1.0\",\"device\":\"FireGuard-RT\"}",
		MQTT_QOS_0_AT_MOST_ONCE);
	return 0;
}

/* Telemetry */

static void send_telemetry(void)
{
	struct fg_state_msg st;
	struct fg_adc_msg adc;
	struct fg_dht_msg dht;
	char msg[256];

	zbus_chan_read(&chan_state, &st, K_MSEC(10));
	zbus_chan_read(&chan_adc, &adc, K_MSEC(10));
	zbus_chan_read(&chan_dht, &dht, K_MSEC(10));

	snprintf(msg, sizeof(msg),
		 "{\"state\":\"%s\",\"light\":%d,\"gas\":%d,\"temp\":%d.%d,\"hum\":%d.%d,"
		 "\"faults\":%u,\"trapped\":%s}",
		 fg_state_name(st.state), adc.light_mv, adc.gas_mv, dht.temp_x10 / 10,
		 abs(dht.temp_x10 % 10), dht.hum_x10 / 10, dht.hum_x10 % 10, st.faults,
		 st.trapped ? "true" : "false");
	publish(TOPIC_TELEMETRY, msg, MQTT_QOS_0_AT_MOST_ONCE);
}

static void flush_events(void)
{
	struct fg_event e;
	char msg[160];

	while (connected && fg_event_log_peek_unsent(&e)) {
		const char *detail = "";
		char uid[12] = "";

		if (e.type == FG_EV_STATE) {
			detail = fg_state_name(e.a);
		} else if (e.type == FG_EV_FAULT) {
			detail = fg_sensor_name(e.a);
		} else if (e.type == FG_EV_CARD) {
			snprintf(uid, sizeof(uid), "%02X%02X%02X%02X", e.uid[0], e.uid[1],
				 e.uid[2], e.uid[3]);
			detail = uid;
		}
		snprintf(msg, sizeof(msg),
			 "{\"event\":\"%s\",\"detail\":\"%s\",\"a\":%u,\"b\":%u,\"seq\":%u,"
			 "\"uptime_ms\":%u}",
			 fg_event_type_name(e.type), detail, e.a, e.b, e.seq, e.ts_ms);
		if (publish(TOPIC_TELEMETRY, msg, MQTT_QOS_1_AT_LEAST_ONCE) != 0) {
			break;
		}
		fg_event_log_mark_sent(e.seq);
	}
}

static uint32_t report_period_ms(void)
{
	struct fg_state_msg st;
	struct fg_config cfg;

	zbus_chan_read(&chan_state, &st, K_MSEC(10));
	fg_config_get(&cfg);
	bool calm = st.state == FG_STATE_NORMAL || st.state == FG_STATE_WARMUP ||
		    st.state == FG_STATE_INIT;

	return calm ? cfg.callin_period_s * 1000U : cfg.sample_period_ms;
}

void fg_comm_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t backoff_ms = 2000;

	/* Publishers inherit this thread priority while notifying it */
	zbus_obs_attach_to_thread(&comm_sub);
	net_mgmt_init_event_callback(&ip_cb, ip_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ip_cb);

	while (1) {
		/* (re)connect Wi-Fi + broker with exponential back-off */
		if (k_sem_take(&net_up, K_NO_WAIT) != 0 && !connected) {
			wifi_connect();
			if (k_sem_take(&net_up, K_SECONDS(20)) != 0) {
				LOG_WRN("Wi-Fi: no IP yet, retry in %u ms", backoff_ms);
				k_msleep(backoff_ms);
				backoff_ms = MIN(backoff_ms * 2, 60000);
				continue;
			}
		}
		if (resolve_broker() || mqtt_start()) {
			k_msleep(backoff_ms);
			backoff_ms = MIN(backoff_ms * 2, 60000);
			k_sem_give(&net_up); /* IP is still there, retry MQTT only */
			continue;
		}
		backoff_ms = 2000;

		uint32_t next_report = 0;

		while (connected) {
			const struct zbus_channel *chan;
			uint32_t now = fg_now_ms();
			struct zsock_pollfd fd = {.fd = client.transport.tcp.sock,
						  .events = ZSOCK_POLLIN};

			if ((int32_t)(now - next_report) >= 0) {
				send_telemetry();
				next_report = now + report_period_ms();
			}
			flush_events();

			/* state change -> immediate report (and switch to fast period) */
			if (zbus_sub_wait(&comm_sub, &chan, K_NO_WAIT) == 0) {
				next_report = now;
			}

			if (zsock_poll(&fd, 1, 100) > 0 && mqtt_input(&client) < 0) {
				break;
			}
			mqtt_live(&client);
		}
		mqtt_abort(&client);
		connected = false;
		LOG_WRN("MQTT session lost, reconnecting");
	}
}
