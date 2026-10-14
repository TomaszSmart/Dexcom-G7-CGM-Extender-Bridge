/*
 * Dexcom G7 GATT protocol constants and message parsing.
 * See ../PROTOCOL_RESEARCH.md for the reverse-engineering notes this is based on
 * (source: LoopKit/G7SensorKit).
 */
#ifndef G7_PROTOCOL_H_
#define G7_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Advertised service, used to spot the sensor while scanning. */
#define G7_ADV_SERVICE_UUID16 0xFEBC

/* CGM service and its characteristics (128-bit Dexcom-defined UUIDs). */
#define G7_CGM_SERVICE_UUID \
	BT_UUID_128_ENCODE(0xf8083532, 0x849e, 0x531c, 0xc594, 0x30f1f86a4ea5)
#define G7_CHAR_COMMUNICATION_UUID \
	BT_UUID_128_ENCODE(0xf8083533, 0x849e, 0x531c, 0xc594, 0x30f1f86a4ea5)
#define G7_CHAR_CONTROL_UUID \
	BT_UUID_128_ENCODE(0xf8083534, 0x849e, 0x531c, 0xc594, 0x30f1f86a4ea5)
#define G7_CHAR_AUTHENTICATION_UUID \
	BT_UUID_128_ENCODE(0xf8083535, 0x849e, 0x531c, 0xc594, 0x30f1f86a4ea5)
#define G7_CHAR_BACKFILL_UUID \
	BT_UUID_128_ENCODE(0xf8083536, 0x849e, 0x531c, 0xc594, 0x30f1f86a4ea5)
/* Bulk data for authentication (EC-JPAKE points, certificates); see xDrip+ libkeks. */
#define G7_CHAR_EXTRA_DATA_UUID \
	BT_UUID_128_ENCODE(0xf8083538, 0x849e, 0x531c, 0xc594, 0x30f1f86a4ea5)

enum g7_opcode {
	G7_OP_AUTH_CHALLENGE_RX = 0x05,
	G7_OP_SESSION_STOP_TX = 0x28,
	G7_OP_GLUCOSE_TX = 0x4e,
	G7_OP_EXTENDED_VERSION_TX = 0x52,
	G7_OP_EXTENDED_VERSION_RX = 0x53,
	G7_OP_BACKFILL_FINISHED = 0x59,
};

/* Payload of an `authentication` indication: opcode, isAuthenticated, isBonded. */
struct g7_auth_status {
	bool authenticated;
	bool bonded;
};

/* Parsed `glucoseTx` (0x4e) push notification from the `control` characteristic. */
struct g7_glucose_reading {
	uint32_t message_timestamp_s; /* seconds since sensor pairing */
	uint16_t sequence;
	uint16_t age_s;             /* sensor-to-BLE delay */
	uint32_t glucose_timestamp_s; /* message_timestamp_s - age_s */
	bool glucose_valid;
	uint16_t glucose_mgdl;       /* meaningless if !glucose_valid */
	uint8_t algorithm_state;
	bool trend_valid;
	int16_t trend_tenths_mgdl_per_min; /* actual trend = this / 10.0; meaningless if !trend_valid */
	bool predicted_valid;
	uint16_t predicted_glucose_mgdl; /* meaningless if !predicted_valid */
	bool display_only;
};

/* Parsed 9-byte record from the `backfill` characteristic (no opcode byte). */
struct g7_backfill_reading {
	uint32_t timestamp_s;
	bool glucose_valid;
	uint16_t glucose_mgdl;
	uint8_t algorithm_state;
	bool display_only;
	bool trend_valid;
	int16_t trend_tenths_mgdl_per_min;
};

/* Extended version reply to a single-byte 0x52 write on `control`. */
struct g7_extended_version {
	uint32_t session_length_s;  /* e.g. 907200 = 10.5 days (10 d + 12 h grace) */
	uint16_t warmup_s;          /* e.g. 1620 = 27 min */
	uint32_t algorithm_version;
	uint8_t hardware_version;
	uint16_t max_lifetime_days;
};

/* Parses `52 00 <len u32> <warmup u16> <algo u32> <hw u8> <max days u16>` (15 bytes). */
bool g7_parse_extended_version(const uint8_t *data, size_t len, struct g7_extended_version *out);

/* Session over: stopped, ended, expired (xDrip+ CalibrationState 0x01/0x0f/0x18/0x1a/0xc2). */
static inline bool g7_state_session_ended(uint8_t state)
{
	return state == 0x01 || state == 0x0f || state == 0x18 || state == 0x1a || state == 0xc2;
}

/* Sensor failure states (xDrip+ "Sensor Failed ..." / transmitter problem / errors). */
static inline bool g7_state_failed(uint8_t state)
{
	return state == 0x0b || state == 0x0c || state == 0x10 || state == 0x11 || state == 0x12 ||
	       (state >= 0x13 && state <= 0x17) || state == 0x19 ||
	       (state >= 0x1b && state <= 0x1e);
}

/* Algorithm states whose glucose is usable (xDrip+ CalibrationState OK / NeedsCalibration). */
static inline bool g7_state_usable(uint8_t state)
{
	return state == 0x06 || state == 0x07;
}

/*
 * Returns the opcode (first byte) of a `control` characteristic notification,
 * or a negative errno if the buffer is empty.
 */
int g7_peek_opcode(const uint8_t *data, size_t len);

/* Parses a 3-byte authentication indication ([0x05, isAuthenticated, isBonded]). */
bool g7_parse_auth_status(const uint8_t *data, size_t len, struct g7_auth_status *out);

/* Parses a glucoseTx (opcode 0x4e) notification, at least 19 bytes. */
bool g7_parse_glucose_tx(const uint8_t *data, size_t len, struct g7_glucose_reading *out);

/* Parses one 9-byte backfill record. */
bool g7_parse_backfill(const uint8_t *data, size_t len, struct g7_backfill_reading *out);

#endif /* G7_PROTOCOL_H_ */
