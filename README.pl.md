# G7 Bridge

[English](README.md) · **Polski**

## Dlaczego G7 Bridge

Sensor Dexcom G7 rozmawia z telefonem tylko z odległości kilku metrów. Dlatego na treningu,
na WF-ie czy na boisku telefon musi być przy dziecku — w pasku biegowym, w saszetce, w nerce albo
w kieszeni, która podskakuje przy każdym kroku. To niewygodne, przeszkadza w grze, a telefony się
psują.

G7 Bridge to mały, lekki moduł, który mieści się w kieszeni. Jest blisko sensora, co 5 minut
odczytuje glukozę i przekazuje ją do telefonu, który może zostać w szatni albo na ławce — nawet
do około 50 m. xDrip+ i AndroidAPS działają jak zwykle, a gdy telefon
na chwilę straci zasięg, bridge przechowa odczyty i wyśle je, gdy tylko połączenie wróci.

**Cel jest prosty: żeby dziecko mogło po prostu trenować, a rodzic nadal widział każdy odczyt.**

## Jak wygląda

<p>
  <img src="docs/images/g7-bridge-case.jpg" width="32%" alt="G7 Bridge w obudowie">
  <img src="docs/images/g7-bridge-usb.jpg" width="32%" alt="Złącze USB-C, przycisk i diody">
  <img src="docs/images/g7-bridge-back.jpg" width="32%" alt="G7 Bridge — widok z tyłu">
</p>

Gotowy G7 Bridge: RAK4631 na płytce RAK19003 z akumulatorem, w obudowie wydrukowanej w 3D (https://www.printables.com/model/286664-rak19003-micro-case-for-meshtastic).
Z przodu złącze USB-C do ładowania, przycisk RESET i diody.
Wymiary: **47 × 50 × 24 mm** (szerokość × długość × grubość).

## Szybki start

Potrzebujesz: **RAK4631** firmy RAKwireless na płytce **RAK19007/RAK19003** z anteną BLE i małym
akumulatorem Li-Po, telefonu z Androidem z **Chrome** i **xDrip+** oraz 4-cyfrowego kodu z aplikatora
sensora.

1. **[Firmware](firmware/README.pl.md)** (pełna instrukcja tam) — pobierz `g7-bridge-rak4631-<wersja>.uf2` z
   [Releases](https://github.com/TomaszSmart/Dexcom-G7-CGM-Extender-Bridge/releases/latest), naciśnij
   dwa razy RESET na płytce i skopiuj plik na dysk `RAK4631` (błąd kopiowania zgłoszony przez
   Windows na końcu jest normalny — płytka restartuje się, gdy tylko dostanie plik).
2. **Włącz zasilanie bridge'a.** Kroki 3–5 trzeba zrobić w ciągu **10 minut** (tylko wtedy telefon
   może się sparować; potem naciśnij raz RESET i kontynuuj).
3. **Strona lub aplikacja** — otwórz https://g7bridge.b-hubit.com w Chrome i naciśnij
   **Połącz z bridge'em** albo zainstaluj [aplikację na Androida](android/README.pl.md) i naciśnij
   **Połącz**; wybierz „G7 Bridge” i zatwierdź parowanie.
4. **Konfiguracja G7** — w panelu „Konfiguracja G7” naciśnij **Z dokumentacji xDrip+**,
   a potem **Wyślij do bridge'a** (jednorazowa procedura).
5. **xDrip+** — Ustawienia → **Hardware Data Source** → **SmartGuide**, wybierz „G7 Bridge”
   i zatwierdź parowanie. Po parowaniu bridge raz rozłączy telefon; xDrip+ połączy się sam.
   AndroidAPS dalej korzysta z xDrip+ jako źródła BG.
6. **Sensor** — wyłącz zbieranie danych na innym telefonie, który jest połączony z sensorem lub użyj nowego sensora, wpisz kod
   z aplikatora w panelu **Nowy sensor** i naciśnij **Paruj**. Do 30 minut bridge znajdzie
   sensor, a odczyty pojawią się w xDrip+.

Aktualizacje: strona pokaże, kiedy jest nowszy firmware; wgrywasz go tak samo jak w kroku 1.
Ustawienia, konfiguracja G7 i parowania zostają.

## Strona

Strona do obsługi G7 Bridge, który odbiera odczyty
z sensora Dexcom G7 i przekazuje je do xDrip+ jako standardowa usługa Bluetooth CGM.

**Strona:** https://g7bridge.b-hubit.com

Otwórz ją w Chrome na Androidzie (wymaga Web Bluetooth), naciśnij „Połącz z bridge'em” i wybierz
„G7 Bridge”. Strona pokazuje ostatni odczyt, stan i dzień sesji sensora, baterię bridge'a, zasięg
radiowy oraz sensory w zasięgu, a przy wymianie sensora wystarczy wpisać 4-cyfrowy kod
z aplikatora. Język strony przełączasz w prawym górnym rogu (EN / PL); domyślny jest angielski.

Telefon można sparować G7 Bridge tylko przez 10 minut od podłączenia jego zasilania.

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
| zielona 2× co 5 s | sensor OK, ale xDrip niepołączony, albo trwa rozgrzewanie sensora |
| zielona + niebieska 3× co 5 s | problem: brak odczytu ponad 15 min, awaria lub koniec sesji sensora |
| zielona + niebieska 1× co 2 s | po włączeniu, czeka na pierwszy odczyt |
| niebieska miga | szuka sensora po nowym kodzie |
| niebieska 1× co 5 s | nie ustawiono kodu ani sensora |
| czerwona świeci | dioda ładowania baterii |

## Aplikacja na Androida

[Aplikacja G7 Bridge](android/README.pl.md) robi to samo co strona, bez Chrome, i pilnuje bridge'a
w tle: ostrzega o słabnącej baterii bridge'a jak również raportuje błędy, np. dlaczego nie ma danych (bridge poza
zasięgiem, sensor niesłyszalny, xDrip+ nie odbiera, koniec sesji sensora). Alarmy glukozy zostają
w xDrip+. Plik APK pobierzesz z najnowszego wydania **app-v…**.

## Firmware Bridge'a

Firmware bridge'a (RAKwireless RAK4631 WisBlock) jest w [firmware/](firmware/README.pl.md):
płytki, instalacja, pierwsza konfiguracja i kompilacja ze źródeł. Konfiguracji G7 (kodu QR z dokumentacji xDrip+) w nim nie ma — wczytujesz ją do
swojego bridge'a ze strony lub aplikacji.

## Repozytorium

| Folder | Co | Wydania |
|---|---|---|
| [firmware/](firmware/README.pl.md) | firmware bridge'a (RAK4631) | tagi `v…` → `.uf2` |
| [web/](web/index.html) | strona konfiguracji, publikowana na https://g7bridge.b-hubit.com | — |
| [android/](android/README.pl.md) | aplikacja na Androida | tagi `app-v…` → `.apk` |

## Licencja

GPL-3.0 (zob. [LICENSE](LICENSE)). Uwierzytelnianie G7 jest wzorowane na xDrip+ (libkeks), które ma licencję GPL-3.0.

## Prywatność

Strona działa w całości w przeglądarce, a aplikacja w całości w telefonie; żadna nie wysyła
nigdzie danych. Pytają tylko GitHuba o najnowszą wersję firmware i pobierają kod QR konfiguracji G7
z dokumentacji xDrip+.

> Projekt hobbystyczny, nie jest wyrobem medycznym. Decyzje terapeutyczne podejmuj na podstawie
> zatwierdzonych urządzeń.
