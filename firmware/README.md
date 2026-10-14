# G7 Bridge firmware

**English** · [Polski](README.pl.md)

Firmware for the G7 Bridge: an nRF52840 board that pairs with a Dexcom G7 sensor, reads it every
5 minutes and serves the readings to xDrip+ as a standard Bluetooth CGM service (0x181F).
Built with the nRF Connect SDK (Zephyr).

> Hobby project, not a medical device. Make treatment decisions based on approved devices.

## Hardware

RAKwireless **RAK4631** WisBlock core on a **RAK19007** or **RAK19003** base board, with the BLE
antenna on the module's BLE u.FL connector (the LoRa antenna is not needed). A 3.7 V Li-Po
battery with a JST-PH 2.0 mm plug — check the polarity against the “+” on the board, there is no
reverse-polarity protection. Build target: `rak4631/nrf52840`.

## Install

1. Download `g7-bridge-rak4631-<version>.uf2` from **Releases**.
2. Press RESET twice quickly: a `RAK4631` drive appears.
3. Copy the `.uf2` file onto that drive. The board flashes itself and restarts. Windows often
   reports a copy error (“device not ready”) because the drive disappears as soon as the last block
   arrives — that is normal; the setup page shows the new version afterwards.

Updates are installed the same way. The pairing code, the sensor, its keys and the app certificates
are kept in a separate part of the flash and survive updates.

## First setup

1. Open the setup page in Chrome on Android: https://g7bridge.b-hubit.com
2. Within 10 minutes of powering the bridge up, connect to “G7 Bridge” and accept the pairing.
3. **App certificates** — a new G7 asks for them on its first pairing. They are not part of this
   firmware: on the page tap **From the xDrip+ documentation** (it reads the QR code from xDrip+'s
   G7 guide) and **Send to bridge**. They stay on your bridge only.
4. **xDrip+** — within the same 10 minutes: xDrip+ → Settings → **Hardware Data Source** →
   **SmartGuide**, choose “G7 Bridge” and accept the pairing request. The bridge drops the phone
   once right after pairing; xDrip+ reconnects by itself. AndroidAPS keeps xDrip+ as its BG source.
5. **New sensor** — enter the 4-digit code from the applicator on the page. Stop data collection on
   any other phone that talks to that sensor first. The bridge finds the sensor the code belongs
   to and pairs with it (up to 5 minutes).

Missed the 10-minute window? Unplug the battery and USB (or press RESET once) and pair again.

## Build from source

Requires nRF Connect SDK **v2.9.3**. Always build without sysbuild:

```
west build --board rak4631/nrf52840 --pristine --no-sysbuild <path-to>/firmware
```

The image is `build/zephyr/zephyr.uf2`. On Windows, a source path with spaces breaks the build —
use a junction (e.g. `C:\ncs\g7bridge`) pointing at this folder.

Optional, private builds only: `tools/make_g7_certs.py` turns the xDrip+ QR text or settings export
into `src/g7_certs.h`, which is then compiled in as a fallback. That file contains a signing key —
never commit or share it, and never publish an image built with it (`.gitignore` excludes it).

## Serial console

The USB port shows logs and a shell (115200 baud). Useful commands: `g7 get_code`, `g7 get_id`,
`g7 certs`, `g7 cert_selftest`, `g7 battery`, `g7 unpair_phone`, `g7 repair_sensor`.
