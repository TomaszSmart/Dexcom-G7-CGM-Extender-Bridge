/*
 * BLE central role for the Dexcom G7 bridge.
 *
 * Scans for a G7 transmitter (advertised service FEBC, GAP name "DXCM*"/"DX02*"),
 * connects, discovers the CGM service, authenticates with the applicator pairing
 * code (app-layer EC-JPAKE over GATT, g7_auth.c -- see PROTOCOL_RESEARCH.md
 * section 2a), bonds when the sensor asks for it, and logs glucose readings from
 * the `control` characteristic.
 *
 * Safety: matching the advertised service+name prefix only proves "this is *a*
 * G7", not "this is *my* G7" -- other sensors can be in range. The bridge will
 * not connect to anything until a specific sensor id is configured over the
 * shell (`g7 set_id <suffix>`, see g7_config.h/g7_shell.c); until then it only
 * logs candidates. It also needs the pairing code (`g7 set_code 1234`).
 *
 * After each reading it asks the G7 to backfill any gap (up to 24 h), keeps the
 * results in g7_history, and serves them to xDrip+ as a standard Bluetooth CGM
 * service (g7_cgms.c).
 *
 * Not yet implemented: extended version request (opcode 0x52).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <bluetooth/gatt_dm.h>

#include "g7_auth.h"
#include "g7_bridge.h"
#include "g7_cgms.h"
#include "g7_config.h"
#include "g7_history.h"
#include "g7_protocol.h"

LOG_MODULE_REGISTER(ble_bridge, LOG_LEVEL_INF);

static const struct bt_uuid_128 cgm_service_uuid =
	BT_UUID_INIT_128(G7_CGM_SERVICE_UUID);
static const struct bt_uuid_128 char_control_uuid =
	BT_UUID_INIT_128(G7_CHAR_CONTROL_UUID);
static const struct bt_uuid_128 char_authentication_uuid =
	BT_UUID_INIT_128(G7_CHAR_AUTHENTICATION_UUID);
static const struct bt_uuid_128 char_extra_data_uuid =
	BT_UUID_INIT_128(G7_CHAR_EXTRA_DATA_UUID);
static const struct bt_uuid_128 char_backfill_uuid =
	BT_UUID_INIT_128(G7_CHAR_BACKFILL_UUID);

static struct bt_conn *default_conn;
static struct bt_gatt_subscribe_params auth_subscribe_params;
static struct bt_gatt_subscribe_params control_subscribe_params;

/*
 * --- scanning: look for GAP name "DXCM*"/"DX02*" alongside adv service FEBC ---
 *
 * That alone only tells us "this is *a* G7", not "this is *my* G7" -- more than
 * one sensor (yours, a household member's, a leftover old one) can be in range
 * at once. So we never auto-connect until a sensor id filter is configured via
 * the `g7 set_id <suffix>` shell command (see g7_config.h); until then we only
 * log what we see, so you can read the right suffix off the log.
 */

#define ADV_NAME_MAX_LEN 32

struct adv_match {
	bool has_service;
	bool has_matching_name;
	char name[ADV_NAME_MAX_LEN];
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
	struct adv_match *match = user_data;

	switch (data->type) {
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		for (size_t i = 0; i + 1 < data->data_len; i += 2) {
			uint16_t uuid = sys_get_le16(&data->data[i]);

			if (uuid == G7_ADV_SERVICE_UUID16) {
				match->has_service = true;
			}
		}
		break;
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE: {
		size_t len = MIN(data->data_len, sizeof(match->name) - 1);

		memcpy(match->name, data->data, len);
		match->name[len] = '\0';

		if (data->data_len >= 4 &&
		    (memcmp(data->data, "DXCM", 4) == 0 ||
		     memcmp(data->data, "DX02", 4) == 0)) {
			match->has_matching_name = true;
		}
		break;
	}
	default:
		break;
	}

	return true; /* keep parsing remaining AD structures */
}

/* True if `name` ends with `suffix` (case-sensitive, byte-for-byte). */
static bool name_has_suffix(const char *name, const char *suffix)
{
	size_t name_len = strlen(name);
	size_t suffix_len = strlen(suffix);

	if (suffix_len == 0 || suffix_len > name_len) {
		return false;
	}

	return memcmp(name + name_len - suffix_len, suffix, suffix_len) == 0;
}

/* Avoid re-logging the same candidate on every single advertisement (several/sec). */
static bt_addr_le_t last_logged_addr;
static int64_t last_logged_uptime = -1;

static bool should_log_candidate(const bt_addr_le_t *addr)
{
	int64_t now = k_uptime_get();

	if (bt_addr_le_eq(addr, &last_logged_addr) && (now - last_logged_uptime) < 5000) {
		return false;
	}

	last_logged_addr = *addr;
	last_logged_uptime = now;
	return true;
}

static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void security_changed(struct bt_conn *conn, bt_security_t level,
			      enum bt_security_err err);

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void start_scan(void);

/* The G7 wakes every 5 minutes and drops the link itself after a reading. Stay
 * away for most of that cycle after a reading (reconnecting in the same wake
 * window just fails), and briefly after a failed attempt so we do not hammer it.
 */
#define RETRY_BACKOFF_MS 30000
#define QUICK_RETRY_MS   1000
#define QUICK_RETRIES    3
#define NEXT_CYCLE_MS    (4 * 60 * 1000)
static int64_t retry_after;
static int quick_retries_left = QUICK_RETRIES;
static bool got_reading;

/* --- G7s in range, and the search for the sensor a new pairing code belongs to --- */

#define SEARCH_WINDOW_MS (30 * 60 * 1000)
#define SEARCH_MAX_FAILS 2

struct seen_entry {
	bt_addr_le_t addr;
	char name[9];
	int8_t rssi;
	int64_t last_ms;
	uint8_t fails;   /* search attempts that ended without a verdict */
	bool tried;      /* the pairing code does not fit this one */
};

static struct seen_entry seen[G7_BRIDGE_MAX_SEEN];
static int64_t search_until;
static bool connecting_for_search;
static char connecting_name[9];

/* Last reading from the sensor (even if not usable), for the UI. */
static struct {
	bool valid;
	uint16_t mgdl;
	uint8_t state;
	int8_t trend;
	bool trend_valid;
	int64_t at_ms;   /* when it was measured, uptime */
} last_reading;

static struct seen_entry *seen_update(const bt_addr_le_t *addr, const char *name, int8_t rssi)
{
	struct seen_entry *slot = &seen[0];

	for (size_t i = 0; i < ARRAY_SIZE(seen); i++) {
		if (seen[i].last_ms && bt_addr_le_eq(&seen[i].addr, addr)) {
			slot = &seen[i];
			goto found;
		}
		if (seen[i].last_ms < slot->last_ms) {
			slot = &seen[i]; /* empty or least recently seen */
		}
	}
	memset(slot, 0, sizeof(*slot));
	bt_addr_le_copy(&slot->addr, addr);
found:
	strncpy(slot->name, name, sizeof(slot->name) - 1);
	slot->rssi = rssi;
	slot->last_ms = k_uptime_get();
	return slot;
}

static struct seen_entry *seen_find(const bt_addr_le_t *addr)
{
	for (size_t i = 0; i < ARRAY_SIZE(seen); i++) {
		if (seen[i].last_ms && bt_addr_le_eq(&seen[i].addr, addr)) {
			return &seen[i];
		}
	}
	return NULL;
}

static bool search_active(void)
{
	return g7_config_get_id()[0] == '\0' && g7_config_get_code()[0] != '\0' &&
	       k_uptime_get() < search_until;
}

void g7_bridge_start_search(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(seen); i++) {
		seen[i].tried = false;
		seen[i].fails = 0;
	}
	search_until = k_uptime_get() + SEARCH_WINDOW_MS;
	LOG_INF("Searching for the sensor that pairing code %s belongs to (30 min)",
		g7_config_get_code());
}

int g7_bridge_repair_sensor(void)
{
	const char *id = g7_config_get_id();
	int removed = 0;

	for (size_t i = 0; i < ARRAY_SIZE(seen); i++) {
		if (seen[i].last_ms && id[0] && name_has_suffix(seen[i].name, id) &&
		    bt_unpair(BT_ID_DEFAULT, &seen[i].addr) == 0) {
			removed++;
		}
	}
	g7_config_clear_key();
	return removed;
}

void g7_bridge_release_sensor(void)
{
	g7_config_set_id("");
	g7_config_set_code("");
	search_until = 0;
	last_reading.valid = false;
	if (default_conn) {
		LOG_INF("Releasing the sensor: disconnecting");
		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

void g7_bridge_get_status(struct g7_bridge_status *st)
{
	int64_t now = k_uptime_get();
	const char *id = g7_config_get_id();

	memset(st, 0, sizeof(*st));
	st->searching = search_active();
	if (st->searching) {
		st->search_left_s = (uint16_t)((search_until - now) / 1000);
	}
	if (last_reading.valid) {
		st->have_reading = true;
		st->mgdl = last_reading.mgdl;
		st->state = last_reading.state;
		st->trend_tenths = last_reading.trend;
		st->trend_valid = last_reading.trend_valid;
		st->reading_age_s = (uint32_t)((now - last_reading.at_ms) / 1000);
	}
	for (size_t i = 0; i < ARRAY_SIZE(seen); i++) {
		if (!seen[i].last_ms) {
			continue;
		}

		struct g7_seen_sensor *o = &st->seen[st->n_seen++];

		strncpy(o->name, seen[i].name, sizeof(o->name) - 1);
		o->rssi = seen[i].rssi;
		o->seen_s_ago = (uint16_t)MIN((now - seen[i].last_ms) / 1000, UINT16_MAX);
		o->tried = seen[i].tried;
		o->paired = id[0] && name_has_suffix(seen[i].name, id);
	}
}

static void scan_recv_cb(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	struct adv_match match = {0};

	if (default_conn || k_uptime_get() < retry_after) {
		return; /* connecting/connected, or backing off after a failure */
	}

	bt_data_parse(buf, ad_parse_cb, &match);

	if (!(match.has_service && match.has_matching_name)) {
		return;
	}

	struct seen_entry *entry = seen_update(info->addr, match.name, info->rssi);
	const char *configured_id = g7_config_get_id();

	connecting_for_search = false;
	if (configured_id[0] == '\0') {
		if (!search_active() || entry->tried) {
			if (should_log_candidate(info->addr)) {
				LOG_INF("Discovered G7 candidate \"%s\"%s -- not connecting", match.name,
					entry->tried ? " (pairing code does not fit)" : "");
			}
			return;
		}
		LOG_INF("Trying \"%s\" with pairing code %s...", match.name, g7_config_get_code());
		connecting_for_search = true;
		strncpy(connecting_name, match.name, sizeof(connecting_name) - 1);
		goto connect;
	}

	if (!name_has_suffix(match.name, configured_id)) {
		if (should_log_candidate(info->addr)) {
			LOG_INF("Ignoring \"%s\" -- does not match configured sensor id \"%s\"",
				match.name, configured_id);
		}
		return;
	}

	if (g7_config_get_code()[0] == '\0') {
		if (should_log_candidate(info->addr)) {
			LOG_WRN("\"%s\" matches, but no pairing code is set (`g7 set_code 1234`)",
				match.name);
		}
		return;
	}

	LOG_INF("Found matching G7 transmitter \"%s\", connecting...", match.name);

connect:
	if (bt_le_scan_stop()) {
		return;
	}
	g7_cgms_adv_pause();

	struct bt_conn_le_create_param create_param = *BT_CONN_LE_CREATE_PARAM(
		BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_INTERVAL);

	int err = bt_conn_le_create(info->addr, &create_param, BT_LE_CONN_PARAM_DEFAULT,
				     &default_conn);
	if (err) {
		LOG_ERR("Failed to create connection (err %d)", err);
		g7_cgms_adv_resume();
		start_scan();
	}
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv_cb,
};

static void start_scan(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	int err = bt_le_scan_start(&scan_param, NULL);

	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for a Dexcom G7 transmitter (service FEBC, name DXCM*/DX02*)...");
}

/* --- GATT: discovery, subscriptions, authentication, data --- */

/*
 * Order, as in xDrip+ (Ob1G5CollectionService / Ob1G5StateMachine.doCheckAuth2):
 * discover unencrypted -> notify ExtraData -> indicate authentication -> run
 * g7_auth -> [bond when the sensor asks] -> indicate control -> write 0x4E.
 * If we already hold a bond, the link is encrypted first.
 */
enum phase {
	PHASE_IDLE,
	PHASE_SECURING, /* bonded before: waiting for encryption */
	PHASE_DISCOVERY,
	PHASE_AUTH,
	PHASE_BONDING,  /* sensor asked for the BLE bond */
	PHASE_DATA,
};

static enum phase phase;
static bool peer_bonded;
static uint16_t auth_handle, auth_ccc_handle;
static uint16_t extra_handle, extra_ccc_handle;
static uint16_t control_handle, control_ccc_handle;

static struct bt_gatt_subscribe_params extra_subscribe_params;
static struct bt_gatt_write_params auth_write_params;
static K_SEM_DEFINE(auth_write_sem, 0, 1);
static uint8_t auth_write_err;

static uint8_t auth_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			       const void *data, uint16_t length)
{
	if (!data) {
		LOG_INF("Authentication characteristic unsubscribed");
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}

	g7_auth_rx_auth(data, length);
	return BT_GATT_ITER_CONTINUE;
}

static uint8_t extra_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				const void *data, uint16_t length)
{
	if (!data) {
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}

	g7_auth_rx_extra(data, length);
	return BT_GATT_ITER_CONTINUE;
}

/* --- backfill: after each reading, ask the G7 for any gap (up to 24 h) --- */

#define BACKFILL_MAX_S   (24 * 3600)
#define BACKFILL_RECORD  9

static uint16_t backfill_handle, backfill_ccc_handle;
static struct bt_gatt_subscribe_params backfill_subscribe_params;
static struct bt_gatt_write_params backfill_write_params;
static uint8_t backfill_req[9];
static unsigned int backfill_rx, backfill_new;

/* Sensor whose clock g7_history is synced to (it restarts with every sensor). */
static bt_addr_le_t clock_sensor;
static bool clock_known;

/* Stores a reading stamped with G7 time (seconds since the session started). */
static bool store_reading(uint32_t dex_time_s, uint16_t mgdl, bool trend_valid, int8_t trend,
			  struct g7_record *out)
{
	*out = (struct g7_record){
		.minute = (uint16_t)MIN(dex_time_s / 60, UINT16_MAX),
		.mgdl = mgdl,
		.trend_tenths = trend,
		.trend_valid = trend_valid,
	};
	return g7_history_add(out);
}

static uint8_t backfill_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				   const void *data, uint16_t length)
{
	if (!data) {
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}

	for (uint16_t off = 0; off + BACKFILL_RECORD <= length; off += BACKFILL_RECORD) {
		struct g7_backfill_reading bf;
		struct g7_record rec;

		if (!g7_parse_backfill((const uint8_t *)data + off, BACKFILL_RECORD, &bf)) {
			continue;
		}
		backfill_rx++;
		if (bf.glucose_valid && g7_state_usable(bf.algorithm_state) &&
		    store_reading(bf.timestamp_s, bf.glucose_mgdl, bf.trend_valid,
				  bf.trend_tenths_mgdl_per_min, &rec)) {
			backfill_new++;
		}
	}
	return BT_GATT_ITER_CONTINUE;
}

static void backfill_written(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	if (err) {
		LOG_ERR("Backfill request rejected (ATT err 0x%02x)", err);
	}
}

static void send_backfill_request(struct bt_conn *conn)
{
	backfill_write_params.func = backfill_written;
	backfill_write_params.handle = control_handle;
	backfill_write_params.offset = 0;
	backfill_write_params.data = backfill_req;
	backfill_write_params.length = sizeof(backfill_req);

	int err = bt_gatt_write(conn, &backfill_write_params);

	if (err) {
		LOG_ERR("Failed to send backfill request (err %d)", err);
	}
}

static void backfill_subscribed(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_subscribe_params *params)
{
	if (err) {
		LOG_ERR("Enabling backfill notifications failed (ATT err 0x%02x)", err);
		return;
	}
	send_backfill_request(conn);
}

/*
 * `reading_s` is the new reading's G7 time; `last` the newest record we had
 * before it (NULL if none). Requests everything in between, capped at 24 h, in
 * xDrip+'s BackFillTxMessage2 format: 59 <start u32 LE> <end u32 LE>.
 */
/* Returns true if a backfill was requested. */
static bool request_backfill(struct bt_conn *conn, uint32_t reading_s, const struct g7_record *last)
{
	if (!backfill_handle) {
		return false;
	}

	uint32_t gap_s = BACKFILL_MAX_S;
	uint16_t reading_min = (uint16_t)MIN(reading_s / 60, UINT16_MAX);

	if (last) {
		if (reading_min <= last->minute || reading_min - last->minute <= 7) {
			return false; /* no gap worth asking for */
		}
		gap_s = MIN((uint32_t)(reading_min - last->minute) * 60 + 120, BACKFILL_MAX_S);
	}

	uint32_t start = reading_s > gap_s ? reading_s - gap_s : 1;
	uint32_t end = reading_s > 60 ? reading_s - 60 : 1;

	if (start >= end) {
		return false;
	}

	backfill_req[0] = G7_OP_BACKFILL_FINISHED; /* 0x59 is both request and done */
	sys_put_le32(start, backfill_req + 1);
	sys_put_le32(end, backfill_req + 5);
	backfill_rx = backfill_new = 0;
	LOG_INF("Requesting backfill of the last %u min", (end - start) / 60);

	backfill_subscribe_params.notify = backfill_notify_cb;
	backfill_subscribe_params.subscribe = backfill_subscribed;
	backfill_subscribe_params.value = BT_GATT_CCC_NOTIFY;
	backfill_subscribe_params.value_handle = backfill_handle;
	backfill_subscribe_params.ccc_handle = backfill_ccc_handle;
	atomic_set_bit(backfill_subscribe_params.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	int err = bt_gatt_subscribe(conn, &backfill_subscribe_params);

	if (err == -EALREADY) {
		send_backfill_request(conn);
	} else if (err) {
		LOG_ERR("Subscribe to backfill failed (err %d)", err);
		return false;
	}
	return true;
}

/* --- session info: ask once per sensor for its session length (extended version) --- */

/* A backfill held back until the 0x52 reply (the G7 closes the link ~1 s after a
 * backfill, so an 0x52 sent after it never gets an answer).
 */
static struct {
	bool pending;
	uint32_t reading_s;
	bool have_last;
	struct g7_record last;
} deferred_backfill;

static struct bt_gatt_write_params version_write_params;
static const uint8_t version_req[] = {G7_OP_EXTENDED_VERSION_TX};

/* Flash writes stay off the Bluetooth RX thread. */
static struct {
	bt_addr_le_t addr;
	struct g7_session_info info;
} pending_session;

static void save_session(struct k_work *work)
{
	int err = g7_config_set_session_length(&pending_session.addr,
					       pending_session.info.length_s,
					       pending_session.info.warmup_s);

	if (err) {
		LOG_WRN("Could not save session info (err %d)", err);
	}
}

static K_WORK_DEFINE(save_session_work, save_session);

static void version_written(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	if (err) {
		LOG_ERR("Extended version request rejected (ATT err 0x%02x)", err);
	}
}

static bool session_length_known(struct bt_conn *conn)
{
	struct g7_session_info info;

	return g7_config_get_session(bt_conn_get_dst(conn), &info) && info.length_s;
}

static void request_session_info(struct bt_conn *conn)
{
	if (session_length_known(conn)) {
		return; /* known for this sensor, saved in flash */
	}

	version_write_params.func = version_written;
	version_write_params.handle = control_handle;
	version_write_params.offset = 0;
	version_write_params.data = version_req;
	version_write_params.length = sizeof(version_req);

	int err = bt_gatt_write(conn, &version_write_params);

	if (err) {
		LOG_ERR("Failed to send extended version request (err %d)", err);
	} else {
		LOG_INF("Asking the sensor for its session length (0x52)");
	}
}

static void on_extended_version(struct bt_conn *conn, const uint8_t *data, uint16_t length)
{
	struct g7_extended_version ver;

	if (!g7_parse_extended_version(data, length, &ver)) {
		LOG_WRN("Malformed extended version reply (%u bytes)", length);
		return;
	}
	LOG_INF("Session length %u s (%u.%u days), warm-up %u s, max %u days, hw %u",
		ver.session_length_s, ver.session_length_s / 86400,
		(ver.session_length_s % 86400) * 10 / 86400, ver.warmup_s, ver.max_lifetime_days,
		ver.hardware_version);

	bt_addr_le_copy(&pending_session.addr, bt_conn_get_dst(conn));
	pending_session.info.length_s = ver.session_length_s;
	pending_session.info.warmup_s = ver.warmup_s;
	k_work_submit(&save_session_work);

	if (deferred_backfill.pending) {
		deferred_backfill.pending = false;
		request_backfill(conn, deferred_backfill.reading_s,
				 deferred_backfill.have_last ? &deferred_backfill.last : NULL);
	}
}

/*
 * The CGM service's Time Offset is sensor time. Re-anchor it on every reading;
 * a different sensor (or a G7 clock that went backwards) starts from scratch.
 */
static void sync_sensor_clock(struct bt_conn *conn, uint32_t sensor_s)
{
	const bt_addr_le_t *addr = bt_conn_get_dst(conn);
	uint32_t prev_s;
	bool had_clock = g7_history_now_s(&prev_s);
	bool new_clock = !clock_known || !bt_addr_le_eq(addr, &clock_sensor) ||
			 (had_clock && sensor_s + 600 < prev_s);

	if (new_clock && clock_known) {
		LOG_INF("Different sensor or its clock restarted -- clearing history");
		g7_history_clear();
	}
	g7_history_sync(sensor_s);
	if (new_clock) {
		bt_addr_le_copy(&clock_sensor, addr);
		clock_known = true;
		LOG_INF("Sensor clock: session running %u.%u days", sensor_s / 86400,
			(sensor_s % 86400) * 10 / 86400);
		g7_cgms_time_changed();
	}
}

static void on_glucose(struct bt_conn *conn, const struct g7_glucose_reading *reading)
{
	struct g7_record last, rec;
	bool have_last = g7_history_latest(&last);

	last_reading.valid = true;
	last_reading.mgdl = reading->glucose_mgdl;
	last_reading.state = reading->algorithm_state;
	last_reading.trend = reading->trend_tenths_mgdl_per_min;
	last_reading.trend_valid = reading->trend_valid;
	last_reading.at_ms = k_uptime_get() - (int64_t)reading->age_s * 1000;

	sync_sensor_clock(conn, reading->message_timestamp_s);
	g7_cgms_set_sensor_state(g7_state_session_ended(reading->algorithm_state),
				 g7_state_failed(reading->algorithm_state));

	if (reading->glucose_valid && g7_state_usable(reading->algorithm_state)) {
		store_reading(reading->glucose_timestamp_s, reading->glucose_mgdl,
			      reading->trend_valid, reading->trend_tenths_mgdl_per_min, &rec);
		g7_cgms_notify_reading(&rec);
	} else {
		LOG_INF("Reading not usable (valid=%d, state 0x%02x) -- not forwarded",
			reading->glucose_valid, reading->algorithm_state);
	}
	/* Sent together the G7 answers only one of backfill and 0x52, and it closes
	 * the link soon after a backfill: ask for the session length first (once per
	 * sensor) and backfill from its reply.
	 */
	if (!session_length_known(conn) && !deferred_backfill.pending) {
		deferred_backfill.pending = true;
		deferred_backfill.reading_s = reading->glucose_timestamp_s;
		deferred_backfill.have_last = have_last;
		if (have_last) {
			deferred_backfill.last = last;
		}
		request_session_info(conn);
		return;
	}
	/* Also the fallback when last cycle's 0x52 went unanswered: do not starve
	 * the history waiting for it.
	 */
	deferred_backfill.pending = false;
	request_backfill(conn, reading->glucose_timestamp_s, have_last ? &last : NULL);
}

static uint8_t control_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				  const void *data, uint16_t length)
{
	if (!data) {
		LOG_INF("Control characteristic unsubscribed");
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}

	int opcode = g7_peek_opcode(data, length);

	switch (opcode) {
	case G7_OP_GLUCOSE_TX: {
		struct g7_glucose_reading reading;

		if (g7_parse_glucose_tx(data, length, &reading)) {
			got_reading = true;
			LOG_INF("Glucose: %u mg/dL (valid=%d, state 0x%02x) trend=%d/10 mg/dL/min "
				"age=%us seq=%u sensor time=%us",
				reading.glucose_mgdl, reading.glucose_valid, reading.algorithm_state,
				reading.trend_tenths_mgdl_per_min, reading.age_s, reading.sequence,
				reading.message_timestamp_s);
			on_glucose(conn, &reading);
		} else {
			LOG_WRN("Malformed glucoseTx payload (%u bytes)", length);
		}
		break;
	}
	case G7_OP_EXTENDED_VERSION_TX:
		on_extended_version(conn, data, length);
		break;
	case G7_OP_BACKFILL_FINISHED:
		LOG_INF("Backfill done: %u records received, %u new in history", backfill_rx,
			backfill_new);
		break;
	default:
		LOG_INF("Unhandled control opcode 0x%02x (%u bytes)", opcode, length);
		LOG_HEXDUMP_DBG(data, length, "control rx");
		break;
	}

	return BT_GATT_ITER_CONTINUE;
}

/* Finds a characteristic's value and CCC handles in the discovered service. */
static int find_chrc(struct bt_gatt_dm *dm, const struct bt_uuid *uuid, uint16_t *value_handle,
		     uint16_t *ccc_handle)
{
	const struct bt_gatt_dm_attr *chrc = bt_gatt_dm_char_by_uuid(dm, uuid);
	const struct bt_gatt_dm_attr *val = chrc ? bt_gatt_dm_desc_by_uuid(dm, chrc, uuid) : NULL;
	const struct bt_gatt_dm_attr *ccc =
		chrc ? bt_gatt_dm_desc_by_uuid(dm, chrc, BT_UUID_GATT_CCC) : NULL;

	if (!val || !ccc) {
		return -ENOENT;
	}
	*value_handle = val->handle;
	*ccc_handle = ccc->handle;
	return 0;
}

static int subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
		     uint16_t value_handle, uint16_t ccc_handle, uint16_t ccc_value,
		     bt_gatt_notify_func_t notify_cb, bt_gatt_subscribe_func_t subscribed_cb)
{
	params->notify = notify_cb;
	params->subscribe = subscribed_cb;
	params->value = ccc_value;
	params->value_handle = value_handle;
	params->ccc_handle = ccc_handle;
	atomic_set_bit(params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	int err = bt_gatt_subscribe(conn, params);

	if (err && err != -EALREADY) {
		LOG_ERR("Subscribe to handle 0x%04x failed (err %d)", value_handle, err);
		return err;
	}
	return 0;
}

static void control_written(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	if (err) {
		LOG_ERR("Glucose request (0x4E) rejected (ATT err 0x%02x)", err);
	}
}

static void control_subscribed(struct bt_conn *conn, uint8_t err,
			       struct bt_gatt_subscribe_params *params)
{
	static const uint8_t glucose_req[] = {G7_OP_GLUCOSE_TX};
	static struct bt_gatt_write_params write_params;

	if (err) {
		LOG_ERR("Enabling control indications failed (ATT err 0x%02x)", err);
		return;
	}

	LOG_INF("Control indications on, requesting glucose (0x4E)");
	write_params.func = control_written;
	write_params.handle = control_handle;
	write_params.offset = 0;
	write_params.data = glucose_req;
	write_params.length = sizeof(glucose_req);

	int rc = bt_gatt_write(conn, &write_params);

	if (rc) {
		LOG_ERR("Failed to send glucose request (err %d)", rc);
	}
}

static void start_data(void)
{
	phase = PHASE_DATA;
	subscribe(default_conn, &control_subscribe_params, control_handle, control_ccc_handle,
		  BT_GATT_CCC_INDICATE, control_notify_cb, control_subscribed);
}

/* --- g7_auth hooks (called from the auth thread) --- */

static void auth_written(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	auth_write_err = err;
	k_sem_give(&auth_write_sem);
}

static int write_auth(const uint8_t *data, size_t len)
{
	static uint8_t buf[20];

	if (!default_conn || len > sizeof(buf)) {
		return -ENOTCONN;
	}

	memcpy(buf, data, len);
	auth_write_params.func = auth_written;
	auth_write_params.handle = auth_handle;
	auth_write_params.offset = 0;
	auth_write_params.data = buf;
	auth_write_params.length = len;
	k_sem_reset(&auth_write_sem);

	int err = bt_gatt_write(default_conn, &auth_write_params);

	if (err) {
		return err;
	}
	if (k_sem_take(&auth_write_sem, K_SECONDS(5))) {
		return -ETIMEDOUT;
	}
	if (auth_write_err) {
		LOG_ERR("authentication write: ATT err 0x%02x", auth_write_err);
		return -EIO;
	}
	return 0;
}

static int write_extra(const uint8_t *data, size_t len)
{
	if (!default_conn) {
		return -ENOTCONN;
	}
	return bt_gatt_write_without_response(default_conn, extra_handle, data, len, false);
}

/*
 * Bonds of sensors used before would otherwise fill the bond table (seen live:
 * -ENOMEM when a new sensor asked to bond). Phones never appear in seen[].
 */
struct bond_find {
	const bt_addr_le_t *addr;
	bool found;
};

static void match_bond(const struct bt_bond_info *info, void *user_data)
{
	struct bond_find *f = user_data;

	f->found |= bt_addr_le_eq(&info->addr, f->addr);
}

static void drop_other_sensor_bonds(const bt_addr_le_t *keep)
{
	for (size_t i = 0; i < ARRAY_SIZE(seen); i++) {
		struct bond_find f = {.addr = &seen[i].addr};

		if (!seen[i].last_ms || bt_addr_le_eq(&seen[i].addr, keep)) {
			continue;
		}
		bt_foreach_bond(BT_ID_DEFAULT, match_bond, &f);
		if (f.found && bt_unpair(BT_ID_DEFAULT, &seen[i].addr) == 0) {
			LOG_INF("Dropped the old bond of \"%s\"", seen[i].name);
		}
	}
}

static void auth_finished(enum g7_auth_result result)
{
	if (!default_conn) {
		return;
	}

	if (connecting_for_search) {
		struct seen_entry *entry = seen_find(bt_conn_get_dst(default_conn));

		if (result == G7_AUTH_OK || result == G7_AUTH_BOND_NOW ||
		    result == G7_AUTH_NEEDS_REPAIR) {
			/* "DXCMab" -> id "ab" */
			const char *suffix = strlen(connecting_name) > 4 ? connecting_name + 4
									  : connecting_name;

			LOG_INF("Pairing code fits \"%s\" -- using it from now on", connecting_name);
			g7_config_set_id(suffix);
			connecting_for_search = false;
		} else if (entry) {
			LOG_INF("Pairing code does not fit \"%s\", will not try it again",
				entry->name);
			entry->tried = true;
		}
		if (result == G7_AUTH_NOT_BONDED) {
			/* Not our sensor: there is no bond of ours to remove. */
			bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			return;
		}
	}

	switch (result) {
	case G7_AUTH_OK:
		start_data();
		break;
	case G7_AUTH_BOND_NOW: {
		LOG_INF("Sensor requested bonding, pairing (Just Works)...");
		phase = PHASE_BONDING;
		drop_other_sensor_bonds(bt_conn_get_dst(default_conn));

		int err = bt_conn_set_security(default_conn, BT_SECURITY_L2);

		if (err) {
			LOG_ERR("Failed to start bonding (err %d)", err);
			bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		break;
	}
	case G7_AUTH_NOT_BONDED:
		/* Sensor no longer knows our bond (xDrip+: BondFailure -> UNBOND). */
		LOG_WRN("Removing our stale bond with the sensor");
		bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(default_conn));
		break;
	case G7_AUTH_NEEDS_REPAIR:
		LOG_WRN("Removing our bond with the sensor -- next connection re-pairs fully");
		g7_config_clear_key();
		bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(default_conn));
		break;
	default:
		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		break;
	}
}

static const struct g7_auth_ops auth_ops = {
	.write_auth = write_auth,
	.write_extra = write_extra,
	.finished = auth_finished,
};

static void auth_subscribed(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_subscribe_params *params)
{
	if (err) {
		LOG_ERR("Enabling authentication indications failed (ATT err 0x%02x)", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	LOG_INF("Authentication indications on, starting G7 authentication");
	phase = PHASE_AUTH;
	/* A sensor we hold a key for would pass on that key alone, whatever the code. */
	if (g7_auth_begin(bt_conn_get_dst(conn), connecting_for_search)) {
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void extra_subscribed(struct bt_conn *conn, uint8_t err,
			     struct bt_gatt_subscribe_params *params)
{
	if (err) {
		LOG_ERR("Enabling ExtraData notifications failed (ATT err 0x%02x)", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	if (subscribe(conn, &auth_subscribe_params, auth_handle, auth_ccc_handle,
		      BT_GATT_CCC_INDICATE, auth_notify_cb, auth_subscribed)) {
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void discovery_completed(struct bt_gatt_dm *dm, void *ctx)
{
	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);
	int err = find_chrc(dm, &char_authentication_uuid.uuid, &auth_handle, &auth_ccc_handle);

	err = err ? err : find_chrc(dm, &char_extra_data_uuid.uuid, &extra_handle, &extra_ccc_handle);
	err = err ? err : find_chrc(dm, &char_control_uuid.uuid, &control_handle,
				    &control_ccc_handle);
	if (find_chrc(dm, &char_backfill_uuid.uuid, &backfill_handle, &backfill_ccc_handle)) {
		LOG_WRN("No backfill characteristic -- history will not be filled");
		backfill_handle = 0;
	}
	bt_gatt_dm_data_release(dm);

	if (err) {
		LOG_ERR("CGM service lacks authentication/ExtraData/control -- not a G7?");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	LOG_INF("CGM service discovered, enabling ExtraData notifications");
	if (subscribe(conn, &extra_subscribe_params, extra_handle, extra_ccc_handle,
		      BT_GATT_CCC_NOTIFY, extra_notify_cb, extra_subscribed)) {
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void discovery_service_not_found(struct bt_conn *conn, void *ctx)
{
	LOG_ERR("CGM service not found on this device");
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void discovery_error(struct bt_conn *conn, int err, void *ctx)
{
	LOG_ERR("GATT discovery failed (err %d)", err);
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static const struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed,
	.service_not_found = discovery_service_not_found,
	.error_found = discovery_error,
};

static void start_discovery(struct bt_conn *conn)
{
	phase = PHASE_DISCOVERY;

	int err = bt_gatt_dm_start(conn, &cgm_service_uuid.uuid, &discovery_cb, NULL);

	if (err) {
		LOG_ERR("Failed to start GATT discovery (err %d)", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

/* --- connection lifecycle --- */

struct bond_lookup {
	const bt_addr_le_t *addr;
	bool found;
};

static void bond_lookup_cb(const struct bt_bond_info *info, void *user_data)
{
	struct bond_lookup *lookup = user_data;

	if (bt_addr_le_eq(&info->addr, lookup->addr)) {
		lookup->found = true;
	}
}

static bool is_bonded(const bt_addr_le_t *addr)
{
	struct bond_lookup lookup = {.addr = addr};

	bt_foreach_bond(BT_ID_DEFAULT, bond_lookup_cb, &lookup);
	return lookup.found;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (conn != default_conn) {
		return; /* the phone's link to our CGM service -- see g7_cgms.c */
	}

	if (err) {
		LOG_ERR("Connection failed (err %d)", err);
		bt_conn_unref(default_conn);
		default_conn = NULL;
		g7_cgms_adv_resume();
		start_scan();
		return;
	}

	peer_bonded = is_bonded(bt_conn_get_dst(conn));
	if (!peer_bonded) {
		LOG_INF("Connected (not bonded yet), discovering CGM service...");
		start_discovery(conn);
		return;
	}

	LOG_INF("Connected to bonded sensor, encrypting...");
	phase = PHASE_SECURING;
	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_ERR("Failed to start encryption (err %d)", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != default_conn) {
		return;
	}

	LOG_INF("Disconnected (reason 0x%02x)", reason);

	if (connecting_for_search) {
		struct seen_entry *entry = seen_find(bt_conn_get_dst(conn));

		if (entry && !entry->tried && ++entry->fails >= SEARCH_MAX_FAILS) {
			LOG_INF("No answer from \"%s\" twice, skipping it", entry->name);
			entry->tried = true;
		}
		connecting_for_search = false;
	}

	if (got_reading) {
		retry_after = k_uptime_get() + NEXT_CYCLE_MS;
		quick_retries_left = QUICK_RETRIES;
	} else if (quick_retries_left > 0) {
		/* Usually a missed first packet; the G7 is still awake for a few s. */
		quick_retries_left--;
		retry_after = k_uptime_get() + QUICK_RETRY_MS;
		LOG_INF("No reading this time, retrying in 1 s");
	} else {
		retry_after = k_uptime_get() + RETRY_BACKOFF_MS;
		quick_retries_left = QUICK_RETRIES;
		LOG_INF("No reading this time, next attempt in %d s", RETRY_BACKOFF_MS / 1000);
	}
	got_reading = false;
	g7_cgms_adv_resume();
	g7_auth_abort();
	phase = PHASE_IDLE;
	bt_conn_unref(default_conn);
	default_conn = NULL;
	start_scan();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			      enum bt_security_err err)
{
	if (conn != default_conn) {
		return;
	}

	if (err) {
		LOG_ERR("Security failed (level %d, err %d)", level, err);
		if (phase == PHASE_SECURING || phase == PHASE_BONDING) {
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		return;
	}

	LOG_INF("Security level %d established", level);

	if (phase == PHASE_SECURING) {
		start_discovery(conn);
	} else if (phase == PHASE_BONDING) {
		start_data();
	}
}

static void auth_cancel(struct bt_conn *conn)
{
	LOG_WRN("Pairing cancelled");
}

/*
 * New phone bonds only within 10 minutes of power-on: bonding is Just Works,
 * so otherwise anyone in range could pair and change the pairing code through
 * the configuration service. Re-plug the bridge to pair a (new) phone.
 * Pairing with the sensor (we are central there) is never restricted.
 */
#define PHONE_PAIRING_WINDOW_MS (10 * 60 * 1000)

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL &&
	    k_uptime_get() > PHONE_PAIRING_WINDOW_MS) {
		LOG_WRN("Refusing phone pairing: only allowed in the first 10 min after power-on "
			"(re-plug the bridge)");
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}
	return BT_SECURITY_ERR_SUCCESS;
}

static struct bt_conn_auth_cb auth_cb = {
	.pairing_accept = pairing_accept,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete, bonded=%d", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

int main(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* BLE-level pairing is Just Works; the pairing code is used by g7_auth instead. */
	bt_conn_auth_cb_register(&auth_cb);
	bt_conn_auth_info_cb_register(&auth_info_cb);
	g7_auth_init(&auth_ops);
	g7_cgms_init();

	bt_le_scan_cb_register(&scan_callbacks);

	LOG_INF("BLE Bridge starting");
	if (g7_config_get_id()[0] == '\0' && g7_config_get_code()[0] != '\0') {
		g7_bridge_start_search();
	}
	start_scan();

	return 0;
}
