# Firmware G7 Bridge

[English](README.md) · **Polski**

Firmware dla G7 Bridge: płytki nRF52840, która paruje się z sensorem Dexcom G7, co 5 minut
odczytuje go i przekazuje odczyty do xDrip+ jako standardowa usługa Bluetooth CGM (0x181F).
Zbudowane na nRF Connect SDK (Zephyr).

> Projekt hobbystyczny, nie jest wyrobem medycznym. Decyzje terapeutyczne podejmuj na podstawie
> zatwierdzonych urządzeń.

## Płytki

| Płytka | Cel kompilacji | Uwagi |
|---|---|---|
| RAK4631 na RAK19007 / RAK19003 (zalecana) | `rak4631/nrf52840` | zewnętrzna antena BLE, poziom baterii, zasięg do telefonu ok. 50 m i więcej |
| Seeed XIAO nRF52840 (Sense) | `xiao_ble` | bardzo mały zasięg (antena na płytce) |
| Nordic nRF52840 Dongle | `nrf52840dongle/nrf52840` | do testów |

## Instalacja

1. Pobierz plik `.uf2` dla swojej płytki z zakładki **Releases**.
2. Wprowadź płytkę w tryb bootloadera: RAK4631 — naciśnij szybko dwa razy RESET (pojawi się dysk
   `RAK4631`); XIAO — naciśnij raz RESET (dysk `XIAO-SENSE`).
3. Skopiuj plik `.uf2` na ten dysk. Płytka sama się zaprogramuje i uruchomi ponownie.

Aktualizacje instaluje się tak samo. Kod parowania, sensor, jego klucze i certyfikaty aplikacji są
w osobnej części pamięci i przetrwają aktualizację.

## Pierwsza konfiguracja

1. Otwórz stronę konfiguracji w Chrome na Androidzie: https://g7bridge.b-hubit.com
2. W ciągu 10 minut od podłączenia zasilania bridge'a połącz się z „G7 Bridge” i zatwierdź parowanie.
3. **Certyfikaty aplikacji** — nowy G7 prosi o nie przy pierwszym parowaniu. Nie ma ich w tym
   firmware: wczytaj je ze strony (zeskanuj aparatem kod QR „Auto configure” z przewodnika G7
   w xDrip+ albo wybierz plik eksportu ustawień xDrip+). Zostają tylko w Twoim bridge'u.
4. **Nowy sensor** — wpisz 4-cyfrowy kod z aplikatora. Bridge sam znajdzie sensor, do którego
   pasuje kod.
5. W xDrip+ wybierz G7 Bridge jako standardowy CGM Bluetooth.

## Kompilacja ze źródeł

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

## Konsola szeregowa

Port USB pokazuje logi i konsolę (115200 bodów). Przydatne polecenia: `g7 get_code`, `g7 get_id`,
`g7 certs`, `g7 cert_selftest`, `g7 battery`, `g7 unpair_phone`, `g7 repair_sensor`.
