#include "g7_config.h"

#include <errno.h>
#include <string.h>

#include <zephyr/settings/settings.h>

static char stored_id[G7_CONFIG_ID_MAX_LEN + 1];
static char stored_code[G7_CONFIG_CODE_MAX_LEN + 1];

/* Session key and the sensor address it was negotiated with. */
static struct __packed {
	bt_addr_le_t addr;
	uint8_t key[G7_CONFIG_KEY_LEN];
} stored_key;
static bool have_key;

/* Session parameters of the sensor at `addr` (survive reboots). */
static struct __packed {
	bt_addr_le_t addr;
	struct g7_session_info info;
} stored_session;
static bool have_session;

static int read_string(char *dst, size_t max_len, size_t len, settings_read_cb read_cb,
		       void *cb_arg)
{
	if (len > max_len) {
		return -EINVAL;
	}

	ssize_t rc = read_cb(cb_arg, dst, len);

	if (rc < 0) {
		return rc;
	}

	dst[len] = '\0';
	return 0;
}

static int g7cfg_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "id", &next) && !next) {
		return read_string(stored_id, G7_CONFIG_ID_MAX_LEN, len, read_cb, cb_arg);
	}
	if (settings_name_steq(name, "code", &next) && !next) {
		return read_string(stored_code, G7_CONFIG_CODE_MAX_LEN, len, read_cb, cb_arg);
	}
	if (settings_name_steq(name, "sess", &next) && !next) {
		if (len != sizeof(stored_session)) {
			return -EINVAL;
		}

		ssize_t rc = read_cb(cb_arg, &stored_session, len);

		have_session = rc == sizeof(stored_session);
		return rc < 0 ? rc : 0;
	}
	if (settings_name_steq(name, "key", &next) && !next) {
		if (len != sizeof(stored_key)) {
			have_key = false;
			return len == 0 ? 0 : -EINVAL;
		}

		ssize_t rc = read_cb(cb_arg, &stored_key, len);

		have_key = rc == sizeof(stored_key);
		return rc < 0 ? rc : 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(g7cfg, "g7cfg", NULL, g7cfg_set, NULL, NULL);

const char *g7_config_get_id(void)
{
	return stored_id;
}

int g7_config_set_id(const char *id)
{
	size_t len = strlen(id);

	if (len > G7_CONFIG_ID_MAX_LEN) {
		return -EINVAL;
	}

	memcpy(stored_id, id, len + 1);

	return settings_save_one("g7cfg/id", stored_id, len);
}

const char *g7_config_get_code(void)
{
	return stored_code;
}

int g7_config_set_code(const char *code)
{
	size_t len = strlen(code);

	if (len != 0 && len != 4 && len != 6) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		if (code[i] < '0' || code[i] > '9') {
			return -EINVAL;
		}
	}

	memcpy(stored_code, code, len + 1);

	int err = settings_save_one("g7cfg/code", stored_code, len);

	return err;
}

bool g7_config_get_key(const bt_addr_le_t *addr, uint8_t key[G7_CONFIG_KEY_LEN])
{
	if (!have_key || !bt_addr_le_eq(addr, &stored_key.addr)) {
		return false;
	}
	memcpy(key, stored_key.key, G7_CONFIG_KEY_LEN);
	return true;
}

bool g7_config_get_key_addr(bt_addr_le_t *addr)
{
	if (have_key) {
		bt_addr_le_copy(addr, &stored_key.addr);
	}
	return have_key;
}

int g7_config_set_key(const bt_addr_le_t *addr, const uint8_t key[G7_CONFIG_KEY_LEN])
{
	bt_addr_le_copy(&stored_key.addr, addr);
	memcpy(stored_key.key, key, G7_CONFIG_KEY_LEN);
	have_key = true;
	return settings_save_one("g7cfg/key", &stored_key, sizeof(stored_key));
}

bool g7_config_get_session(const bt_addr_le_t *addr, struct g7_session_info *out)
{
	if (!have_session || !bt_addr_le_eq(addr, &stored_session.addr)) {
		return false;
	}
	*out = stored_session.info;
	return true;
}

static void session_for(const bt_addr_le_t *addr)
{
	if (!have_session || !bt_addr_le_eq(addr, &stored_session.addr)) {
		memset(&stored_session, 0, sizeof(stored_session));
		bt_addr_le_copy(&stored_session.addr, addr);
		have_session = true;
	}
}

int g7_config_set_session_length(const bt_addr_le_t *addr, uint32_t length_s, uint16_t warmup_s)
{
	session_for(addr);
	stored_session.info.length_s = length_s;
	stored_session.info.warmup_s = warmup_s;
	return settings_save_one("g7cfg/sess", &stored_session, sizeof(stored_session));
}

int g7_config_set_session_start(const bt_addr_le_t *addr, int64_t start_utc_s)
{
	session_for(addr);
	stored_session.info.start_utc_s = start_utc_s;
	return settings_save_one("g7cfg/sess", &stored_session, sizeof(stored_session));
}

int g7_config_clear_key(void)
{
	have_key = false;
	return settings_delete("g7cfg/key");
}
