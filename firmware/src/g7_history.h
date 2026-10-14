/*
 * In-RAM glucose history (a bit over 24 h at the G7's 5-minute cadence), fed by
 * live G7 readings and G7 backfill, served to xDrip+ over the CGM service.
 *
 * Times are "sensor minutes": minutes since the G7 session started (the G7's
 * own clock / 60). They are the CGM service's Time Offset, so xDrip+ -- which
 * takes "now minus the current Time Offset" as the session start -- shows the
 * real sensor age. The clock is only known once a G7 reading has arrived since
 * boot (there is no real-time clock); until then g7_history_now() fails.
 * History is lost on reboot; the next G7 backfill refills it.
 */
#ifndef G7_HISTORY_H_
#define G7_HISTORY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct g7_record {
	uint16_t minute;      /* sensor minutes */
	uint16_t mgdl;
	int8_t trend_tenths;  /* mg/dL/min * 10 */
	bool trend_valid;
};

/* Anchors the sensor clock: the G7 reported `sensor_s` seconds just now. */
void g7_history_sync(uint32_t sensor_s);

/* Current sensor time; false while not synced since boot. */
bool g7_history_now_s(uint32_t *sensor_s);
bool g7_history_now(uint16_t *minute);

/* Drops every record (e.g. a different sensor, whose clock restarted). */
void g7_history_clear(void);

/*
 * Stores a reading, replacing one within +-2 minutes of it (live reading vs.
 * the same point arriving again via backfill). Returns true if it was new.
 */
bool g7_history_add(const struct g7_record *rec);

/* Newest stored record, or false if empty. */
bool g7_history_latest(struct g7_record *out);

/* Number of records with min <= minute <= max. */
size_t g7_history_count(uint16_t min, uint16_t max);

/*
 * Copies the n-th (0 = oldest) record with min <= minute <= max, oldest first.
 * Returns false past the end.
 */
bool g7_history_get(uint16_t min, uint16_t max, size_t n, struct g7_record *out);

#endif /* G7_HISTORY_H_ */
