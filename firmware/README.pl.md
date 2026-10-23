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
3. Skopiuj plik `.uf2` na ten dysk. Płytka sama się zaprogramuje i uruchomi ponownie. Windows
   często zgłasza błąd kopiowania („urządzenie nie jest gotowe”), bo dysk znika zaraz po odebraniu
   ostatniej części pliku — to normalne; strona pokaże potem nową wersję.

Aktualizacje instaluje się tak samo. Kod parowania, sensor i konfiguracja G7 są
w osobnej części pamięci i przetrwają aktualizację.

## Pierwsza konfiguracja

1. Otwórz stronę konfiguracji w Chrome na Androidzie: https://g7bridge.b-hubit.com
2. W ciągu 10 minut od włączenia bridge’a albo podłączenia ładowarki USB-C połącz się z „G7 Bridge” i zatwierdź parowanie.
3. **Konfiguracja G7** — jednorazowo, przed pierwszym G7: na stronie naciśnij **Z dokumentacji xDrip+**
   i **Wyślij do bridge’a**. Zostaje tylko w Twoim bridge’u.
4. **xDrip+** — w tych samych 10 minutach: xDrip+ → Ustawienia → **Hardware Data Source** →
   **SmartGuide**, wybierz „G7 Bridge” i zatwierdź parowanie. Zaraz po parowaniu bridge raz rozłączy
   telefon; xDrip+ połączy się sam. AndroidAPS dalej korzysta z xDrip+ jako źródła BG.
5. **Nowy sensor** — wpisz na stronie 4-cyfrowy kod z aplikatora. Wcześniej wyłącz zbieranie danych
   w innym telefonie, który łączy się z tym sensorem. Bridge sam znajdzie sensor, do którego pasuje
   kod, i sparuje się z nim (do 30 minut).

Minęło 10 minut? Podłącz do bridge’a ładowarkę USB-C (albo naciśnij raz RESET) — parowanie
otworzy się na kolejne 10 minut.
