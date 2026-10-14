/*
 * Persisted bridge configuration: which specific G7 sensor to connect to, its
 * applicator pairing code, and the session key negotiated with it.
 *
 * The board advertises with a GAP name like "DXCM01" -- multiple sensors
 * (yours, a neighbour's, a leftover old one) can be in range at once, so we
 * must not just grab the first thing that looks like a G7. Until a sensor id
 * is configured, the bridge only *logs* candidates and refuses to connect.
 */
#ifndef G7_CONFIG_H_
#define G7_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>

#define G7_CONFIG_ID_MAX_LEN   8
#define G7_CONFIG_CODE_MAX_LEN 6
#define G7_CONFIG_KEY_LEN      16

/* Currently configured suffix to match against the tail of the GAP name, or
 * "" if none is set yet (bridge will not auto-connect to anything).
 */
const char *g7_config_get_id(void);

/* Sets and persists (survives reboot) the sensor id filter. Pass "" to clear
 * it. Returns 0 on success, -EINVAL if id is too long, or a settings error.
 */
int g7_config_set_id(const char *id);

/* 4-digit applicator pairing code (6 for Dexcom ONE+), or "" if not set. */
const char *g7_config_get_code(void);

/* Sets and persists the pairing code. Pass "" to clear. Session keys are kept:
 * they belong to a sensor address, and a sensor that is already paired only
 * accepts the key from its full pairing (see G7_AUTH_NEEDS_REPAIR).
 */
int g7_config_set_code(const char *code);

/* Copies the EC-JPAKE session key saved for `addr` into `key`. Returns false
 * if there is none (or it belongs to another sensor).
 */
bool g7_config_get_key(const bt_addr_le_t *addr, uint8_t key[G7_CONFIG_KEY_LEN]);

/* Address of the sensor the saved session key belongs to; false if none. */
bool g7_config_get_key_addr(bt_addr_le_t *addr);

/* Saves the session key for `addr`, replacing any previous one. */
int g7_config_set_key(const bt_addr_le_t *addr, const uint8_t key[G7_CONFIG_KEY_LEN]);

/* What we know about the current sensor's session (kept in flash). */
struct g7_session_info {
	uint32_t length_s;     /* from the G7's 0x52 reply, e.g. 907200 = 10.5 days; 0 = unknown */
	uint16_t warmup_s;
	int64_t start_utc_s;   /* session start, Unix time, learned from the phone; 0 = unknown */
};

/* Session info saved for `addr`; false if nothing is known about this sensor. */
bool g7_config_get_session(const bt_addr_le_t *addr, struct g7_session_info *out);

/* Updates one part of `addr`'s session info (a new sensor starts from scratch). */
int g7_config_set_session_length(const bt_addr_le_t *addr, uint32_t length_s, uint16_t warmup_s);
int g7_config_set_session_start(const bt_addr_le_t *addr, int64_t start_utc_s);

/* Forgets the saved session key. */
int g7_config_clear_key(void);

#endif /* G7_CONFIG_H_ */
