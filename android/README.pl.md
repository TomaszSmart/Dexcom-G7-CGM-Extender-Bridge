# Aplikacja G7 Bridge na Androida

[English](README.md) · **Polski**

Aplikacja na Androida robi to samo co [strona konfiguracji](https://g7bridge.b-hubit.com), bez
Chrome: łączy się z bridge’em, pokazuje odczyt, sensor i jego sesję, zasięg, baterię bridge’a
i wersję firmware, paruje nowy sensor po kodzie i wczytuje certyfikaty aplikacji. Dodatkowo
pilnuje bridge’a w tle.

> Projekt hobbystyczny, nie jest wyrobem medycznym. Alarmy glukozy zostają w xDrip+.

## Instalacja

1. W telefonie otwórz [Releases](https://github.com/TomaszSmart/Dexcom-G7-CGM-Extender-Bridge/releases)
   i pobierz `g7-bridge-app-<wersja>.apk` z najnowszego wydania **app-v…**.
2. Otwórz plik i zezwól na instalowanie aplikacji z tego źródła, gdy Android o to zapyta.
3. Uruchom **G7 Bridge** i zezwól na Bluetooth („Urządzenia w pobliżu”) oraz powiadomienia.

Aktualizacje instaluje się tak samo, na starą wersję; ustawienia zostają.

## Pierwsze użycie

Jeśli xDrip+ w tym telefonie jest już połączony z bridge’em, aplikacja sama go znajdzie.
W przeciwnym razie naciśnij **Połącz**, wybierz „G7 Bridge” i zatwierdź parowanie — w ciągu
10 minut od podłączenia zasilania bridge’a, jak każdy telefon.

- **Certyfikaty aplikacji** — **Z dokumentacji xDrip+** (albo **Skanuj aparatem**), potem
  **Wyślij do bridge’a**. Raz na bridge.
- **Nowy sensor** — wpisz kod z aplikatora i naciśnij **Paruj**.

## Powiadomienia

Mniej więcej co 15 minut (także w trybie oszczędzania energii) aplikacja raz odczytuje bridge —
w niecałą sekundę, połączenie i tak jest otwarte przez xDrip+ — i mówi tylko o tym, czego xDrip+
nie widzi:

| Powiadomienie | Kiedy |
|---|---|
| Bateria bridge’a 20 % / 10 % | to przy 10 % jest głośniejsze; w ustawieniach Androida dla tej aplikacji możesz pozwolić mu przebijać tryb Nie przeszkadzać |
| Telefon nie łączy się z bridge’em | ok. 30 min bez kontaktu, z godziną i ostatnim stanem baterii |
| Bridge nie słyszy sensora / nie dostaje odczytów | brak odczytu ponad 20 min |
| xDrip+ nie odbiera odczytów z bridge’a | przez ok. 30 min (firmware 0.3.0 lub nowszy) |
| Koniec sesji / awaria sensora | raz na sensor |
| Brak certyfikatów aplikacji, nowy firmware | raz |

Poza baterią to ciche powiadomienia. Alarm **Missed readings** w xDrip+ trzymaj włączony
i z przebijaniem trybu Nie przeszkadzać — to on jest siatką bezpieczeństwa.

## Kompilacja

Otwórz ten folder (`android/`) w Android Studio albo uruchom `./gradlew assembleDebug`. W Windows
ścieżka ze znakiem `&` psuje Gradle — otwórz projekt przez junction, np. `C:\dev\g7android`.

Wydania buduje GitHub Actions (`.github/workflows/android.yml`): wypchnięcie tagu `app-vX.Y.Z`
buduje APK podpisany kluczem z sekretów repozytorium `ANDROID_KEYSTORE_BASE64`,
`ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` i `ANDROID_KEY_PASSWORD` i dołącza go do wydania.
Chroń ten klucz: bez niego zainstalowanych aplikacji nie da się zaktualizować.
