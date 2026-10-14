# G7 Bridge

[English](README.md) · **Polski**

## Dlaczego G7 Bridge

Sensor Dexcom G7 rozmawia z telefonem tylko z odległości kilku metrów. Dlatego na treningu piłki,
na WF-ie czy na boisku telefon musi być przy dziecku — w pasku biegowym, w saszetce, w nerce albo
w kieszeni, która podskakuje przy każdym kroku. To niewygodne, przeszkadza w grze, a telefony się
psują.

G7 Bridge to mały, lekki moduł, który mieści się w kieszeni. Jest blisko sensora, co 5 minut
odczytuje glukozę i przekazuje ją do telefonu, który może zostać w szatni albo na ławce — nawet
do około 50 m. xDrip+ i AndroidAPS działają jak zwykle, a gdy telefon
na chwilę straci zasięg, bridge przechowa odczyty i wyśle je, gdy tylko połączenie wróci.

**Cel jest prosty: żeby dziecko mogło po prostu grać, a rodzic nadal widział każdy odczyt.**

## Szybki start

Potrzebujesz: **RAK4631** firmy RAKwireless na płytce **RAK19007/RAK19003** z anteną BLE i małym
akumulatorem Li-Po, telefonu z Androidem z **Chrome** i **xDrip+** oraz 4-cyfrowego kodu z aplikatora
sensora.

1. **Firmware** — pobierz `g7-bridge-rak4631-<wersja>.uf2` z
   [Releases](https://github.com/TomaszSmart/Dexcom-G7-CGM-Extender-Bridge/releases/latest), naciśnij
   dwa razy RESET na płytce i skopiuj plik na dysk `RAK4631` (błąd kopiowania zgłoszony przez
   Windows na końcu jest normalny — płytka restartuje się, gdy tylko dostanie plik).
2. **Włącz zasilanie bridge'a.** Kroki 3–5 trzeba zrobić w ciągu **10 minut** (tylko wtedy telefon
   może się sparować; potem naciśnij raz RESET i kontynuuj).
3. **Strona** — otwórz https://g7bridge.b-hubit.com w Chrome, naciśnij **Połącz z bridge'em**,
   wybierz „G7 Bridge” i zatwierdź parowanie.
4. **Certyfikaty aplikacji** — w panelu „Certyfikaty aplikacji” naciśnij **Z dokumentacji xDrip+**,
   a potem **Wyślij do bridge'a** (raz na bridge; przetrwają aktualizacje).
5. **xDrip+** — Ustawienia → **Hardware Data Source** → **SmartGuide**, wybierz „G7 Bridge”
   i zatwierdź parowanie. Po parowaniu bridge raz rozłączy telefon; xDrip+ połączy się sam.
   AndroidAPS dalej korzysta z xDrip+ jako źródła BG.
6. **Sensor** — wyłącz zbieranie danych w innym telefonie, który łączy się z sensorem, wpisz kod
   z aplikatora w panelu **Nowy sensor** i naciśnij **Paruj**. W ciągu ok. 5 minut bridge znajdzie
   sensor, a odczyty pojawią się w xDrip+.

Aktualizacje: strona pokaże, kiedy jest nowszy firmware; wgrywasz go tak samo jak w kroku 1.
Ustawienia, certyfikaty i parowania zostają.

## Strona

Strona do obsługi domowego mostka Bluetooth, który odbiera odczyty
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

## Diody bridge'a

| Dioda | Co znaczy |
|---|---|
| zielona 1× co 5 s | sensor OK, świeży odczyt, xDrip połączony |
| zielona 2× co 5 s | sensor OK, ale xDrip niepołączony, albo rozgrzewanie |
| zielona + niebieska 3× co 5 s | problem: brak odczytu ponad 15 min, awaria lub koniec sesji |
| zielona + niebieska 1× co 2 s | po włączeniu, czeka na pierwszy odczyt |
| niebieska miga | szuka sensora po nowym kodzie |
| niebieska 1× co 5 s | nie ustawiono kodu ani sensora |
| czerwona świeci | dioda ładowania baterii (nie zależy od bridge'a) |

## Firmware

Firmware bridge'a (RAKwireless RAK4631 WisBlock) jest w [firmware/](firmware/README.pl.md):
płytki, instalacja, pierwsza konfiguracja i kompilacja ze źródeł. Certyfikatów aplikacji, o które
prosi nowy G7, w nim nie ma — wczytujesz je do swojego bridge'a ze strony.

## Licencja

GPL-3.0 (zob. [LICENSE](LICENSE)). Uwierzytelnianie G7 jest wzorowane na xDrip+ (libkeks), które ma licencję GPL-3.0.

## Prywatność

Strona działa w całości w przeglądarce i nie wysyła nigdzie żadnych danych.

> Projekt hobbystyczny, nie jest wyrobem medycznym. Decyzje terapeutyczne podejmuj na podstawie
> zatwierdzonych urządzeń.
