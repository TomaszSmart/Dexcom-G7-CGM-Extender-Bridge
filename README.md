# G7 Bridge

Strona do obsługi domowego mostka Bluetooth (nRF52840), który odbiera odczyty z sensora
Dexcom G7 i przekazuje je do xDrip+ jako standardowa usługa Bluetooth CGM.

**Strona:** https://tomaszsmart.github.io/G7-Bridge/

Otwórz ją w Chrome na Androidzie (wymaga Web Bluetooth), „Połącz z bridge'em” i wybierz
„G7 Bridge”. Strona pokazuje ostatni odczyt, stan i dzień sesji sensora oraz sensory w zasięgu,
a przy wymianie sensora wystarczy wpisać 4-cyfrowy kod z aplikatora.

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

## Prywatność

Strona działa w całości w przeglądarce i nie wysyła nigdzie żadnych danych.

> Projekt hobbystyczny, nie jest wyrobem medycznym. Decyzje terapeutyczne podejmuj na podstawie
> zatwierdzonych urządzeń.
