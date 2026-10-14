/* `g7 ...` shell commands, available on the same USB serial console as the logs. */

#include <zephyr/bluetooth/conn.h>
#include <errno.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include "g7_auth.h"
#include "g7_battery.h"
#include "g7_bridge.h"
#include "g7_certstore.h"
#include "g7_config.h"
#include "g7_jpake.h"

static int cmd_g7_set_id(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int err = g7_config_set_id(argv[1]);

	if (err) {
		shell_error(sh, "Failed to save id (err %d)", err);
		return err;
	}

	shell_print(sh,
		    "Sensor id filter set to \"%s\" -- will now only connect to a G7 "
		    "whose advertised name ends with this.",
		    argv[1]);
	return 0;
}

static int cmd_g7_clear_id(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	g7_config_set_id("");
	shell_print(sh,
		    "Sensor id filter cleared -- bridge will NOT auto-connect to anything; "
		    "it will only log discovered G7 transmitters until an id is set.");
	return 0;
}

static int cmd_g7_get_id(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const char *id = g7_config_get_id();

	if (id[0] == '\0') {
		shell_print(sh,
			    "No sensor id filter set -- bridge will only log discovered G7 "
			    "transmitters, it will not connect to any of them.");
	} else {
		shell_print(sh, "Sensor id filter: \"%s\"", id);
	}
	return 0;
}

static int cmd_g7_set_code(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int err = g7_config_set_code(argv[1]);

	if (err) {
		shell_error(sh, "Pairing code must be 4 digits (G7) or 6 digits (ONE+), err %d", err);
		return err;
	}
	g7_config_set_id("");
	g7_bridge_start_search();
	shell_print(sh, "Pairing code set to %s -- searching for the sensor it belongs to "
			"(30 min; `g7 set_id` to pick one by hand).", argv[1]);
	return 0;
}

static int cmd_g7_battery(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct g7_battery b;

	if (!g7_battery_get(&b)) {
		shell_print(sh, "No battery measurement on this board (or not sampled yet).");
		return 0;
	}
	shell_print(sh, "Battery: %u mV, ~%u %%, USB power %s", b.mv, b.pct, b.usb ? "yes" : "no");
	return 0;
}

static int cmd_g7_search(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (g7_config_get_code()[0] == '\0') {
		shell_error(sh, "Set the pairing code first (`g7 set_code 1234`).");
		return -EINVAL;
	}
	g7_config_set_id("");
	g7_bridge_start_search();
	shell_print(sh, "Searching for the sensor pairing code %s belongs to (30 min).",
		    g7_config_get_code());
	return 0;
}

static int cmd_g7_get_code(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const char *code = g7_config_get_code();

	shell_print(sh, "Pairing code: %s", code[0] ? code : "(not set)");
	return 0;
}

struct unpair_ctx {
	bt_addr_le_t sensor;
	bool have_sensor;
	int removed;
};

static void unpair_non_sensor(const struct bt_bond_info *info, void *user_data)
{
	struct unpair_ctx *ctx = user_data;

	if (ctx->have_sensor && bt_addr_le_eq(&info->addr, &ctx->sensor)) {
		return;
	}
	if (bt_unpair(BT_ID_DEFAULT, &info->addr) == 0) {
		ctx->removed++;
	}
}

static int cmd_g7_unpair_phone(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct unpair_ctx ctx = {0};

	ctx.have_sensor = g7_config_get_key_addr(&ctx.sensor);
	if (!ctx.have_sensor) {
		shell_error(sh, "No sensor paired yet -- use `g7 forget` to clear everything.");
		return -ENOENT;
	}
	bt_foreach_bond(BT_ID_DEFAULT, unpair_non_sensor, &ctx);
	shell_print(sh, "Removed %d phone bond(s); the sensor bond is kept. Pair the phone again.",
		    ctx.removed);
	return 0;
}

static int cmd_g7_repair_sensor(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int removed = g7_bridge_repair_sensor();

	shell_print(sh, "Session key forgotten, %d sensor bond(s) removed -- the next connection "
			"pairs the sensor from scratch.", removed);
	return 0;
}

static int cmd_g7_forget(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	g7_config_clear_key();
	bt_unpair(BT_ID_DEFAULT, NULL);
	shell_print(sh, "Forgot the session key and all BLE bonds -- next connection re-pairs "
			"from scratch (EC-JPAKE with the pairing code).");
	return 0;
}

static int cmd_g7_jpake_selftest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Running EC-JPAKE self-test, results in the log...");

	int err = g7_jpake_selftest();

	shell_print(sh, "EC-JPAKE self-test %s", err ? "FAILED" : "PASSED");
	return err;
}

static int cmd_g7_cert_selftest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = g7_auth_cert_selftest();

	shell_print(sh, "Certificate self-test %s", err ? "FAILED" : "PASSED");
	return err;
}

static int cmd_g7_certs(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const src[] = {"none (load them from the setup page)",
					  "stored in flash", "built into this firmware"};

	if (argc > 1 && strcmp(argv[1], "clear") == 0) {
		g7_certs_clear();
	}
	shell_print(sh, "App certificates: %s", src[g7_certs_source()]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(g7_cmds,
	SHELL_CMD_ARG(set_id, NULL,
		      "Set the G7 sensor name suffix to connect to, e.g. `g7 set_id 7A`.\n"
		      "Read candidate suffixes from the discovery log while no filter is set.",
		      cmd_g7_set_id, 2, 0),
	SHELL_CMD_ARG(clear_id, NULL,
		      "Clear the sensor id filter (back to log-only, no auto-connect).",
		      cmd_g7_clear_id, 1, 0),
	SHELL_CMD_ARG(get_id, NULL, "Show the currently configured sensor id filter.",
		      cmd_g7_get_id, 1, 0),
	SHELL_CMD_ARG(set_code, NULL,
		      "Set the 4-digit pairing code printed on the sensor applicator.",
		      cmd_g7_set_code, 2, 0),
	SHELL_CMD_ARG(battery, NULL, "Show the battery voltage and charge estimate.",
		      cmd_g7_battery, 1, 0),
	SHELL_CMD_ARG(search, NULL, "Find the sensor the pairing code belongs to (clears the id).",
		      cmd_g7_search, 1, 0),
	SHELL_CMD_ARG(get_code, NULL, "Show the configured pairing code.", cmd_g7_get_code, 1,
		      0),
	SHELL_CMD_ARG(unpair_phone, NULL,
		      "Forget every bond except the sensor's (phone must pair again).",
		      cmd_g7_unpair_phone, 1, 0),
	SHELL_CMD_ARG(repair_sensor, NULL,
		      "Drop the sensor's bond and key: full re-pair on the next connection.",
		      cmd_g7_repair_sensor, 1, 0),
	SHELL_CMD_ARG(forget, NULL, "Forget the session key and BLE bonds (full re-pair).",
		      cmd_g7_forget, 1, 0),
	SHELL_CMD_ARG(cert_selftest, NULL,
		      "Check the built-in app certificates and ECDSA signing.",
		      cmd_g7_cert_selftest, 1, 0),
	SHELL_CMD_ARG(certs, NULL,
		      "Show where the app certificates come from; `g7 certs clear` removes stored ones.",
		      cmd_g7_certs, 1, 1),
	SHELL_CMD_ARG(jpake_selftest, NULL,
		      "Check the EC-JPAKE port against the known-good transcript.",
		      cmd_g7_jpake_selftest, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(g7, &g7_cmds, "Dexcom G7 bridge configuration", NULL);
