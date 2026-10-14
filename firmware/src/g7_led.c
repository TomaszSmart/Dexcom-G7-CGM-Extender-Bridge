/*
 * Status on the XIAO's RGB LED, so the bridge can be checked without a phone.
 * Steady states are one short flash every few seconds (it sits by a bed at night):
 *
 *   green  flash /5 s   sensor OK, fresh reading, xDrip+ connected
 *   yellow flash /5 s   sensor OK but no phone connected, or sensor warming up
 *   blue   blink 1 Hz   searching for the sensor a new pairing code belongs to
 *   blue   flash /5 s   not set up (no pairing code / sensor)
 *   cyan   flash /2 s   after power-on, waiting for the first reading (15 min)
 *   red    flash /5 s   no reading for 15 min, or the sensor failed / its session ended
 *   red, green, blue    once at boot (LED self-test)
 *
 * Boards with only green and blue (RAK4631) count flashes instead:
 *   yellow -> 2 green flashes, red -> 3 flashes of green+blue, cyan -> green+blue.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "g7_bridge.h"
#include "g7_cgms.h"
#include "g7_config.h"
#include "g7_protocol.h"

LOG_MODULE_REGISTER(g7_led, LOG_LEVEL_INF);

#define STALE_S   (15 * 60)
#define FLASH_MS  80
#define TICK_MS   40

enum color { OFF = 0, RED = 1, GREEN = 2, BLUE = 4, YELLOW = RED | GREEN, CYAN = GREEN | BLUE };

#define HAS_RED    DT_NODE_EXISTS(DT_ALIAS(status_red))
#define FLASH_GAP  250

/* Colours come from per-board aliases in boards/<board>.overlay. */
static const struct {
	struct gpio_dt_spec spec;
	enum color bit;
} leds[] = {
#if HAS_RED
	{GPIO_DT_SPEC_GET(DT_ALIAS(status_red), gpios), RED},
#endif
	{GPIO_DT_SPEC_GET(DT_ALIAS(status_green), gpios), GREEN},
	{GPIO_DT_SPEC_GET(DT_ALIAS(status_blue), gpios), BLUE},
};

static void show(enum color c)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		gpio_pin_set_dt(&leds[i].spec, (c & leds[i].bit) != 0);
	}
}

struct pattern {
	enum color color;
	uint16_t period_ms;
	uint16_t on_ms;
	uint8_t flashes; /* 0 = 1 */
};

/* Without a red LED, colours that need red become flash counts. */
static struct pattern for_board(struct pattern p)
{
	if (!HAS_RED) {
		if (p.color == YELLOW) {
			p.color = GREEN;
			p.flashes = 2;
		} else if (p.color == RED) {
			p.color = CYAN;
			p.flashes = 3;
		}
	}
	return p;
}

static bool lit(const struct pattern *p, int64_t now)
{
	uint32_t phase = now % p->period_ms;
	uint32_t n = p->flashes ? p->flashes : 1;

	if (n == 1) {
		return phase < p->on_ms;
	}
	return phase < n * FLASH_GAP && (phase % FLASH_GAP) < p->on_ms;
}

static struct pattern current_pattern(void)
{
	struct g7_bridge_status st;
	bool configured = g7_config_get_code()[0] != '\0';

	g7_bridge_get_status(&st);

	if (st.searching) {
		return (struct pattern){BLUE, 1000, 500};
	}
	if (!configured || g7_config_get_id()[0] == '\0') {
		return (struct pattern){BLUE, 5000, FLASH_MS};
	}
	if (!st.have_reading) {
		/* Since power-on nothing from the sensor yet. */
		return k_uptime_get() < STALE_S * 1000LL ? (struct pattern){CYAN, 2000, FLASH_MS}
							  : (struct pattern){RED, 5000, FLASH_MS};
	}
	if (g7_state_failed(st.state) || g7_state_session_ended(st.state) ||
	    st.reading_age_s > STALE_S) {
		return (struct pattern){RED, 5000, FLASH_MS};
	}
	if (!g7_state_usable(st.state) || !g7_cgms_phone_connected()) {
		return (struct pattern){YELLOW, 5000, FLASH_MS};
	}
	return (struct pattern){GREEN, 5000, FLASH_MS};
}

static void led_thread(void *p1, void *p2, void *p3)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i].spec) ||
		    gpio_pin_configure_dt(&leds[i].spec, GPIO_OUTPUT_INACTIVE)) {
			LOG_ERR("LED %u not available", i);
			return;
		}
	}

	/* Self-test so a dead LED is not mistaken for "all quiet". */
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		show(leds[i].bit);
		k_msleep(250);
	}
	show(OFF);

	struct pattern p = for_board(current_pattern());
	int64_t next_eval = 0;

	for (;;) {
		int64_t now = k_uptime_get();

		/* Re-evaluate once a second; the pattern runs off the uptime clock. */
		if (now >= next_eval) {
			p = for_board(current_pattern());
			next_eval = now + 1000;
		}
		show(lit(&p, now) ? p.color : OFF);
		k_msleep(TICK_MS);
	}
}

K_THREAD_DEFINE(g7_led_tid, 1024, led_thread, NULL, NULL, NULL, 10, 0, 500);
