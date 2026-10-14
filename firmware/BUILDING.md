# Building the G7 Bridge firmware

**English** · [Polski](BUILDING.pl.md)

Only needed to change the firmware. To install it, use the ready `.uf2` from **Releases** (see
[README](README.md)).

Requires nRF Connect SDK **v2.9.3**. Always build without sysbuild:

```
west build --board rak4631/nrf52840 --pristine --no-sysbuild <path-to>/firmware
```

The image is `build/zephyr/zephyr.uf2`. On Windows, a source path with spaces breaks the build —
use a junction (e.g. `C:\ncs\g7bridge`) pointing at this folder.

Optional, private builds only: `tools/make_g7_certs.py` turns the xDrip+ QR text or settings export
into `src/g7_certs.h`, which is then compiled in as a fallback. That file contains a signing key —
never commit or share it, and never publish an image built with it (`.gitignore` excludes it).

Pushing a tag `v*` to GitHub builds the same image in CI (`.github/workflows/firmware.yml`) and
attaches it to a Release. The build fails if `g7_certs.h` was committed.
