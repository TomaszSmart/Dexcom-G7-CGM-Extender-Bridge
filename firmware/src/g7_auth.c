/*
 * Dexcom G7 application-layer authentication state machine (see g7_auth.h).
 * State names and transitions follow libkeks' Plugin.java so the two can be
 * compared side by side; comments quote the Java state they correspond to.
 */
#include "g7_auth.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "g7_certstore.h"
#include "g7_config.h"
#include "g7_jpake.h"
#include "g7_protocol.h"

LOG_MODULE_REGISTER(g7_auth, LOG_LEVEL_INF);

#define CHUNK_LEN        20
#define CHUNK_GAP_MS     40  /* xDrip+ doNext(): threadSleep(40) between chunks */
#define AFTER_DATA_MS    500 /* ... and threadSleep(500) before the command */
#define STEP_TIMEOUT     K_SECONDS(15)
#define RX_MAX           64

enum state {
	ST_IDLE,
	ST_ROUND1,         /* Round1: sent 0A 00, collecting the sensor's round 1 packet */
	ST_ROUND2,         /* Round2 */
	ST_ROUND3,         /* Round3 */
	ST_REQUEST_AUTH,   /* RequestAuth: sent 02 <token> <slot> */
	ST_CHALLENGE_REPLY,/* ChallengeReply: sent 04 <hash>, waiting for 05 status */
	ST_CERT_INFO_A,    /* SendCertificate1: sent 0B 00 <len>, waiting for 0B reply */
	ST_CERT_DATA_A,    /* SendCertificate1out: sent cert A, collecting the sensor's */
	ST_CERT_INFO_B,    /* SendCertificate2 */
	ST_CERT_DATA_B,    /* SendCertificate2out */
	ST_SIGN_CHALLENGE, /* SendKeyChallenge: sent 0C <16 random>, waiting for its challenge */
	ST_SIGN_REPLY,     /* SendKeyChallengeOut: sent signature + 0D 00 02 */
	ST_KEEPALIVE,      /* GetData/Pairing: sent 06 19, waiting for the bond trigger */
};

enum ev_type {
	EV_START,
	EV_RX_AUTH,
	EV_RX_EXTRA,
	EV_ABORT,
};

struct ev {
	uint8_t type;
	uint8_t len;
	uint8_t data[RX_MAX];
};

/* A 160-byte round packet arrives as 8+ notifications, a certificate as ~25. */
K_MSGQ_DEFINE(auth_q, sizeof(struct ev), 40, 4);

static const struct g7_auth_ops *ops;

static struct {
	enum state st;
	bt_addr_le_t peer;
	bool saved_key;       /* key came from settings, JPAKE skipped */
	bool check_code;      /* ignore any saved key: prove the pairing code */
	bool token_mismatch;  /* sensor's 03 reply did not match our key */
	uint8_t key[G7_JPAKE_KEY_LEN];
	struct g7_jpake jp;
	uint8_t acc[G7_JPAKE_PACKET_LEN];
	size_t acc_len;
	size_t cert_expected;
	size_t cert_rx;
	uint8_t token[G7_CHALLENGE_LEN];
} s;

/* Sensor replies to 06 19 that mean "create the BLE bond now" (Plugin.bondNow). */
static const uint8_t bond_trigger[][3] = {{0x06, 0x19}, {0xff, 0x06, 0x01}, {0x06, 0x00}};
static const uint8_t bond_trigger_len[] = {2, 3, 2};

static const uint8_t keepalive[] = {0x06, 0x19};
static const uint8_t challenge_out[] = {0x0d, 0x00, 0x02};

const char *g7_auth_result_str(enum g7_auth_result result)
{
	switch (result) {
	case G7_AUTH_OK:
		return "authenticated + bonded";
	case G7_AUTH_BOND_NOW:
		return "authenticated, bond requested";
	case G7_AUTH_WRONG_CODE:
		return "wrong pairing code (or an old sensor nearby)";
	case G7_AUTH_NO_CERTS:
		return "sensor needs app certificates, none loaded (setup page)";
	case G7_AUTH_NOT_BONDED:
		return "sensor does not know us -- local bond must go";
	case G7_AUTH_NEEDS_REPAIR:
		return "sensor needs a full re-pair -- dropping our bond";
	default:
		return "protocol error";
	}
}

/* ---- sending -------------------------------------------------------------- */

static int send_extra(const uint8_t *data, size_t len)
{
	for (size_t off = 0; off < len; off += CHUNK_LEN) {
		int err = ops->write_extra(data + off, MIN(CHUNK_LEN, len - off));

		if (err) {
			LOG_ERR("ExtraData write failed (err %d)", err);
			return err;
		}
		k_msleep(CHUNK_GAP_MS);
	}
	k_msleep(AFTER_DATA_MS);
	return 0;
}

static int send_auth(const uint8_t *data, size_t len)
{
	LOG_HEXDUMP_DBG(data, len, "auth tx");

	int err = ops->write_auth(data, len);

	if (err) {
		LOG_ERR("authentication write failed (err %d)", err);
	}
	return err;
}

/* sequencePacket(): optional 160-byte packet on ExtraData, then 0A <round-1>. */
static int send_round(int next_round_param, const uint8_t *pkt)
{
	const uint8_t cmd[] = {0x0a, next_round_param};

	if (pkt) {
		int err = send_extra(pkt, G7_JPAKE_PACKET_LEN);

		if (err) {
			return err;
		}
	}
	return send_auth(cmd, sizeof(cmd));
}

/* AuthRequestTxMessage2: 02 <8-byte token> <slot>. */
static int send_auth_request(void)
{
	uint8_t msg[2 + G7_CHALLENGE_LEN];

	sys_csrand_get(s.token, sizeof(s.token));
	msg[0] = 0x02;
	memcpy(msg + 1, s.token, sizeof(s.token));
	/* Always slot 2, as Juggluco does. xDrip+ can switch to 1 once bonded
	 * (needsBonding()), but a bonded G7 drops the link on that (2026-09-28).
	 */
	msg[1 + G7_CHALLENGE_LEN] = 0x02;
	return send_auth(msg, sizeof(msg));
}

/* ---- finishing ------------------------------------------------------------ */

static void finish(enum g7_auth_result result)
{
	if (s.st == ST_IDLE) {
		return;
	}
	LOG_INF("Authentication finished: %s", g7_auth_result_str(result));
	s.st = ST_IDLE;
	memset(&s.jp, 0, sizeof(s.jp));
	ops->finished(result);
}

static void save_key(void)
{
	if (!s.saved_key) {
		int err = g7_config_set_key(&s.peer, s.key);

		if (err) {
			LOG_WRN("Could not persist session key (err %d)", err);
		} else {
			s.saved_key = true;
		}
	}
}

/* ---- JPAKE rounds (ExtraData) ---------------------------------------------- */

static void on_round_packet(int round)
{
	static uint8_t pkt[G7_JPAKE_PACKET_LEN];
	int err = g7_jpake_read_round(&s.jp, round, s.acc);

	s.acc_len = 0;
	if (err) {
		LOG_ERR("Sensor's round %d proof rejected (err %d)", round, err);
		finish(G7_AUTH_ERROR);
		return;
	}
	LOG_INF("Sensor's round %d accepted", round);

	err = g7_jpake_write_round(&s.jp, round, NULL, pkt);
	if (err) {
		LOG_ERR("Building our round %d failed (err %d)", round, err);
		finish(G7_AUTH_ERROR);
		return;
	}

	if (round < 3) {
		err = send_round(round, pkt);
		s.st = round == 1 ? ST_ROUND2 : ST_ROUND3;
	} else {
		err = g7_jpake_derive_key(&s.jp, s.key);
		if (!err) {
			LOG_INF("EC-JPAKE complete, session key derived");
			err = send_extra(pkt, sizeof(pkt));
		}
		if (!err) {
			err = send_auth_request();
			s.st = ST_REQUEST_AUTH;
		}
	}
	if (err) {
		finish(G7_AUTH_ERROR);
	}
}

static void on_extra(const uint8_t *data, size_t len)
{
	LOG_HEXDUMP_DBG(data, len, "extra rx");

	switch (s.st) {
	case ST_ROUND1:
	case ST_ROUND2:
	case ST_ROUND3: {
		size_t n = MIN(len, sizeof(s.acc) - s.acc_len);

		memcpy(s.acc + s.acc_len, data, n);
		s.acc_len += n;
		if (s.acc_len == sizeof(s.acc)) {
			on_round_packet(s.st - ST_ROUND1 + 1);
		}
		break;
	}
	case ST_CERT_INFO_A:
	case ST_CERT_INFO_B:
		/* libkeks keeps accumulating here (dontClearAccumulator). */
		s.cert_rx += len;
		break;
	case ST_CERT_DATA_A:
	case ST_CERT_DATA_B:
		s.cert_rx += len;
		if (s.cert_rx < s.cert_expected) {
			break;
		}
		LOG_INF("Received sensor certificate %c (%zu bytes)",
			s.st == ST_CERT_DATA_A ? 'A' : 'B', s.cert_rx);
		s.cert_rx = 0;
		if (s.st == ST_CERT_DATA_A) {
			uint8_t msg[6] = {0x0b, 0x01};
			size_t b_len;

			g7_certs_b(&b_len);
			sys_put_le32(b_len, msg + 2);
			s.st = ST_CERT_INFO_B;
			if (send_auth(msg, sizeof(msg))) {
				finish(G7_AUTH_ERROR);
			}
		} else {
			uint8_t msg[17] = {0x0c};

			sys_csrand_get(msg + 1, 16);
			s.st = ST_SIGN_CHALLENGE;
			if (send_auth(msg, sizeof(msg))) {
				finish(G7_AUTH_ERROR);
			}
		}
		break;
	default:
		/* e.g. the sensor's 64-byte signature during SendKeyChallenge; unused. */
		break;
	}
}

/* ---- authentication indications -------------------------------------------- */

int g7_auth_cert_selftest(void)
{
	return g7_certs_selftest();
}

static void start_cert_exchange(void)
{
	uint8_t msg[6] = {0x0b, 0x00};
	size_t a_len;

	g7_certs_a(&a_len);
	sys_put_le32(a_len, msg + 2);
	s.cert_rx = 0;
	s.st = ST_CERT_INFO_A;
	if (send_auth(msg, sizeof(msg))) {
		finish(G7_AUTH_ERROR);
	}
}

/* CertInfoRxMessage: 0B <state> <which> <size16 LE> .. (7 bytes). */
static void on_cert_info(const uint8_t *data, size_t len)
{
	if (len != 7 || data[0] != 0x0b || data[1] != 0 || sys_get_le16(data + 3) == 0) {
		LOG_ERR("Sensor rejected certificate %c", s.st == ST_CERT_INFO_A ? 'A' : 'B');
		finish(G7_AUTH_ERROR);
		return;
	}
	s.cert_expected = sys_get_le16(data + 3);

	bool a = s.st == ST_CERT_INFO_A;
	size_t cert_len;
	const uint8_t *cert = a ? g7_certs_a(&cert_len) : g7_certs_b(&cert_len);

	LOG_INF("Sending certificate %c, sensor's is %zu bytes", a ? 'A' : 'B', s.cert_expected);
	s.st = a ? ST_CERT_DATA_A : ST_CERT_DATA_B;
	if (send_extra(cert, cert_len)) {
		finish(G7_AUTH_ERROR);
	}
}

static void on_auth_status(const uint8_t *data, size_t len)
{
	struct g7_auth_status st;

	if (len < 3 || data[0] != G7_OP_AUTH_CHALLENGE_RX) {
		LOG_ERR("Expected auth status, got opcode 0x%02x", len ? data[0] : 0);
		finish(G7_AUTH_ERROR);
		return;
	}
	st.authenticated = data[1] == 1;
	st.bonded = data[2] == 1;
	LOG_INF("Auth status: authenticated=%u bonded=%u", data[1], data[2]);

	if (s.token_mismatch) {
		if (!st.authenticated) {
			/* Our saved key is no longer the sensor's. */
			g7_config_clear_key();
			finish(st.bonded ? G7_AUTH_NEEDS_REPAIR : G7_AUTH_WRONG_CODE);
			return;
		}
		LOG_ERR("Sensor accepted us although its token hash did not match -- "
			"our token check is wrong, continuing");
	}

	if (data[2] == 3 || !st.authenticated) {
		/* needsRefresh() / failed: next connection redoes EC-JPAKE. */
		g7_config_clear_key();
	}
	if (!st.authenticated) {
		finish(st.bonded ? G7_AUTH_ERROR : G7_AUTH_NOT_BONDED);
		return;
	}

	if (st.bonded && !s.saved_key) {
		/* Seen live: a bonded G7 accepts a fresh EC-JPAKE for this link only,
		 * rejects the new key next time and drops the link before any data.
		 * Only the full pairing path makes it keep a key.
		 */
		finish(G7_AUTH_NEEDS_REPAIR);
		return;
	}

	save_key();
	if (st.bonded) {
		finish(G7_AUTH_OK);
	} else if (strlen(g7_config_get_code()) > 4) {
		/* Pairing: 6-digit codes skip the certificate exchange. */
		s.st = ST_KEEPALIVE;
		if (send_auth(keepalive, sizeof(keepalive))) {
			finish(G7_AUTH_ERROR);
		}
	} else if (g7_certs_source() == G7_CERTS_NONE) {
		finish(G7_AUTH_NO_CERTS);
	} else {
		start_cert_exchange();
	}
}

static bool is_bond_trigger(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < ARRAY_SIZE(bond_trigger); i++) {
		if (len == bond_trigger_len[i] && memcmp(data, bond_trigger[i], len) == 0) {
			return true;
		}
	}
	return false;
}

static void on_auth(const uint8_t *data, size_t len)
{
	uint8_t msg[1 + G7_CHALLENGE_LEN];

	LOG_HEXDUMP_DBG(data, len, "auth rx");

	if (is_bond_trigger(data, len)) {
		save_key();
		finish(G7_AUTH_BOND_NOW);
		return;
	}

	switch (s.st) {
	case ST_REQUEST_AUTH:
		/* 03 <hash of our token, 8> <sensor challenge, 8> */
		if (len < 17 || data[0] != 0x03) {
			LOG_ERR("Unexpected AuthRequest reply (%zu bytes)", len);
			finish(G7_AUTH_ERROR);
			return;
		}
		g7_challenge_hash(s.key, s.token, msg);
		s.token_mismatch = memcmp(msg, data + 1, G7_CHALLENGE_LEN) != 0;
		if (s.token_mismatch) {
			if (!s.saved_key) {
				/* libkeks "Mismatch - wait": wrong code, or another sensor
				 * (or one still held by another app). Stop right here.
				 */
				finish(G7_AUTH_WRONG_CODE);
				return;
			}
			/* libkeks does not check this with a saved key; the 05 status
			 * will tell (on_auth_status()).
			 */
			LOG_WRN("Sensor's token hash does not match the saved key");
		}
		msg[0] = 0x04;
		g7_challenge_hash(s.key, data + 9, msg + 1);
		s.st = ST_CHALLENGE_REPLY;
		if (send_auth(msg, sizeof(msg))) {
			finish(G7_AUTH_ERROR);
		}
		break;

	case ST_CHALLENGE_REPLY:
		on_auth_status(data, len);
		break;

	case ST_CERT_INFO_A:
	case ST_CERT_INFO_B:
		on_cert_info(data, len);
		break;

	case ST_SIGN_CHALLENGE: {
		static uint8_t sig[64];

		/* 0C <status> <16-byte challenge> */
		if (len < 18 || data[1] != 0) {
			LOG_ERR("Sensor rejected our key challenge");
			finish(G7_AUTH_ERROR);
			return;
		}
		if (g7_certs_sign(data + 2, sig)) {
			finish(G7_AUTH_ERROR);
			return;
		}
		LOG_INF("Signed sensor challenge");
		s.st = ST_SIGN_REPLY;
		if (send_extra(sig, sizeof(sig)) || send_auth(challenge_out, sizeof(challenge_out))) {
			finish(G7_AUTH_ERROR);
		}
		break;
	}

	case ST_SIGN_REPLY:
		s.st = ST_KEEPALIVE;
		if (send_auth(keepalive, sizeof(keepalive))) {
			finish(G7_AUTH_ERROR);
		}
		break;

	default:
		/* Acks to 0A xx during the rounds etc. carry nothing we need. */
		break;
	}
}

/* ---- thread ------------------------------------------------------------------ */

static void start(void)
{
	s.acc_len = 0;
	s.token_mismatch = false;
	s.saved_key = !s.check_code && g7_config_get_key(&s.peer, s.key);

	if (s.saved_key) {
		LOG_INF("Using saved session key, skipping EC-JPAKE");
		s.st = ST_REQUEST_AUTH;
		if (send_auth_request()) {
			finish(G7_AUTH_ERROR);
		}
		return;
	}

	int err = g7_jpake_init(&s.jp, g7_config_get_code());

	if (err) {
		LOG_ERR("EC-JPAKE init failed (err %d)", err);
		s.st = ST_ROUND1; /* so finish() reports it */
		finish(G7_AUTH_ERROR);
		return;
	}
	LOG_INF("Starting EC-JPAKE with pairing code");
	s.st = ST_ROUND1;
	if (send_round(0, NULL)) {
		finish(G7_AUTH_ERROR);
	}
}

static void auth_thread(void *p1, void *p2, void *p3)
{
	struct ev ev;

	for (;;) {
		k_timeout_t wait = s.st == ST_IDLE ? K_FOREVER : STEP_TIMEOUT;

		int rc = k_msgq_get(&auth_q, &ev, wait);

		if (rc == -EAGAIN) {
			LOG_ERR("Timed out waiting for the sensor (state %d)", s.st);
			finish(G7_AUTH_ERROR);
			continue;
		}
		if (rc) {
			continue; /* -ENOMSG: queue purged by g7_auth_abort() */
		}

		switch (ev.type) {
		case EV_START:
			start();
			break;
		case EV_RX_AUTH:
			if (s.st != ST_IDLE) {
				on_auth(ev.data, ev.len);
			}
			break;
		case EV_RX_EXTRA:
			if (s.st != ST_IDLE) {
				on_extra(ev.data, ev.len);
			}
			break;
		case EV_ABORT:
			s.st = ST_IDLE;
			memset(&s.jp, 0, sizeof(s.jp));
			break;
		}
	}
}

K_THREAD_DEFINE(g7_auth_tid, 3072, auth_thread, NULL, NULL, NULL, 7, 0, 0);

/* ---- entry points (any context) ------------------------------------------- */

static void post(enum ev_type type, const uint8_t *data, size_t len)
{
	struct ev ev = {.type = type, .len = MIN(len, RX_MAX)};

	if (data) {
		memcpy(ev.data, data, ev.len);
	}
	if (k_msgq_put(&auth_q, &ev, K_NO_WAIT)) {
		LOG_ERR("Auth queue full, dropped event %d", type);
	}
}

void g7_auth_init(const struct g7_auth_ops *auth_ops)
{
	ops = auth_ops;
}

int g7_auth_begin(const bt_addr_le_t *peer, bool check_code)
{
	const char *code = g7_config_get_code();

	if (strlen(code) != 4 && strlen(code) != 6) {
		LOG_ERR("No pairing code configured (see `g7 set_code`)");
		return -EINVAL;
	}

	/* Written before EV_START is queued; the thread only reads them after. */
	bt_addr_le_copy(&s.peer, peer);
	s.check_code = check_code;
	post(EV_START, NULL, 0);
	return 0;
}

void g7_auth_rx_auth(const uint8_t *data, size_t len)
{
	post(EV_RX_AUTH, data, len);
}

void g7_auth_rx_extra(const uint8_t *data, size_t len)
{
	post(EV_RX_EXTRA, data, len);
}

void g7_auth_abort(void)
{
	k_msgq_purge(&auth_q);
	post(EV_ABORT, NULL, 0);
}
