# G7 Bridge firmware

**English** · [Polski](README.pl.md)

Firmware for the G7 Bridge: an nRF52840 board that pairs with a Dexcom G7 sensor, reads it every
5 minutes and serves the readings to xDrip+ as a standard Bluetooth CGM service (0x181F).
Built with the nRF Connect SDK (Zephyr).

> Hobby project, not a medical device. Make treatment decisions based on approved devices.

## Boards

| Board | Build target | Notes |
|---|---|---|
| RAK4631 on RAK19007 / RAK19003 (recommended) | `rak4631/nrf52840` | external BLE antenna, battery level, ~50 m+ range to the phone |
| Seeed XIAO nRF52840 (Sense) | `xiao_ble` | very short range (on-board antenna) |
| Nordic nRF52840 Dongle | `nrf52840dongle/nrf52840` | for testing |

## Install

1. Download the `.uf2` file for your board from **Releases**.
2. Put the board into its bootloader: RAK4631 — press RESET twice quickly (a `RAK4631` drive appears);
   XIAO — press RESET once (`XIAO-SENSE` drive).
3. Copy the `.uf2` file onto that drive. The board flashes itself and restarts.

Updates are installed the same way. The pairing code, the sensor, its keys and the app certificates
are kept in a separate part of the flash and survive updates.

## First setup

1. Open the setup page in Chrome on Android: https://g7bridge.b-hubit.com
2. Within 10 minutes of powering the bridge up, connect to “G7 Bridge” and accept the pairing.
3. **App certificates** — a new G7 asks for them on its first pairing. They are not part of this
   firmware: load them from the page (scan xDrip+'s G7 “Auto configure” QR code with the camera,
   or pick an xDrip+ settings export). They stay on your bridge only.
4. **New sensor** — enter the 4-digit code from the applicator. The bridge finds the sensor the
   code belongs to.
5. In xDrip+, choose the G7 Bridge as a standard Bluetooth CGM.

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
