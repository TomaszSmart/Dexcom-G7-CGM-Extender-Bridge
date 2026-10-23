/*
 * Configuration service for the phone web page (web/bridge.html, Web Bluetooth).
 *
 *   service  a7c10001-3f8e-4b5a-9d2c-6e0b1f4a2c90
 *   status   a7c10002-...  read:  JSON snapshot (code, sensor, last reading,
 *                                 session, sensors in range, the reader's link RSSI,
 *                                 last command result)
 *   command  a7c10003-...  write: "code 1234" | "search" | "id 18" | "release" | "unpair_phone"
 *
 * Both need an encrypted (bonded) link, and new bonds are only accepted in the
 * first 10 minutes after power-on (main.c pairing_accept).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include "g7_battery.h"
#include "g7_bridge.h"
#include "g7_certstore.h"
#include "g7_cgms.h"
#include "g7_config.h"
#include "g7_history.h"

LOG_MODULE_REGISTER(g7_ui, LOG_LEVEL_INF);

#define UUID_BASE(n) BT_UUID_128_ENCODE(0xa7c10000 + (n), 0x3f8e, 0x4b5a, 0x9d2c, 0x6e0b1f4a2c90)

static const struct bt_uuid_128 ui_svc_uuid = BT_UUID_INIT_128(UUID_BASE(1));
static const struct bt_uuid_128 ui_status_uuid = BT_UUID_INIT_128(UUID_BASE(2));
static const struct bt_uuid_128 ui_cmd_uuid = BT_UUID_INIT_128(UUID_BASE(3));
static const struct bt_uuid_128 ui_certs_uuid = BT_UUID_INIT_128(UUID_BASE(4));

static char status_json[BT_ATT_MAX_ATTRIBUTE_LEN];
static size_t status_len;
static char last_msg[64] = "ready";

/* ---- RSSI of the link the page reads us over (range test) ------------------- *
 * HCI Read RSSI waits for the controller, which must not happen on the BT RX
 * thread that serves the read: each read queues a measurement, and the status
 * carries the one from the previous read (the page polls, so it is 1 poll old).
 */
static struct bt_conn *rssi_conn;
static int8_t reader_rssi;
static bool have_rssi;

static void measure_rssi(struct k_work *work)
{
	struct bt_conn *conn = rssi_conn;
	struct bt_hci_cp_read_rssi *cp;
	struct net_buf *buf, *rsp = NULL;
	uint16_t handle;

	rssi_conn = NULL;
	if (!conn) {
		return;
	}
	if (bt_hci_get_conn_handle(conn, &handle) == 0) {
		buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
		if (buf) {
			cp = net_buf_add(buf, sizeof(*cp));
			cp->handle = sys_cpu_to_le16(handle);
			if (bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp) == 0) {
				const struct bt_hci_rp_read_rssi *rp = (const void *)rsp->data;

				reader_rssi = rp->rssi;
				have_rssi = rp->status == 0;
				net_buf_unref(rsp);
			}
		}
	}
	bt_conn_unref(conn);
}

static K_WORK_DEFINE(rssi_work, measure_rssi);

static void queue_rssi(struct bt_conn *conn)
{
	if (!rssi_conn && !k_work_is_pending(&rssi_work)) {
		rssi_conn = bt_conn_ref(conn);
		k_work_submit(&rssi_work);
	}
}

/* ---- status ---------------------------------------------------------------- */

#define APPEND(...)                                                                        \
	do {                                                                                \
		int n = snprintf(p, end - p, __VA_ARGS__);                                  \
		p += (n > 0 && n < end - p) ? n : 0;                                        \
	} while (0)

/*
 * Room kept for the fields after the sensor list (RSSI, battery, certificates, version and a
 * message of up to 63 characters), plus one more list entry.
 */
#define STATUS_TAIL_RESERVE (sizeof(last_msg) + 120)
#define STATUS_SEEN_ENTRY_MAX 64

static void build_status(void)
{
	struct g7_bridge_status st;
	struct g7_session_info info;
	bt_addr_le_t sensor;
	uint32_t sensor_s;
	char *p = status_json, *end = status_json + sizeof(status_json);

	g7_bridge_get_status(&st);

	APPEND("{\"code\":\"%s\",\"id\":\"%s\",\"search\":%d,\"left\":%u,\"phone\":%d,"
	       "\"xdrip\":%d",
	       g7_config_get_code(), g7_config_get_id(), st.searching, st.search_left_s,
	       g7_cgms_phone_connected(), g7_cgms_collecting());

	if (st.have_reading) {
		APPEND(",\"g\":{\"mgdl\":%u,\"state\":%u,\"age\":%u", st.mgdl, st.state,
		       st.reading_age_s);
		if (st.trend_valid) {
			APPEND(",\"trend\":%d", st.trend_tenths);
		}
		APPEND("}");
	}

	/* No sensor set up (or released): the old sensor's session is not news any more */
	bool none = g7_config_get_id()[0] == '\0' && g7_config_get_code()[0] == '\0';
	bool have_info = !none && g7_config_get_key_addr(&sensor) &&
			 g7_config_get_session(&sensor, &info);

	APPEND(",\"sess\":{");
	if (!none && g7_history_now_s(&sensor_s)) {
		APPEND("\"min\":%u,", sensor_s / 60);
	}
	if (have_info && info.length_s) {
		APPEND("\"len\":%u,", info.length_s / 60);
	}
	if (have_info && info.start_utc_s) {
		APPEND("\"start\":%lld,", info.start_utc_s);
	}
	if (p[-1] == ',') {
		p--;
	}
	/*
	 * The whole status must fit one attribute (512 bytes): with many G7s around the list
	 * got cut off mid-JSON. The sensor in use goes first, and the list stops while there is
	 * still room for the fields after it (RSSI, battery, certificates, version, message).
	 */
	APPEND("},\"seen\":[");
	size_t listed = 0;

	for (int pass = 0; pass < 2; pass++) {
		for (size_t i = 0; i < st.n_seen; i++) {
			const struct g7_seen_sensor *s = &st.seen[i];

			if (s->paired != (pass == 0) ||
			    end - p < STATUS_TAIL_RESERVE + STATUS_SEEN_ENTRY_MAX) {
				continue;
			}
			/* "tried"/"use" only when set: the page and the app read a missing one as 0 */
			APPEND("%s{\"n\":\"%s\",\"rssi\":%d,\"ago\":%u%s%s}", listed++ ? "," : "",
			       s->name, s->rssi, s->seen_s_ago, s->tried ? ",\"tried\":1" : "",
			       s->paired ? ",\"use\":1" : "");
		}
	}
	APPEND("]");
	if (have_rssi) {
		APPEND(",\"prssi\":%d", reader_rssi);
	}

	struct g7_battery bat;

	if (g7_battery_get(&bat)) {
		APPEND(",\"bat\":{\"mv\":%u,\"pct\":%u,\"usb\":%d}", bat.mv, bat.pct, bat.usb);
	}
	static const char *const cert_src[] = {"none", "stored", "builtin"};

	APPEND(",\"certs\":\"%s\"", cert_src[g7_certs_source()]);
	APPEND(",\"fw\":\"%s\"", CONFIG_BT_DIS_FW_REV_STR);
	APPEND(",\"msg\":\"%s\"}", last_msg);

	status_len = p - status_json;
}

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	/* Long reads come back with offset > 0: keep serving the same snapshot. */
	if (offset == 0) {
		build_status();
		queue_rssi(conn);
	}
	return bt_gatt_attr_read(conn, attr, buf, len, offset, status_json, status_len);
}

/* ---- commands (settings writes and unpairing stay off the BT RX thread) ------ */

static char pending_cmd[24];

struct unpair_ctx {
	bt_addr_le_t sensor;
	bool have_sensor;
	int removed;
};

static void unpair_non_sensor(const struct bt_bond_info *info, void *user_data)
{
	struct unpair_ctx *ctx = user_data;

	if (!(ctx->have_sensor && bt_addr_le_eq(&info->addr, &ctx->sensor)) &&
	    bt_unpair(BT_ID_DEFAULT, &info->addr) == 0) {
		ctx->removed++;
	}
}

static bool all_digits(const char *s)
{
	for (; *s; s++) {
		if (*s < '0' || *s > '9') {
			return false;
		}
	}
	return true;
}

static void run_command(struct k_work *work)
{
	char *cmd = pending_cmd;

	if (strncmp(cmd, "code ", 5) == 0) {
		const char *code = cmd + 5;

		if ((strlen(code) != 4 && strlen(code) != 6) || !all_digits(code) ||
		    g7_config_set_code(code)) {
			snprintf(last_msg, sizeof(last_msg), "invalid code");
		} else {
			/* A new code means a new sensor: find it by the code. */
			g7_config_set_id("");
			g7_bridge_start_search();
			snprintf(last_msg, sizeof(last_msg), "code %s set, searching", code);
		}
	} else if (strcmp(cmd, "search") == 0) {
		if (g7_config_get_code()[0] == '\0') {
			snprintf(last_msg, sizeof(last_msg), "set a code first");
		} else {
			g7_config_set_id("");
			g7_bridge_start_search();
			snprintf(last_msg, sizeof(last_msg), "searching");
		}
	} else if (strncmp(cmd, "id ", 3) == 0) {
		snprintf(last_msg, sizeof(last_msg),
			 g7_config_set_id(cmd + 3) ? "invalid id" : "sensor id set");
	} else if (strcmp(cmd, "release") == 0) {
		g7_bridge_release_sensor();
		snprintf(last_msg, sizeof(last_msg), "sensor released");
	} else if (strcmp(cmd, "unpair_phone") == 0) {
		struct unpair_ctx ctx = {0};

		ctx.have_sensor = g7_config_get_key_addr(&ctx.sensor);
		bt_foreach_bond(BT_ID_DEFAULT, unpair_non_sensor, &ctx);
		snprintf(last_msg, sizeof(last_msg), "%d phone bond(s) removed", ctx.removed);
	} else if (strcmp(cmd, "certs_commit") == 0) {
		int err = g7_certs_commit();

		snprintf(last_msg, sizeof(last_msg), "%s",
			 !err ? "G7 configuration saved"
			 : err == -EBADMSG ? "G7 configuration: QR code data does not match"
			 : err == -EINVAL ? "G7 configuration: upload incomplete"
					  : "G7 configuration: saving failed");
	} else if (strcmp(cmd, "certs_clear") == 0) {
		g7_certs_clear();
		snprintf(last_msg, sizeof(last_msg), "G7 configuration removed");
	} else {
		snprintf(last_msg, sizeof(last_msg), "unknown command");
	}
	LOG_INF("UI command \"%s\": %s", cmd, last_msg);
}

static K_WORK_DEFINE(cmd_work, run_command);

static ssize_t write_cmd(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset || len == 0 || len >= sizeof(pending_cmd)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (k_work_is_pending(&cmd_work)) {
		return BT_GATT_ERR(BT_ATT_ERR_PROCEDURE_IN_PROGRESS);
	}
	memcpy(pending_cmd, buf, len);
	pending_cmd[len] = '\0';
	while (len && (pending_cmd[len - 1] == '\n' || pending_cmd[len - 1] == ' ')) {
		pending_cmd[--len] = '\0';
	}
	k_work_submit(&cmd_work);
	return len ? len : 1;
}

/*
 * Certificate upload: <part 1=A 2=B 3=key> <offset u16 LE> <bytes>, in order;
 * then the command "certs_commit" validates and saves them.
 */
static ssize_t write_certs(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *p = buf;

	if (offset || len < 4) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (g7_certs_stage(p[0], sys_get_le16(p + 1), p + 3, len - 3)) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

BT_GATT_SERVICE_DEFINE(ui_svc,
	BT_GATT_PRIMARY_SERVICE(&ui_svc_uuid),
	BT_GATT_CHARACTERISTIC(&ui_status_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_status, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&ui_cmd_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE_ENCRYPT,
			       NULL, write_cmd, NULL),
	BT_GATT_CHARACTERISTIC(&ui_certs_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE_ENCRYPT,
			       NULL, write_certs, NULL),
);
