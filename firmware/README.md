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

Updates are installed the same way. The pairing code, the sensor and the G7 configuration are kept in a separate part of the flash and survive updates.

## First setup

1. Open the setup page in Chrome on Android: https://g7bridge.b-hubit.com
2. Within 10 minutes of powering the bridge up, connect to “G7 Bridge” and accept the pairing.
3. **G7 configuration** — once, before the first G7: on the page tap **From the xDrip+ documentation**
   and **Send to bridge**. It stays on your bridge only.
4. **xDrip+** — within the same 10 minutes: xDrip+ → Settings → **Hardware Data Source** →
   **SmartGuide**, choose “G7 Bridge” and accept the pairing request. The bridge drops the phone
   once right after pairing; xDrip+ reconnects by itself. AndroidAPS keeps xDrip+ as its BG source.
5. **New sensor** — enter the 4-digit code from the applicator on the page. Stop data collection on
   any other phone that talks to that sensor first. The bridge finds the sensor the code belongs
   to and pairs with it (up to 30 minutes).

Missed the 10-minute window? Unplug the battery and USB (or press RESET once) and pair again.
