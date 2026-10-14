# Firmware G7 Bridge

[English](README.md) · **Polski**

Firmware dla G7 Bridge: płytki nRF52840, która paruje się z sensorem Dexcom G7, co 5 minut
odczytuje go i przekazuje odczyty do xDrip+ jako standardowa usługa Bluetooth CGM (0x181F).
Zbudowane na nRF Connect SDK (Zephyr).

> Projekt hobbystyczny, nie jest wyrobem medycznym. Decyzje terapeutyczne podejmuj na podstawie
> zatwierdzonych urządzeń.

## Sprzęt

Moduł RAKwireless **RAK4631** WisBlock na płytce bazowej **RAK19007** albo **RAK19003**, z anteną
BLE w gnieździe u.FL opisanym BLE (antena LoRa nie jest potrzebna). Akumulator Li-Po 3,7 V z wtykiem
JST-PH 2,0 mm — sprawdź polaryzację z „+” na płytce, płytka nie ma zabezpieczenia przed odwrotnym
podłączeniem. Cel kompilacji: `rak4631/nrf52840`.

## Instalacja

1. Pobierz `g7-bridge-rak4631-<wersja>.uf2` z zakładki **Releases**.
2. Naciśnij szybko dwa razy RESET: pojawi się dysk `RAK4631`.
3. Skopiuj plik `.uf2` na ten dysk. Płytka sama się zaprogramuje i uruchomi ponownie.

Aktualizacje instaluje się tak samo. Kod parowania, sensor, jego klucze i certyfikaty aplikacji są
w osobnej części pamięci i przetrwają aktualizację.

## Pierwsza konfiguracja

1. Otwórz stronę konfiguracji w Chrome na Androidzie: https://g7bridge.b-hubit.com
2. W ciągu 10 minut od podłączenia zasilania bridge'a połącz się z „G7 Bridge” i zatwierdź parowanie.
3. **Certyfikaty aplikacji** — nowy G7 prosi o nie przy pierwszym parowaniu. Nie ma ich w tym
   firmware: na stronie naciśnij **Z dokumentacji xDrip+** (strona odczyta kod QR z przewodnika G7
   xDrip+) i **Wyślij do bridge'a**. Zostają tylko w Twoim bridge'u.
4. **xDrip+** — w tych samych 10 minutach: xDrip+ → Ustawienia → **Hardware Data Source** →
   **SmartGuide**, wybierz „G7 Bridge” i zatwierdź parowanie. Zaraz po parowaniu bridge raz rozłączy
   telefon; xDrip+ połączy się sam. AndroidAPS dalej korzysta z xDrip+ jako źródła BG.
5. **Nowy sensor** — wpisz na stronie 4-cyfrowy kod z aplikatora. Wcześniej wyłącz zbieranie danych
   w innym telefonie, który łączy się z tym sensorem. Bridge sam znajdzie sensor, do którego pasuje
   kod, i sparuje się z nim (do 5 minut).

Minęło 10 minut? Odłącz baterię i USB (albo naciśnij raz RESET) i sparuj ponownie.

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
