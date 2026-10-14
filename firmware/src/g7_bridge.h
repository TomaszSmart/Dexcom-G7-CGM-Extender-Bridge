/*
 * What the bridge's sensor side (main.c) exposes to the configuration UI
 * (g7_ui.c): a status snapshot, and starting the search for a new sensor.
 */
#ifndef G7_BRIDGE_H_
#define G7_BRIDGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define G7_BRIDGE_MAX_SEEN 6

struct g7_seen_sensor {
	char name[9];         /* e.g. "DXCMab" */
	int8_t rssi;
	uint16_t seen_s_ago;
	bool tried;           /* the current pairing code did not fit it */
	bool paired;          /* the sensor we are using */
};

struct g7_bridge_status {
	bool searching;               /* trying unknown sensors with the pairing code */
	uint16_t search_left_s;
	bool have_reading;
	uint16_t mgdl;                /* last reading from the sensor, forwarded or not */
	uint8_t state;                /* its G7 algorithm state */
	int8_t trend_tenths;
	bool trend_valid;
	uint32_t reading_age_s;
	size_t n_seen;
	struct g7_seen_sensor seen[G7_BRIDGE_MAX_SEEN];
};

void g7_bridge_get_status(struct g7_bridge_status *st);

/*
 * With a pairing code set and no sensor id, try each G7 in range once until
 * the code fits one (then its name becomes the sensor id). Runs for 30 min.
 */
void g7_bridge_start_search(void);

/*
 * Drops our bond and session key with the configured sensor so the next
 * connection does a full pairing. Returns how many bonds were removed (0 if the
 * sensor has not been seen since boot).
 */
int g7_bridge_repair_sensor(void);

#endif /* G7_BRIDGE_H_ */
