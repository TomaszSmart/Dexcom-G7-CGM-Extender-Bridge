/*
 * Dexcom G7 application-layer authentication, driven over GATT. Mirrors the
 * state machine of xDrip+'s libkeks Plugin.aNext()/receivedResponse()/
 * receivedData() -- see ../PROTOCOL_RESEARCH.md section 2a:
 *
 *   EC-JPAKE (3 rounds, only without a saved key) -> AuthRequest 02 ->
 *   challenge 04 -> status 05 -> [certificate exchange 0B/0C/0D, first pairing
 *   of a 4-digit sensor only] -> keepalive 06 19 -> sensor echoes -> BLE bond.
 *
 * All crypto (up to ~0.5 s per step) runs on this module's own thread, never
 * in the Bluetooth RX context; the rx_* entry points only queue the bytes.
 */
#ifndef G7_AUTH_H_
#define G7_AUTH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>

enum g7_auth_result {
	/* Sensor reports authenticated + bonded: go read data. */
	G7_AUTH_OK,
	/* Authenticated, sensor asked for BLE bonding: pair now, then read data. */
	G7_AUTH_BOND_NOW,
	/* Sensor's proof/challenge did not match: wrong pairing code (or old sensor nearby). */
	G7_AUTH_WRONG_CODE,
	/* Sensor needs the app certificates, but this build has none (g7_certs.h). */
	G7_AUTH_NO_CERTS,
	/* Sensor says we are neither authenticated nor bonded: drop our local bond. */
	G7_AUTH_NOT_BONDED,
	/*
	 * The code fits, but the sensor is bonded to us and will not keep a new
	 * session key (it rejected our saved one, or we only have a fresh EC-JPAKE
	 * key on a bonded link). Drop our bond so the next connection goes through
	 * the full pairing (certificates, bond), which the sensor does remember.
	 */
	G7_AUTH_NEEDS_REPAIR,
	/* Protocol error, unexpected reply or timeout. */
	G7_AUTH_ERROR,
};

struct g7_auth_ops {
	/* Write to the `authentication` characteristic (with response). Blocking is fine. */
	int (*write_auth)(const uint8_t *data, size_t len);
	/* Write one <= 20-byte chunk to `ExtraData` (without response). */
	int (*write_extra)(const uint8_t *data, size_t len);
	/* Called once from the auth thread when the exchange ends. */
	void (*finished)(enum g7_auth_result result);
};

void g7_auth_init(const struct g7_auth_ops *ops);

/*
 * Starts authenticating with `peer` using the configured pairing code. Call once
 * `ExtraData` notifications and `authentication` indications are enabled.
 * With `check_code`, a saved session key is ignored and EC-JPAKE runs, so the
 * result says whether the pairing code fits this sensor (search by code).
 */
int g7_auth_begin(const bt_addr_le_t *peer, bool check_code);

/* Feed incoming `authentication` indications / `ExtraData` notifications. */
void g7_auth_rx_auth(const uint8_t *data, size_t len);
void g7_auth_rx_extra(const uint8_t *data, size_t len);

/* Drop any exchange in progress (e.g. on disconnect). */
void g7_auth_abort(void);

const char *g7_auth_result_str(enum g7_auth_result result);

/*
 * Checks the built-in app certificates: signing key matches certificate B and
 * a test signature verifies. Returns -ENOENT if this build has no g7_certs.h.
 */
int g7_auth_cert_selftest(void);

#endif /* G7_AUTH_H_ */
