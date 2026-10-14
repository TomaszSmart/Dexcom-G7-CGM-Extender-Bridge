#include "g7_protocol.h"

#include <errno.h>
#include <string.h>

static inline uint16_t rd_u16le(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t rd_u32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static inline uint32_t rd_u24le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

int g7_peek_opcode(const uint8_t *data, size_t len)
{
	if (len < 1) {
		return -EINVAL;
	}
	return data[0];
}

bool g7_parse_auth_status(const uint8_t *data, size_t len, struct g7_auth_status *out)
{
	if (len < 3 || data[0] != G7_OP_AUTH_CHALLENGE_RX) {
		return false;
	}

	out->authenticated = data[1] != 0;
	out->bonded = data[2] != 0;
	return true;
}

bool g7_parse_glucose_tx(const uint8_t *data, size_t len, struct g7_glucose_reading *out)
{
	if (len < 19 || data[0] != G7_OP_GLUCOSE_TX) {
		return false;
	}

	out->message_timestamp_s = rd_u32le(&data[2]);
	out->sequence = rd_u16le(&data[6]);
	out->age_s = rd_u16le(&data[10]);
	out->glucose_timestamp_s = out->message_timestamp_s - out->age_s;

	uint16_t glucose_raw = rd_u16le(&data[12]);

	out->glucose_valid = glucose_raw != 0xFFFF;
	out->glucose_mgdl = glucose_raw & 0x0FFF;

	out->algorithm_state = data[14];

	int8_t trend_raw = (int8_t)data[15];

	out->trend_valid = trend_raw != 0x7F;
	out->trend_tenths_mgdl_per_min = trend_raw;

	uint16_t predicted_raw = rd_u16le(&data[16]);

	out->predicted_valid = predicted_raw != 0xFFFF;
	out->predicted_glucose_mgdl = predicted_raw & 0x0FFF;

	out->display_only = (data[18] & 0x10) != 0;

	return true;
}

bool g7_parse_backfill(const uint8_t *data, size_t len, struct g7_backfill_reading *out)
{
	if (len < 9) {
		return false;
	}

	out->timestamp_s = rd_u24le(&data[0]);

	uint16_t glucose_raw = rd_u16le(&data[4]);

	out->glucose_valid = glucose_raw != 0xFFFF;
	out->glucose_mgdl = glucose_raw & 0x0FFF;

	out->algorithm_state = data[6];
	out->display_only = (data[7] & 0x10) != 0;

	int8_t trend_raw = (int8_t)data[8];

	out->trend_valid = trend_raw != 0x7F;
	out->trend_tenths_mgdl_per_min = trend_raw;

	return true;
}

bool g7_parse_extended_version(const uint8_t *data, size_t len, struct g7_extended_version *out)
{
	if (len < 15 || data[0] != G7_OP_EXTENDED_VERSION_TX || data[1] != 0x00) {
		return false;
	}

	out->session_length_s = rd_u32le(&data[2]);
	out->warmup_s = rd_u16le(&data[6]);
	out->algorithm_version = rd_u32le(&data[8]);
	out->hardware_version = data[12];
	out->max_lifetime_days = rd_u16le(&data[13]);
	return true;
}
