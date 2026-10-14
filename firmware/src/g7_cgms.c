/*
 * Continuous Glucose Monitoring Service (CGMS 1.0.x) peripheral, the subset
 * xDrip+'s GluPro collector uses. No E2E-CRC: CGM Feature says "not
 * supported" and we never append one; CRCs xDrip+ puts on its control point
 * writes are ignored.
 */
#include "g7_cgms.h"
#include "g7_config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

LOG_MODULE_REGISTER(g7_cgms, LOG_LEVEL_INF);

/* CGM Measurement flags */
#define MEAS_FLAG_TREND 0x01

/* Record Access Control Point */
#define RACP_REPORT_RECORDS  0x01
#define RACP_ABORT           0x03
#define RACP_REPORT_NUMBER   0x04
#define RACP_NUMBER_RESPONSE 0x05
#define RACP_RESPONSE        0x06
#define RACP_OPER_NULL       0x00
#define RACP_OPER_ALL        0x01
#define RACP_OPER_LE         0x02
#define RACP_OPER_GE         0x03
#define RACP_OPER_RANGE      0x04
#define RACP_OPER_FIRST      0x05
#define RACP_OPER_LAST       0x06
#define RACP_FILTER_TIME_OFFSET 0x01
#define RACP_RSP_SUCCESS        0x01
#define RACP_RSP_OP_NOT_SUPP    0x02
#define RACP_RSP_INVALID_OPER   0x03
#define RACP_RSP_OPER_NOT_SUPP  0x04
#define RACP_RSP_INVALID_OPERAND 0x05
#define RACP_RSP_NO_RECORDS     0x06
#define RACP_RSP_BUSY           0x08 /* "procedure not completed" */

/* CGM Specific Ops Control Point */
#define SOCP_SET_INTERVAL  0x01
#define SOCP_GET_INTERVAL  0x02
#define SOCP_INTERVAL_RSP  0x03
#define SOCP_START_SESSION 0x1a
#define SOCP_STOP_SESSION  0x1b
#define SOCP_RESPONSE      0x1c
#define SOCP_RSP_SUCCESS   0x01
#define SOCP_RSP_OP_NOT_SUPP 0x02

#define G7_INTERVAL_MIN 5

static struct bt_conn *phone;
static bool adv_paused;

/* Sensor Status Annunciation octet (CGM Status / Measurement), from the G7 state. */
#define SSA_SESSION_STOPPED    0x01
#define SSA_SENSOR_MALFUNCTION 0x08
static uint8_t sensor_status;

/* G7 standard session when the sensor has not told us yet: 10 days + 12 h grace. */
#define DEFAULT_SESSION_HOURS 252
static bool meas_notify, racp_indicate, socp_indicate;

/* Session Start Time placeholder (2026-01-01) until the phone has told us its
 * clock: xDrip+ reads it, finds it more than 2 minutes off and writes "now".
 */
static const uint8_t session_start_unknown[9] = {0xea, 0x07, 1, 1, 0, 0, 0, 0, 0};

/* ---- encoding ------------------------------------------------------------ */

/* IEEE 11073 16-bit SFLOAT: 4-bit exponent, 12-bit mantissa (both signed). */
static uint16_t sfloat(int mantissa, int exponent)
{
	return ((exponent & 0x0f) << 12) | (mantissa & 0x0fff);
}

static size_t encode_measurement(const struct g7_record *rec, uint8_t buf[8])
{
	size_t len = 6;

	buf[1] = 0;
	sys_put_le16(sfloat(rec->mgdl, 0), buf + 2);
	sys_put_le16(rec->minute, buf + 4);
	if (rec->trend_valid) {
		buf[1] |= MEAS_FLAG_TREND;
		sys_put_le16(sfloat(rec->trend_tenths, -1), buf + len);
		len += 2;
	}
	buf[0] = len;
	return len;
}

/* ---- attribute handlers ---------------------------------------------------- */

static ssize_t read_feature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			    uint16_t len, uint16_t offset)
{
	/* No optional features; type = interstitial fluid (0x9), sample location =
	 * subcutaneous tissue (0x5); E2E-CRC 0xFFFF = not supported.
	 */
	static const uint8_t feature[] = {0x00, 0x00, 0x00, 0x59, 0xff, 0xff};

	return bt_gatt_attr_read(conn, attr, buf, len, offset, feature, sizeof(feature));
}

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	uint8_t status[5] = {0};
	uint16_t now;

	/* Time Offset "now" in sensor minutes: xDrip+ takes session start = now - this.
	 * Until a G7 reading has arrived since boot we do not know it; refuse, so
	 * xDrip+ keeps the start it already has instead of learning a wrong one.
	 */
	if (!g7_history_now(&now)) {
		if (offset == 0) {
			LOG_INF("Phone read CGM status -- sensor time not known yet, refusing");
		}
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	sys_put_le16(now, status);
	status[2] = sensor_status;
	if (offset == 0) {
		LOG_INF("Phone read CGM status (sensor at %u min = %u.%u days, status 0x%02x)", now,
			now / 1440, (now % 1440) * 10 / 1440, sensor_status);
	}
	return bt_gatt_attr_read(conn, attr, buf, len, offset, status, sizeof(status));
}

/* ---- Session Start Time: civil date <-> Unix time (H. Hinnant's algorithms) ---- */

static int64_t days_from_civil(int y, unsigned int m, unsigned int d)
{
	y -= m <= 2;

	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned int yoe = (unsigned int)(y - era * 400);
	const unsigned int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	const unsigned int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return (int64_t)era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned int *m, unsigned int *d)
{
	z += 719468;

	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned int doe = (unsigned int)(z - era * 146097);
	const unsigned int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned int mp = (5 * doy + 2) / 153;

	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

static void encode_date_time(int64_t t, uint8_t out[9])
{
	int y;
	unsigned int m, d;
	int64_t days = t / 86400, secs = t % 86400;

	if (secs < 0) {
		secs += 86400;
		days--;
	}
	civil_from_days(days, &y, &m, &d);
	sys_put_le16(y, out);
	out[2] = m;
	out[3] = d;
	out[4] = secs / 3600;
	out[5] = secs / 60 % 60;
	out[6] = secs % 60;
	out[7] = 0; /* UTC, as xDrip+ writes it */
	out[8] = 0;
}

static bool current_session(bt_addr_le_t *sensor, struct g7_session_info *info)
{
	return g7_config_get_key_addr(sensor) && g7_config_get_session(sensor, info);
}

/* The phone's clock arrived: flash writes stay off the Bluetooth RX thread. */
static struct {
	bt_addr_le_t sensor;
	int64_t start;
} pending_start;

static void kick_phone(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(kick_work, kick_phone);

static void save_start(struct k_work *work)
{
	int err = g7_config_set_session_start(&pending_start.sensor, pending_start.start);

	if (err) {
		LOG_WRN("Could not save session start (err %d)", err);
	}
	/* Reconnect so xDrip+ reads the corrected start back and settles on it. */
	k_work_schedule(&kick_work, K_SECONDS(1));
}

static K_WORK_DEFINE(save_start_work, save_start);

static ssize_t read_start_time(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	uint8_t value[9];
	bt_addr_le_t sensor;
	struct g7_session_info info;

	if (current_session(&sensor, &info) && info.start_utc_s) {
		encode_date_time(info.start_utc_s, value);
	} else {
		memcpy(value, session_start_unknown, sizeof(value));
	}
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

/*
 * CGMS: the collector writes its current time; the sensor derives the session
 * start by subtracting its Time Offset, and reports that from then on.
 */
static ssize_t write_start_time(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *v = buf;
	uint32_t sensor_s;
	bt_addr_le_t sensor;
	struct g7_session_info info = {0};

	if (offset || len < 9) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (v[2] < 1 || v[2] > 12 || v[3] < 1 || v[3] > 31 || v[4] > 23 || v[5] > 59 ||
	    v[6] > 59) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	int64_t now = days_from_civil(sys_get_le16(v), v[2], v[3]) * 86400 + v[4] * 3600 +
		      v[5] * 60 + v[6] - (int8_t)v[7] * 900;

	if (!g7_history_now_s(&sensor_s) || !g7_config_get_key_addr(&sensor)) {
		LOG_INF("Phone sent its clock, but the sensor time is not known yet");
		return len;
	}
	g7_config_get_session(&sensor, &info);

	int64_t start = now - sensor_s;
	uint8_t shown[9];

	encode_date_time(start, shown);
	LOG_INF("Phone clock -> session started %u-%02u-%02u %02u:%02u UTC",
		sys_get_le16(shown), shown[2], shown[3], shown[4], shown[5]);

	if (info.start_utc_s && llabs(start - info.start_utc_s) <= 120) {
		return len; /* already known */
	}
	bt_addr_le_copy(&pending_start.sensor, &sensor);
	pending_start.start = start;
	k_work_submit(&save_start_work);
	return len;
}

static ssize_t read_run_time(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	uint8_t run_time[2];
	struct g7_session_info info;
	bt_addr_le_t sensor;
	uint16_t hours = DEFAULT_SESSION_HOURS;

	/* Expected session length in hours; xDrip+ shows remaining days from it. */
	if (g7_config_get_key_addr(&sensor) && g7_config_get_session(&sensor, &info) &&
	    info.length_s) {
		hours = (info.length_s + 1800) / 3600;
	}
	sys_put_le16(hours, run_time);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, run_time, sizeof(run_time));
}

static void meas_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	meas_notify = value == BT_GATT_CCC_NOTIFY;
	LOG_INF("Phone %s glucose notifications", meas_notify ? "enabled" : "disabled");
}

static void racp_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	racp_indicate = value == BT_GATT_CCC_INDICATE;
}

static void socp_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	socp_indicate = value == BT_GATT_CCC_INDICATE;
}

static ssize_t write_racp(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t write_socp(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags);

BT_GATT_SERVICE_DEFINE(cgms_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_CGMS),
	BT_GATT_CHARACTERISTIC(BT_UUID_CGM_MEASUREMENT, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(meas_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_CGM_FEATURE, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_feature, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_CGM_STATUS, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_status, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_CGM_SESSION_START_TIME,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_start_time,
			       write_start_time, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_CGM_SESSION_RUN_TIME, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_run_time, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_RECORD_ACCESS_CONTROL_POINT,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE, BT_GATT_PERM_WRITE, NULL,
			       write_racp, NULL),
	BT_GATT_CCC(racp_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_CGM_SPECIFIC_OPS_CONTROL_POINT,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE, BT_GATT_PERM_WRITE, NULL,
			       write_socp, NULL),
	BT_GATT_CCC(socp_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Value attribute indexes in cgms_svc.attrs[]: service, then per characteristic
 * a declaration + value (+ CCC for measurement, RACP and SOCP).
 */
#define ATTR_MEAS 2
#define ATTR_RACP 13
#define ATTR_SOCP 16

/* ---- control points (worker thread: may send many notifications) ----------- */

static struct {
	bool racp;          /* else SOCP */
	uint8_t data[20];
	uint8_t len;
} pending;
static atomic_t busy;
static K_SEM_DEFINE(work_sem, 0, 1);
static K_SEM_DEFINE(indicate_sem, 1, 1);
static struct bt_gatt_indicate_params ind_params;

static void indicate_done(struct bt_conn *conn, struct bt_gatt_indicate_params *params,
			  uint8_t err)
{
	k_sem_give(&indicate_sem);
}

static void indicate(int attr_idx, const uint8_t *data, uint16_t len)
{
	static uint8_t buf[8];

	if (!phone || k_sem_take(&indicate_sem, K_SECONDS(5))) {
		return;
	}
	memcpy(buf, data, len);
	ind_params.attr = &cgms_svc.attrs[attr_idx];
	ind_params.func = indicate_done;
	ind_params.data = buf;
	ind_params.len = len;
	if (bt_gatt_indicate(phone, &ind_params)) {
		k_sem_give(&indicate_sem);
	}
}

static void racp_response(uint8_t req_op, uint8_t code)
{
	const uint8_t rsp[] = {RACP_RESPONSE, RACP_OPER_NULL, req_op, code};

	indicate(ATTR_RACP, rsp, sizeof(rsp));
}

/* `wait` retries while TX buffers are full -- only from our own thread. */
static int send_measurement(const struct g7_record *rec, bool wait)
{
	uint8_t buf[8];
	size_t len = encode_measurement(rec, buf);

	for (int tries = 0; tries < (wait ? 50 : 1); tries++) {
		int err = bt_gatt_notify(phone, &cgms_svc.attrs[ATTR_MEAS], buf, len);

		if (err != -ENOMEM) {
			return err;
		}
		k_msleep(20); /* TX buffers full, let the link drain */
	}
	return -ENOMEM;
}

static void handle_racp(const uint8_t *req, size_t len)
{
	uint8_t op = req[0], oper = len > 1 ? req[1] : 0xff;
	uint16_t min = 0, max = 0xffff;

	if (op == RACP_ABORT) {
		racp_response(op, RACP_RSP_SUCCESS);
		return;
	}
	if (op != RACP_REPORT_RECORDS && op != RACP_REPORT_NUMBER) {
		racp_response(op, RACP_RSP_OP_NOT_SUPP);
		return;
	}

	switch (oper) {
	case RACP_OPER_ALL:
	case RACP_OPER_FIRST:
	case RACP_OPER_LAST:
		break;
	case RACP_OPER_LE:
	case RACP_OPER_GE:
	case RACP_OPER_RANGE:
		if (len < (oper == RACP_OPER_RANGE ? 7 : 5) || req[2] != RACP_FILTER_TIME_OFFSET) {
			racp_response(op, RACP_RSP_INVALID_OPERAND);
			return;
		}
		if (oper == RACP_OPER_LE) {
			max = sys_get_le16(req + 3);
		} else {
			min = sys_get_le16(req + 3);
		}
		if (oper == RACP_OPER_RANGE) {
			max = sys_get_le16(req + 5);
		}
		break;
	default:
		racp_response(op, RACP_RSP_OPER_NOT_SUPP);
		return;
	}

	size_t count = g7_history_count(min, max);
	size_t first = 0;

	if (oper == RACP_OPER_FIRST || oper == RACP_OPER_LAST) {
		first = oper == RACP_OPER_LAST && count ? count - 1 : 0;
		count = MIN(count, 1);
	}

	if (op == RACP_REPORT_NUMBER) {
		uint8_t rsp[4] = {RACP_NUMBER_RESPONSE, RACP_OPER_NULL};

		sys_put_le16(count, rsp + 2);
		indicate(ATTR_RACP, rsp, sizeof(rsp));
		return;
	}

	LOG_INF("Phone asked for history %u..%u min: sending %zu records", min, max, count);
	if (count == 0) {
		racp_response(op, RACP_RSP_NO_RECORDS);
		return;
	}
	for (size_t i = 0; i < count; i++) {
		struct g7_record rec;

		if (!phone || !g7_history_get(min, max, first + i, &rec) ||
		    send_measurement(&rec, true)) {
			racp_response(op, RACP_RSP_BUSY);
			return;
		}
	}
	racp_response(op, RACP_RSP_SUCCESS);
}

static void handle_socp(const uint8_t *req, size_t len)
{
	uint8_t op = req[0];

	if (op == SOCP_GET_INTERVAL) {
		const uint8_t rsp[] = {SOCP_INTERVAL_RSP, G7_INTERVAL_MIN};

		indicate(ATTR_SOCP, rsp, sizeof(rsp));
		return;
	}

	/* The G7 sets its own pace and needs no session control or calibration
	 * from here; accept interval/session ops as no-ops, refuse the rest.
	 */
	bool ok = op == SOCP_SET_INTERVAL || op == SOCP_START_SESSION || op == SOCP_STOP_SESSION;
	const uint8_t rsp[] = {SOCP_RESPONSE, op, ok ? SOCP_RSP_SUCCESS : SOCP_RSP_OP_NOT_SUPP};

	LOG_INF("Phone control point op 0x%02x -> %s", op, ok ? "ok" : "not supported");
	indicate(ATTR_SOCP, rsp, sizeof(rsp));
}

static void cgms_thread(void *p1, void *p2, void *p3)
{
	for (;;) {
		k_sem_take(&work_sem, K_FOREVER);
		if (pending.racp) {
			handle_racp(pending.data, pending.len);
		} else {
			handle_socp(pending.data, pending.len);
		}
		atomic_clear(&busy);
	}
}

K_THREAD_DEFINE(g7_cgms_tid, 2048, cgms_thread, NULL, NULL, NULL, 8, 0, 0);

static ssize_t queue_cp(bool racp, bool subscribed, const void *buf, uint16_t len,
			uint16_t offset)
{
	if (offset || len == 0 || len > sizeof(pending.data)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (!subscribed) {
		return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
	}
	if (!atomic_cas(&busy, 0, 1)) {
		return BT_GATT_ERR(BT_ATT_ERR_PROCEDURE_IN_PROGRESS);
	}
	pending.racp = racp;
	pending.len = len;
	memcpy(pending.data, buf, len);
	k_sem_give(&work_sem);
	return len;
}

static ssize_t write_racp(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	return queue_cp(true, racp_indicate, buf, len, offset);
}

static ssize_t write_socp(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	return queue_cp(false, socp_indicate, buf, len, offset);
}

/* ---- connection + advertising ------------------------------------------------ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_CGMS_VAL)),
};
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_retry(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(adv_retry_work, adv_retry);

static void start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_ONE_TIME, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err && err != -EALREADY) {
		/* Seen live: refused while the scanner held another address. Without a
		 * retry the phone could not reconnect until the next sensor connection.
		 */
		LOG_WRN("Advertising failed to start (err %d), retrying in 2 s", err);
		k_work_schedule(&adv_retry_work, K_SECONDS(2));
	} else if (!err) {
		LOG_INF("Advertising CGM service as \"%s\"", CONFIG_BT_DEVICE_NAME);
	}
}

static bool is_phone_link(struct bt_conn *conn)
{
	struct bt_conn_info info;

	return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL;
}

static void cgms_connected(struct bt_conn *conn, uint8_t err)
{
	if (err || !is_phone_link(conn)) {
		return;
	}
	phone = bt_conn_ref(conn);
	LOG_INF("Phone connected");
}

static void cgms_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != phone) {
		return;
	}
	LOG_INF("Phone disconnected (reason 0x%02x)", reason);
	bt_conn_unref(phone);
	phone = NULL;
	meas_notify = racp_indicate = socp_indicate = false;
	k_sem_give(&indicate_sem);
}

/*
 * xDrip+'s Nordic BLE manager only starts bonding on the first connection and
 * configures (notifications, RACP, status reads) on a later, bonded one. Drop the
 * link shortly after a fresh pairing so it reconnects and gets on with it.
 */
static void kick_phone(struct k_work *work)
{
	if (phone) {
		LOG_INF("Disconnecting phone so xDrip+ reconnects and re-reads the CGM status");
		bt_conn_disconnect(phone, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void cgms_pairing_complete(struct bt_conn *conn, bool bonded)
{
	if (conn == phone && bonded) {
		k_work_schedule(&kick_work, K_SECONDS(2));
	}
}

static struct bt_conn_auth_info_cb cgms_auth_info_cb = {
	.pairing_complete = cgms_pairing_complete,
};

static void cgms_recycled(void)
{
	if (!phone && !adv_paused) {
		start_advertising();
	}
}

static void adv_retry(struct k_work *work)
{
	cgms_recycled();
}

void g7_cgms_adv_pause(void)
{
	adv_paused = true;
	if (!phone) {
		bt_le_adv_stop();
	}
}

void g7_cgms_adv_resume(void)
{
	adv_paused = false;
	if (!phone) {
		start_advertising();
	}
}

BT_CONN_CB_DEFINE(cgms_conn_callbacks) = {
	.connected = cgms_connected,
	.disconnected = cgms_disconnected,
	.recycled = cgms_recycled,
};

int g7_cgms_init(void)
{
	bt_conn_auth_info_cb_register(&cgms_auth_info_cb);
	start_advertising();
	return 0;
}

void g7_cgms_set_sensor_state(bool session_ended, bool failed)
{
	sensor_status = (session_ended ? SSA_SESSION_STOPPED : 0) |
			(failed ? SSA_SENSOR_MALFUNCTION : 0);
}

void g7_cgms_time_changed(void)
{
	/* xDrip+ only reads CGM Status when a connection becomes ready. */
	if (phone) {
		k_work_schedule(&kick_work, K_SECONDS(1));
	}
}

bool g7_cgms_phone_connected(void)
{
	return phone != NULL;
}

void g7_cgms_notify_reading(const struct g7_record *rec)
{
	if (!phone || !meas_notify || atomic_get(&busy)) {
		return; /* the phone will backfill it via RACP */
	}

	int err = send_measurement(rec, false);

	if (err) {
		LOG_WRN("Glucose notification failed (err %d)", err);
	} else {
		LOG_INF("Sent %u mg/dL to phone", rec->mgdl);
	}
}
