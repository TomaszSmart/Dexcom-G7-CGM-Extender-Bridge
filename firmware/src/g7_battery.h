#ifndef G7_BATTERY_H_
#define G7_BATTERY_H_

#include <stdbool.h>
#include <stdint.h>

struct g7_battery {
	uint16_t mv;   /* cell voltage */
	uint8_t pct;   /* rough charge estimate from the voltage */
	bool usb;      /* USB power present (charging, or no battery at all) */
};

/* False on boards without a battery measurement, or before the first sample. */
bool g7_battery_get(struct g7_battery *b);

#endif /* G7_BATTERY_H_ */
