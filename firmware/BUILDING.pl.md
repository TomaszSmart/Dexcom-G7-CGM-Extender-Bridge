# Kompilacja firmware G7 Bridge

[English](BUILDING.md) · **Polski**

Potrzebna tylko do zmian w firmware. Do instalacji użyj gotowego pliku `.uf2` z zakładki
**Releases** (zob. [README](README.pl.md)).

Wymaga nRF Connect SDK **v2.9.3**. Zawsze kompiluj bez sysbuild:

```
west build --board rak4631/nrf52840 --pristine --no-sysbuild <ścieżka>/firmware
```

Obraz to `build/zephyr/zephyr.uf2`. W Windows ścieżka ze spacjami psuje kompilację — użyj
junction (np. `C:\ncs\g7bridge`) wskazującego na ten folder.

Opcjonalnie, tylko do prywatnych kompilacji: `tools/make_g7_certs.py` zamienia tekst QR xDrip+ albo
eksport ustawień w `src/g7_certs.h`, który jest wtedy wkompilowany jako zapas. Ten plik zawiera klucz
podpisujący — nigdy go nie commituj ani nie udostępniaj i nie publikuj obrazu z nim zbudowanego
(`.gitignore` go pomija).

Wypchnięcie tagu `v*` na GitHub buduje ten sam obraz w CI (`.github/workflows/firmware.yml`)
i dołącza go do wydania (Release). Budowanie przerywa się, jeśli ktoś dodał `g7_certs.h`.
