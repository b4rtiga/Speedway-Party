# Speedway Party

Zręcznościowa gra żużlowa na **Amigę 1200 (AGA, 68EC020, 2 MB Chip RAM)**. Turniej 15 biegów dla 1–4 graczy przy jednej kanapie, wolne miejsca zajmują boty. Kod w C (Bebbo GCC, NDK 3.9) pisze bezpośrednio do rejestrów, z wyłączonym systemem.

![Bieg na mokrym torze](art/screen_05_racing.png)

| | | |
|---|---|---|
| ![Tytuł](art/screen_01_title.png) | ![Start spod taśmy](art/screen_04_gate_red.png) | ![Taśma!](art/screen_06_false_start.png) |
| ![Program zawodów](art/screen_03_table.png) | ![Przerwa na tor](art/screen_08_grading.png) | ![Podium](art/screen_10_podium.png) |

*Zrzuty z prawdziwego kodu gry skompilowanego na PC (`tools/shots`).*

## Spis treści
1. [Budowanie](#budowanie)
2. [Uruchomienie w WinUAE](#uruchomienie-w-winuae)
3. [Sterowanie](#sterowanie)
4. [Narzędzia na PC](#narzędzia-na-pc)
5. [Struktura projektu](#struktura-projektu)
6. [Znane ograniczenia / do zrobienia](#znane-ograniczenia--do-zrobienia)

---

## Budowanie

### 1. Toolchain Bebbo (amiga-gcc)
Najprościej na Windows przez **WSL2 z Ubuntu**. Sam toolchain buduje się raz, ok. 30–60 min:

```bash
sudo apt update
sudo apt install make wget git gcc g++ lhasa libgmp-dev libmpfr-dev libmpc-dev flex bison gettext texinfo ncurses-dev autoconf rsync libreadline-dev
git clone https://github.com/bebbo/amiga-gcc
cd amiga-gcc
make update
make all -j4 PREFIX=$HOME/amiga-gcc
echo 'export PATH=$HOME/amiga-gcc/bin:$PATH' >> ~/.bashrc
```

Sprawdzenie: `m68k-amigaos-gcc --version`. NDK 3.9 i libnix są w toolchainie, nic więcej nie trzeba.

Aktualną listę zależności znajdziesz w README repozytorium [bebbo/amiga-gcc](https://github.com/bebbo/amiga-gcc).

### 2. Gra
W WSL, w katalogu projektu (np. `/mnt/e/Workshop/Speedway Party`):

```bash
make dist
```

Powstaje katalog `dist/`:
```
dist/SpeedwayParty          plik wykonywalny
dist/data/*                 grafika i dźwięk (ładowane z PROGDIR:data/)
dist/s/startup-sequence     uruchamia grę od razu po starcie Amigi
```

Gotowe dane (`data/`) są w repozytorium. Do budowania nie potrzebujesz Pythona.

---

## Uruchomienie w WinUAE

1. **Quickstart → Model: A1200**, konfiguracja z Kickstartem 3.0 lub 3.1. ROM musisz mieć własny, np. z Amiga Forever.
2. **CPU and FPU:** 68020, *cycle-exact* (najwierniejszy czas, ważny dla stałych 50 FPS). Bez FPU.
3. **RAM:** Chip 2 MB, Slow 0, Fast 0. Testujemy bez Fast RAM, tak jak na gołym A1200.
4. **Chipset:** AGA, PAL.
Gotowa konfiguracja jest w [tools/winuae/SpeedwayParty-A1200.uae](tools/winuae/SpeedwayParty-A1200.uae): wpisz w niej ścieżkę do swojego `dist/` i wczytaj ją w WinUAE (*Configurations → Load*) albo uruchom `winuae64.exe -f SpeedwayParty-A1200.uae`. Ręcznie:

5. **CD & Hard drives → Add Directory or Archive…**: wskaż katalog `dist/`.
   - *Device name*: `DH0`, *Volume label*: `Speedway`
   - *Boot priority*: 0 lub wyżej (katalog ma być dyskiem startowym)
6. **Game ports:** port 1 = joystick (klawiatura lub pad), port 0 = drugi joystick, jeśli gra 2+ graczy. **Nie zostawiaj myszy w porcie 0**, bo kursor w menu będzie skakał.
7. Start. Kickstart uruchomi `s/startup-sequence`, a ten od razu grę. Workbench nie jest potrzebny.

Jeśli wolisz Workbench: podepnij `dist/` jako dodatkowy dysk i w Shellu wpisz `cd Speedway:` i `SpeedwayParty`.

**ESC** wychodzi z gry i oddaje sterowanie systemowi.

### Gdy coś nie działa
| Objaw | Gdzie szukać |
|---|---|
| Obraz „poszatkowany”, przesunięty o 64 px | `DDF_STOP` i `MOD_ADJ` w `src/video.c` (pobieranie bitplanów w trybie 4×). W WinUAE działa `$38/$A0`. Do testów: `make dist EXTRA=-DDDF_STOP=0xD0` |
| Menu samo się przełącza | mysz w porcie 0: menu czyta tylko port 1 i klawiaturę, ale gracz 2 ustawiony na JOY 0 będzie „jechał” myszą |
| Zamiast grafiki są proste kształty | brak `data/` obok programu (gra wtedy rysuje grafikę zastępczą) |
| Brak dźwięku | brak `data/*.raw` |
| Gracze 3–4 na adapterze LPT nie reagują | bity adaptera w `src/input.c`, oznaczone „sprawdzić” |

---

## Sterowanie

Każdy gracz ma tylko **LEWO i PRAWO**. Gaz jest automatyczny.

| | Gracz 1 | Gracz 2 | Gracz 3 | Gracz 4 |
|---|---|---|---|---|
| domyślnie | joystick port 1 | joystick port 0 | klawisze **Z / X** | klawisze **N / M** |
| do wyboru w menu | + adapter 4 graczy (LPT), strzałki | | | |

- **Start spod taśmy:** trzymaj dowolny kierunek. Na zielonym świetle **puść**. Puszczenie przed zielonym to taśma (falstart). Trzymanie dłużej niż 0,3 s po zielonym to mielenie kołem w miejscu.
- **Jazda:** LEWO łamie motocykl w łuk. Im mocniej złamany, tym większy opór i niższa prędkość, do której może się rozpędzić (przy pełnym złamaniu ok. 25% mniej). PRAWO to kontra: głównie prostuje motocykl, a skręt w prawo jest 3× słabszy niż w lewo. Najszybsze jest pulsacyjne dozowanie uślizgu.
- **Szeroka niesie:** pod bandą, z pełną prędkością i w lekkim uślizgu motocykl dostaje dodatkowe przyspieszenie. Za wolno pod bandą motocykl się zakopuje.
- **Szprycha:** jazda tuż za rywalem w uślizgu lekko spowalnia. Szukaj czystej linii.
- **Menu:** joystick (góra/dół/lewo/prawo/fire) albo strzałki i Return. Nazwisko edytujesz z klawiatury, polskie litery przez Alt (Alt+X = Ź, Alt+Z = Ż).
- Liczbę graczy zmieniasz w pierwszym wierszu ekranu ustawień (LEWO/PRAWO).

---

## Narzędzia na PC

Wymagają Pythona 3 z `numpy`, a sprawdzanie kodu i symulator także `pip install ziglang`.

| Polecenie | Co robi |
|---|---|
| `make assets` | regeneruje grafikę (`tools/genart.py`), dźwięk (`tools/genaudio.py`), font (`tools/genfont.py`) i podglądy w `art/` |
| `make check` | sprawdza składnię i typy całego kodu Clangiem dla 68020 (atrapy NDK w `tools/ndkstub/`) |
| `make sim` | symulator: te same pliki logiki gry rozgrywają turniej botów bez grafiki |
| `sh tools/sim/batch.sh` | 5 turniejów, średnie punkty dla każdego poziomu AI (do strojenia) |
| `python tools/sim/plot.py` | tory jazdy z symulacji na tle toru, zapis do `art/sim_lines.png` |
| `sh tools/shots/run.sh` | zrzuty ekranu z prawdziwego kodu gry (UI, HUD, duszki, szprycha, traktory) skompilowanego na PC, zapis do `art/screen_*.png` |
| `python tools/import_engine.py plik.wav` | robi pętlę silnika dla Pauli z prawdziwego nagrania |

Geometria toru jest zapisana w dwóch miejscach: `src/track.c` i `tools/genart.py` (`CX, CY, HALF_L, R_IN, R_OUT`). Muszą być identyczne.

---

## Struktura projektu

```
src/
  main.c        pętla 50 Hz, maszyna stanów turnieju
  system.c      przejęcie i oddanie sprzętu, własne przerwanie VBL
  video.c       Copper-listy AGA: 8 bitplanów, paleta 24-bit, kolory per linia, HUD, podwójny bufor
  sprites.c     duszki 64 px (pary attached), klatki motocykli
  blit.c        Blitter: wypełnianie, kopiowanie, kropki „cookie-cut” (szprycha)
  start.c       procedura startowa, taśma, refleks
  race.c        model jazdy: auto-gaz, uślizg, banda, kontakt, okrążenia
  ai.c          boty: Amator / Profi / Mistrz
  roost.c       szprycha i woda z polewaczki (cząsteczki Blittera)
  track.c       geometria owalu, mapa przyczepności, wilgotność
  tractors.c    przerwa na tor (traktory na duszkach)
  tournament.c  tabela 15 biegów, punkty, biegi nominowane
  ui.c, text.c  ekrany tekstowe, font z polskimi literami
  hud.c         HUD: kolejność, okrążenie, światła, sędzia z flagą
  audio.c       Paula: silniki, kibice, brzęczyk, gwizdek
  input.c       joysticki, adapter LPT, klawiatura (CIA)
  assets.c      ładowanie data/ przed przejęciem sprzętu
data/           gotowe dane binarne (generowane narzędziami)
art/            podglądy PNG/WAV (nie trafiają na Amigę)
tools/          generatory, sprawdzanie kodu, symulator
```

---

## Znane ograniczenia / do zrobienia
- Gra kompiluje się Bebbo GCC 6.5.0b bez ostrzeżeń i działa w WinUAE (A1200, 68020 cycle-exact, AGA, 2 MB Chip) z ROM-em AROS przy stałych 50 FPS. Nie była jeszcze testowana z prawdziwym Kickstartem 3.1 ani na prawdziwym sprzęcie.
- Słychać silniki tylko 2 zawodników naraz (Paula ma 4 kanały).
- Dźwięk silnika jest syntetyczny. Prawdziwe nagranie można zaimportować przez `tools/import_engine.py`.
- Brak dogrywki przy remisie punktowym (o kolejności decydują zwycięstwa biegów).
- Nie ma ilustracji na ekranie tytułowym.

---

## Licencja
[MIT](LICENSE) © 2026 b4rtiga
