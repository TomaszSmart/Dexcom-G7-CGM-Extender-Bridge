# Aplikacja G7 Bridge na Androida

[English](README.md) · **Polski**

Aplikacja na Androida robi to samo co [strona konfiguracji](https://g7bridge.b-hubit.com): łączy się z bridge’em, pokazuje odczyt, sensor i jego sesję, zasięg, baterię bridge’a
i wersję firmware, paruje nowy sensor po kodzie i wczytuje konfigurację G7. Dodatkowo pilnuje bridge’a w tle.

> Projekt hobbystyczny, nie jest wyrobem medycznym. Alarmy glukozy zostają w xDrip+.

## Instalacja

1. W telefonie z Androidem otwórz [Releases](https://github.com/TomaszSmart/Dexcom-G7-CGM-Extender-Bridge/releases)
   i pobierz `g7-bridge-app-<wersja>.apk` z najnowszego wydania **app-v…**.
2. Otwórz plik i zezwól na instalowanie aplikacji z tego źródła, gdy Android o to zapyta.
3. Uruchom **G7 Bridge** i zezwól na Bluetooth („Urządzenia w pobliżu”) oraz powiadomienia.

Aktualizacje instaluje się tak samo, na starą wersję; ustawienia zostają.

## Pierwsze użycie

Jeśli xDrip+ w tym telefonie jest już połączony z bridge’em, aplikacja sama go znajdzie.
W przeciwnym razie naciśnij **Połącz**, wybierz „G7 Bridge” i zatwierdź parowanie w ciągu
10 minut od podłączenia zasilania bridge’a.

- **Konfiguracja G7** — **Z dokumentacji xDrip+** (albo **Skanuj aparatem**), potem
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
| Brak konfiguracji G7, nowa wersja | raz |

Poza baterią to ciche powiadomienia. Główną aplikacją wciąż jest xDrip+: upewnij się, że jego
ustawienia i alarmy są skonfigurowane zgodnie z [dokumentacją xDrip+](https://navid200.github.io/xDrip/), w tym alarm
**Missed readings** (brak odczytów), któremu pozwolisz przebijać tryb Nie przeszkadzać.
