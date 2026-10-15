/*
 * Output side: the bridge as a standard Bluetooth SIG Continuous Glucose
 * Monitoring Service (0x181F) peripheral, for xDrip+'s "GluPro" collector
 * (xDrip+ libglupro, lwld.glucose.profile.GluProBle, via Nordic's Android BLE
 * library). xDrip+ bonds (Just Works), enables measurement notifications,
 * reads CGM Status to anchor our Time Offset minutes to wall-clock time, and
 * fills gaps itself through the Record Access Control Point.
 */
#ifndef G7_CGMS_H_
#define G7_CGMS_H_

#include "g7_history.h"

/* Starts connectable advertising (service 0x181F). Call after bt_enable(). */
int g7_cgms_init(void);

/*
 * Stops/restarts our advertising around a connection attempt to the G7: with
 * scanning, advertising and the phone link all sharing the radio, the G7's
 * short wake window was often missed ("failed to establish").
 */
void g7_cgms_adv_pause(void);
void g7_cgms_adv_resume(void);

/* Sensor state for CGM Status: session over (stopped/ended/expired), or failed. */
void g7_cgms_set_sensor_state(bool session_ended, bool failed);

/*
 * The sensor clock was (re)learned -- first reading after boot, or another
 * sensor. Reconnects the phone so xDrip+ re-reads CGM Status and its session start.
 */
void g7_cgms_time_changed(void);

/* True while a phone is connected (xDrip+, or only the setup app). */
bool g7_cgms_phone_connected(void);

/* True while the connected phone collects readings: it has used the CGM service. */
bool g7_cgms_collecting(void);

/* Pushes a new live reading to a connected, subscribed phone (no-op otherwise). */
void g7_cgms_notify_reading(const struct g7_record *rec);

#endif /* G7_CGMS_H_ */
