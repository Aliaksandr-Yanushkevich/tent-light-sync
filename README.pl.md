# tent-light-sync

[English](README.md) | [Polski](README.pl.md)

Oświetlenie kempingowe, które pomaga dostrzec odciągi namiotu i inne przeszkody na biwaku po zmroku. Celem projektu jest ograniczenie ryzyka potknięć i upadków, ochrona sprzętu przed przypadkowym uszkodzeniem oraz stworzenie przytulnej atmosfery dzięki delikatnemu, zsynchronizowanemu pulsowaniu świateł.

Przykładowe zastosowania to oznaczanie:

- Odciągów i punktów mocowania namiotu.
- Hamaków i linek ich zawieszenia.
- Odciągów stabilizujących komin piecyka namiotowego.

## Zasada działania

Płytki ESP32 synchronizują płynne rozjaśnianie i przygaszanie świateł za pomocą ESP-NOW. Każda płytka steruje światłem przez tranzystor MOSFET, a pobliskie urządzenia współdzielą ten sam cykl jasności bez routera ani połączenia z internetem.

Oprogramowanie znajduje się w pliku [`sketch.ino`](sketch.ino). Wgraj ten sam szkic na każdą płytkę — role są wybierane automatycznie.

## Sprzęt i połączenia

- Płytka ESP32 z Wi-Fi i dostępnym wyjściem GPIO4.
- Źródło światła obsługujące sterowanie PWM oraz odpowiednio dobrany zasilacz.
- Tranzystor MOSFET z kanałem N, sterowany poziomami logicznymi, dobrany do napięcia i prądu oświetlenia.
- Rezystor 100–220 Ω między GPIO4 a bramką tranzystora MOSFET.

Typowy układ z tranzystorem MOSFET po stronie masy:

| Połączenie | Punkt docelowy |
| --- | --- |
| ESP32 GPIO4 | Bramka MOSFET-a przez rezystor 100–220 Ω |
| Źródło MOSFET-a | Wspólna masa |
| Dren MOSFET-a | Ujemny zacisk oświetlenia |
| Dodatni zacisk oświetlenia | Dodatni zacisk zasilacza oświetlenia |
| ESP32 GND | Masa zasilacza oświetlenia |

Prąd oświetlenia powinien płynąć przez tranzystor MOSFET; nie zasilaj oświetlenia bezpośrednio z GPIO4. Zalecany jest rezystor między bramką a masą, utrzymujący tranzystor w stanie wyłączonym podczas uruchamiania płytki. Szkic nie steruje tym rezystorem.

## Wgrywanie i uruchamianie

1. Zainstaluj Arduino IDE oraz pakiet obsługi swojej płytki ESP32. Szkic korzysta z funkcji `ledcAttach` / `ledcWrite` sterowanych numerem pinu oraz funkcji zwrotnej odbioru z typem `esp_now_recv_info_t`, więc wybierz pakiet udostępniający te API.
2. Umieść plik `sketch.ino` w folderze szkicu o nazwie `sketch` i otwórz go w Arduino IDE lub zaakceptuj propozycję IDE utworzenia folderu o odpowiedniej nazwie.
3. Wybierz płytkę ESP32 i port szeregowy, a następnie wgraj szkic. Program korzysta z nagłówków `WiFi.h` i `esp_now.h` dostarczanych z pakietem oraz z `math.h`; nie odwołuje się do dodatkowych bibliotek zewnętrznych.
4. Otwórz monitor portu szeregowego z prędkością **115200 bodów**.
5. Powtórz te kroki dla pozostałych płytek i uruchom je w zasięgu łączności radiowej.

Każda płytka uruchamia się jako urządzenie nadrzędne (master). Po odebraniu pakietów rozgłoszeniowych płytki zaczynają podążać za urządzeniem nadrzędnym o najniższym ogłaszanym adresie MAC. Przy skutecznej wymianie pakietów grupa ostatecznie wybiera płytkę o najniższym adresie MAC.

## Synchronizacja

- Urządzenie nadrzędne rozsyła ustawienia oświetlenia i czas od rozpoczęcia cyklu co **3 sekundy**.
- Urządzenia podrzędne kopiują te ustawienia i przy każdym zaakceptowanym pakiecie korygują lokalny czas rozpoczęcia cyklu.
- Jasność zmienia się sinusoidalnie, płynnie przechodząc od minimum do maksimum i z powrotem, domyślnie w ciągu **10 sekund**.
- Urządzenie podrzędne, które przez **30 sekund** nie otrzyma zaakceptowanego pakietu synchronizacji, staje się nadrzędnym i rozpoczyna cykl od nowa. Kolejne pakiety pozwalają grupie ponownie wybrać urządzenie nadrzędne.
- Pojedyncza płytka samodzielnie realizuje ten sam efekt pulsowania.

Synchronizacja jest okresowa i nie kompensuje opóźnień transmisji radiowej. Zmiany ról resetują lub korygują fazę cyklu, co może powodować widoczny skok jasności.

## Konfiguracja

Zmień poniższe stałe na początku pliku `sketch.ino`, a następnie wgraj zaktualizowane oprogramowanie:

| Stała | Wartość domyślna | Znaczenie |
| --- | --- | --- |
| `PIN_PWM` | `4` | GPIO połączone z bramką MOSFET-a |
| `PWM_FREQ` | `1000` | Częstotliwość PWM w Hz |
| `PWM_RES` | `10` | Rozdzielczość PWM w bitach; wartości wypełnienia od 0 do 1023 |
| `DEFAULT_MIN_BRIGHTNESS` | `80` | Minimalne wypełnienie PWM, około 8% |
| `DEFAULT_MAX_BRIGHTNESS` | `400` | Maksymalne wypełnienie PWM, około 39% |
| `DEFAULT_PERIOD_MS` | `10000` | Czas pełnego cyklu pulsowania w milisekundach |
| `MASTER_SYNC_INTERVAL_MS` | `3000` | Odstęp między transmisjami urządzenia nadrzędnego |
| `MASTER_TIMEOUT_MS` | `30000` | Czas bez zaakceptowanej synchronizacji przed przejęciem roli nadrzędnej |

Wartości jasności muszą mieścić się w zakresie PWM, a minimum nie może przekraczać maksimum. Okres jest przechowywany w pakiecie jako `uint16_t`, więc ustaw dodatnią wartość nie większą niż 65535 ms. Odebrany okres równy zero jest zastępowany wartością domyślną.

Ustawienia wybranego urządzenia nadrzędnego są przekazywane urządzeniom podrzędnym. Wgraj tę samą konfigurację na wszystkie płytki, jeśli ustawienia mają pozostać takie same po zmianie urządzenia nadrzędnego.

## Format pakietu i ograniczenia

`SyncPacket` jest przesyłany bezpośrednio jako struktura C++ zawierająca:

| Pole | Typ | Przeznaczenie |
| --- | --- | --- |
| `version` | `uint8_t` | Wersja protokołu, obecnie `1` |
| `mode` | `uint8_t` | Identyfikator efektu, obecnie `MODE_BREATH` (`1`) |
| `master_mac` | `uint8_t[6]` | Ogłaszany adres MAC urządzenia nadrzędnego, używany przy wyborze roli |
| `period_ms` | `uint16_t` | Czas trwania cyklu pulsowania |
| `min_brightness` | `uint16_t` | Minimalne wypełnienie PWM |
| `max_brightness` | `uint16_t` | Maksymalne wypełnienie PWM |
| `elapsed_ms` | `uint32_t` | Czas od rozpoczęcia cyklu urządzenia nadrzędnego |
| `sequence` | `uint32_t` | Rosnący licznik transmisji, wyświetlany w celach diagnostycznych |

Odbiorniki sprawdzają rozmiar pakietu, wersję i tryb. Licznik sekwencji nie służy do odrzucania nieaktualnych pakietów. Ponieważ pakiety wykorzystują układ struktury w pamięci, wszystkie płytki powinny korzystać ze zgodnych kompilacji i tej samej definicji pakietu.

Szkic korzysta z nieszyfrowanych pakietów rozgłoszeniowych ESP-NOW i nie uwierzytelnia ogłaszanego urządzenia nadrzędnego. Kanał odbiorcy rozgłoszeniowego jest ustawiony na `0`, co oznacza bieżący kanał Wi-Fi; program nie wybiera kanału jawnie. Płytki muszą pracować na tym samym kanale, aby się komunikować. Zaimplementowano wyłącznie efekt pulsowania.

## Diagnostyka przez port szeregowy

| Komunikat | Znaczenie |
| --- | --- |
| `OWN MAC: ...` | Adres MAC interfejsu Wi-Fi tej płytki |
| `Broadcast peer added` | Pomyślnie zarejestrowano odbiorcę rozgłoszeniowego |
| `ROLE: MASTER, MAC=...` | Ta płytka generuje pakiety synchronizacji |
| `ROLE: SLAVE, MASTER=... seq=...` | Zaakceptowany pakiet zaktualizował ustawienia i czas cyklu urządzenia podrzędnego |
| `SYNC TX seq=...` | ESP-NOW przyjęło żądanie wysłania pakietu; nie jest to potwierdzenie odbioru |
| `SYNC FAIL code=...` | Żądanie wysłania pakietu zwróciło błąd |
| `MASTER TIMEOUT` | Urządzenie podrzędne przejmuje rolę nadrzędną po utracie synchronizacji |

Jeśli światła się nie synchronizują, sprawdź zasięg, zgodność kanałów Wi-Fi i oprogramowania oraz komunikaty inicjalizacji. Jeśli światło nie reaguje, sprawdź wybrane GPIO, wspólną masę, połączenia tranzystora MOSFET i zasilacz oświetlenia. Szkic wypisuje błędy inicjalizacji ESP-NOW, ale nadal realizuje lokalny efekt świetlny.

## Dodatkowe materiały

[Zdjęcia i filmy — album udostępniony w iCloud](https://www.icloud.com/sharedalbum/#D1gv3nVbA1MKH-d3rlWUlG7xFMwCAEQARog94Jf2bXLYHOpxdDWDsPbc_b9ZjobRVccVwYr2VrZj9g)
