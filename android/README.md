# G7 Bridge app for Android

**English** · [Polski](README.pl.md)

The Android app does what the [setup page](https://g7bridge.b-hubit.com) does: connects to the
bridge, shows the reading, the sensor and its session, the range, the bridge's
battery and firmware version, pairs a new sensor by its code and loads the app certificates.
On top of that it watches the bridge in the background.

> Hobby project, not a medical device. Glucose alarms stay in xDrip+.

## Install

1. On the Android phone, open [Releases](https://github.com/TomaszSmart/Dexcom-G7-CGM-Extender-Bridge/releases)
   and download `g7-bridge-app-<version>.apk` from the newest **app-v…** release.
2. Open the file and allow installing apps from this source when Android asks.
3. Start **G7 Bridge** and allow Bluetooth (“Nearby devices”) and notifications.

Updates install the same way, over the old version; settings are kept.

## First use

If xDrip+ on this phone is already connected to the bridge, the app finds the bridge by itself.
Otherwise tap **Connect**, choose “G7 Bridge” and accept the pairing request within 10 minutes
of powering the bridge up.

- **App certificates** — **From the xDrip+ documentation** (or **Scan with camera**), then
  **Send to bridge**. Needed once per bridge.
- **New sensor** — enter the applicator code and tap **Pair**.

## Notifications

About every 15 minutes (also in battery saver) the app reads the bridge once — for under a second,
the link is open anyway because of xDrip+ — and tells you only what xDrip+ cannot see:

| Notification | When |
|---|---|
| Bridge battery 20 % / 10 % | the 10 % one is louder; you may let it through Do Not Disturb in Android's settings for this app |
| The phone cannot reach the bridge | ~30 min without contact, with the last time and battery level |
| The bridge does not hear the sensor / gets no readings | no reading for over 20 min |
| xDrip+ is not collecting from the bridge | for ~30 min (firmware 0.3.0 or newer) |
| Sensor session ended / sensor failure | once per sensor |
| App certificates are not loaded, new firmware available | once |
