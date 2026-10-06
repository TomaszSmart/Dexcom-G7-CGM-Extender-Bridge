# G7 Bridge

**English** · [Polski](README.pl.md)

Web page for a home-made Bluetooth bridge (nRF52840, e.g. RAK4631) that receives readings from a
Dexcom G7 sensor and forwards them to xDrip+ as a standard Bluetooth CGM service.

**Page:** https://g7bridge.b-hubit.com

Open it in Chrome on Android (Web Bluetooth is required), tap “Connect to bridge” and choose
“G7 Bridge”. The page shows the latest reading, the sensor's state and session day, the bridge
battery, the radio range and the sensors in range. When you change the sensor, just enter the
4-digit code from the applicator. The page language can be switched between English and Polish
(top right); English is the default.

A phone can only be paired with the bridge within 10 minutes of powering the bridge up.

## Connect automatically after reloading the page

By default Chrome needs a click on “Connect to bridge” after every reload (a Web Bluetooth
safeguard). To let the page reconnect to the remembered bridge by itself:

1. Type in the address bar: `chrome://flags/#enable-web-bluetooth-new-permissions-backend`
2. Set “Use the new permissions backend for Web Bluetooth” to **Enabled**.
3. Tap **Relaunch** (Chrome restarts).
4. Open the page and connect to the bridge once with the button.

From then on the page reconnects by itself within a few seconds after a reload. After clearing
the site data or its permissions, connect once with the button again. This is an experimental
Chrome flag, so it may change in future versions.

## Bridge LEDs (RAK4631)

| LED | Meaning |
|---|---|
| green 1× every 5 s | sensor OK, fresh reading, xDrip connected |
| green 2× every 5 s | sensor OK but xDrip not connected, or warm-up |
| green + blue 3× every 5 s | problem: no reading for over 15 min, sensor failure or session ended |
| green + blue 1× every 2 s | after power-on, waiting for the first reading |
| blue blinking | searching for the sensor that matches a new code |
| blue 1× every 5 s | no pairing code or sensor set |
| red steady | battery charging LED (not controlled by the bridge) |

## Privacy

The page runs entirely in the browser and does not send any data anywhere.

> Hobby project, not a medical device. Make treatment decisions based on approved devices.
