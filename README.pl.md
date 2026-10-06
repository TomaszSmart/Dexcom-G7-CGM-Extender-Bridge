# G7 Bridge

[English](README.md) · **Polski**

Strona do obsługi domowego mostka Bluetooth (nRF52840, np. RAK4631), który odbiera odczyty
z sensora Dexcom G7 i przekazuje je do xDrip+ jako standardowa usługa Bluetooth CGM.

**Strona:** https://g7bridge.b-hubit.com

Otwórz ją w Chrome na Androidzie (wymaga Web Bluetooth), naciśnij „Połącz z bridge'em” i wybierz
„G7 Bridge”. Strona pokazuje ostatni odczyt, stan i dzień sesji sensora, baterię bridge'a, zasięg
radiowy oraz sensory w zasięgu, a przy wymianie sensora wystarczy wpisać 4-cyfrowy kod
z aplikatora. Język strony przełączasz w prawym górnym rogu (EN / PL); domyślny jest angielski.

Telefon można sparować z mostkiem tylko przez 10 minut od podłączenia jego zasilania.

## Łączenie bez klikania po odświeżeniu strony

Chrome domyślnie wymaga kliknięcia „Połącz z bridge'em” po każdym odświeżeniu strony
(zabezpieczenie Web Bluetooth). Żeby strona łączyła się sama z zapamiętanym bridge'em:

1. Wpisz w pasku adresu: `chrome://flags/#enable-web-bluetooth-new-permissions-backend`
2. Ustaw „Use the new permissions backend for Web Bluetooth” na **Enabled**.
3. Naciśnij **Relaunch** (Chrome się zrestartuje).
4. Otwórz stronę i raz połącz się z bridge'em przyciskiem.

Od tej pory po odświeżeniu strona łączy się sama w ciągu kilku sekund. Po wyczyszczeniu danych
strony lub usunięciu jej uprawnień trzeba raz połączyć się przyciskiem od nowa. To flaga
eksperymentalna Chrome, więc w przyszłych wersjach może się zmienić.

## Diody bridge'a (RAK4631)

| Dioda | Co znaczy |
|---|---|
| zielona 1× co 5 s | sensor OK, świeży odczyt, xDrip połączony |
| zielona 2× co 5 s | sensor OK, ale xDrip niepołączony, albo rozgrzewanie |
| zielona + niebieska 3× co 5 s | problem: brak odczytu ponad 15 min, awaria lub koniec sesji |
| zielona + niebieska 1× co 2 s | po włączeniu, czeka na pierwszy odczyt |
| niebieska miga | szuka sensora po nowym kodzie |
| niebieska 1× co 5 s | nie ustawiono kodu ani sensora |
| czerwona świeci | dioda ładowania baterii (nie zależy od bridge'a) |

## Prywatność

Strona działa w całości w przeglądarce i nie wysyła nigdzie żadnych danych.

> Projekt hobbystyczny, nie jest wyrobem medycznym. Decyzje terapeutyczne podejmuj na podstawie
> zatwierdzonych urządzeń.
