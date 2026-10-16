"""mkrelease.py - paczki do pobrania z katalogow dist/ (PL) i dist-en/ (EN).

release/SpeedwayParty.zip: katalog SpeedwayParty/ z obiema wersjami jezykowymi na dysk twardy -
SpeedwayParty_PL i SpeedwayParty_EN (kazda z ikona), wspolne data/, s/startup-sequence (PL),
CZYTAJ.TXT i README.TXT z ikonami - oraz ikona szuflady SpeedwayParty.info obok katalogu.
Instrukcje z ikonami trafiaja tez do dist/ (CZYTAJ.TXT) i dist-en/ (README.TXT) - dyskietki
(tools/mkadf.py) buduje make release po tym skrypcie. Disk.info jest tylko na dyskietkach.
Uruchamiane przez `make release` (po `make dist dist-en`)."""
import pathlib
import sys
import time
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mkicons  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
DIST_EN = ROOT / "dist-en"
OUT = ROOT / "release" / "SpeedwayParty.zip"

README_PL = """SPEEDWAY PARTY - gra zuzlowa na Amige 1200 (AGA)
==================================================

Wymagania: Amiga 1200 (lub zgodna z chipsetem AGA), 68020, 2 MB Chip RAM,
Kickstart 3.0/3.1/3.2. Fast RAM nie jest potrzebny (jesli jest - gra z niego
korzysta). Gra powinna ruszyc na Amidze 1200 (takze bez Fast RAM), A1200 z karta
turbo, Amidze 4000 i innych komputerach z AGA oraz w emulatorach (WinUAE, FS-UAE,
EmulatorJS) w konfiguracji A1200.

WERSJE JEZYKOWE
- SpeedwayParty_PL - po polsku, SpeedwayParty_EN - po angielsku (English version);
  obie korzystaja z tego samego katalogu data/ i tych samych rekordow
- dyskietki: SpeedwayParty_PL.adf i SpeedwayParty_EN.adf

URUCHOMIENIE
- z Workbencha: skopiuj katalog SpeedwayParty (z ikona SpeedwayParty.info) na dysk
  i kliknij dwa razy ikone SpeedwayParty_PL w szufladzie SpeedwayParty
- z Shella:
      cd SpeedwayParty
      SpeedwayParty_PL
  (katalog data/ musi lezec obok pliku gry)
- z dyskietki SpeedwayParty_PL.adf: wloz do DF0: i wlacz Amige - gra rusza sama
- w emulatorze: podepnij katalog SpeedwayParty jako dysk (Add Directory)
  z priorytetem startu >= 0 - s/startup-sequence uruchomi gre od razu (po polsku).

STEROWANIE (kazdy gracz: tylko LEWO / PRAWO, gaz automatyczny)
- gracz 1: joystick w porcie 1, gracz 2: port 0, gracze 3-4: klawisze Z/X i N/M
  (do wyboru w ustawieniach takze strzalki i adapter 4 graczy na LPT;
  JOY 1 i strzalki dzialaja zamiennie, jesli nikt inny ich nie uzywa)
- start: trzymaj kierunek pod tasma, PUSC na zielonym swietle (brzeczyk i trzask tasmy)
- LEWO lamie motocykl w luk (do 40 st.), PRAWO to kontra
- najlepsza linia (ciemniejsza, wyjezdzona sciezka) jest inna na kazdych zawodach;
  na niej uslizg mniej hamuje
- plansza tytulowa czeka na FIRE / Return, potem TURNIEJ, INSTRUKCJA
  (zasady, start, linia jazdy), REKORDY (TOP 5 biegow i okrazen graczy) albo CREDITS
- gracz w TOP 5 wpisuje imie raz, po turnieju (7 znakow): GORA/DOL znak, LEWO/PRAWO pozycja,
  mozna tez pisac z klawiatury, FIRE zapisz
- rekordy (halloffame.dat) i ustawienia (settings.dat) zapisuja sie przy wyjsciu
  z gry (ESC na ekranie wyboru i ESC jeszcze raz); reset Amigi w trakcie gry traci
  wyniki z sesji
- menu: joystick w porcie 1 albo 0 (FIRE = zatwierdz) albo strzalki + Return;
  czym zatwierdzisz TURNIEJ, tym steruje GRACZ 1 (JOY 0, JOY 1 albo STRZALKI)
- ESC albo drugi przycisk joysticka = pauza / wyjscie

Kod zrodlowy i opis: https://github.com/b4rtiga/Speedway-Party
Licencja MIT (c) 2026 b4rtiga
"""

README_EN = """SPEEDWAY PARTY - a speedway racing game for the Amiga 1200 (AGA)
===============================================================

Requirements: Amiga 1200 (or any AGA machine), 68020, 2 MB Chip RAM,
Kickstart 3.0/3.1/3.2. Fast RAM is not required (used if present). The game
should run on an Amiga 1200 (also without Fast RAM), an A1200 with an
accelerator card, an Amiga 4000 and other AGA machines, and in emulators
(WinUAE, FS-UAE, EmulatorJS) set up as an A1200.

LANGUAGES
- SpeedwayParty_EN - English, SpeedwayParty_PL - Polish (wersja polska);
  both use the same data/ directory and share the same records
- floppy disks: SpeedwayParty_EN.adf and SpeedwayParty_PL.adf

RUNNING THE GAME
- from Workbench: copy the SpeedwayParty drawer (with SpeedwayParty.info) to your
  hard disk and double-click the SpeedwayParty_EN icon in the SpeedwayParty drawer
- from the Shell:
      cd SpeedwayParty
      SpeedwayParty_EN
  (the data/ directory must sit next to the game file)
- from the SpeedwayParty_EN.adf floppy: insert it in DF0: and switch the Amiga on -
  the game starts by itself
- in an emulator: mount the SpeedwayParty directory as a hard disk (Add Directory)
  with boot priority >= 0 - s/startup-sequence starts the game (in Polish; change
  it to SpeedwayParty_EN for the English version).

CONTROLS (each player: LEFT / RIGHT only, the throttle is automatic)
- player 1: joystick in port 1, player 2: port 0, players 3-4: keys Z/X and N/M
  (cursor keys and a 4-player parallel port adapter can be chosen in the settings;
  JOY 1 and the cursor keys work interchangeably if nobody else uses them)
- start: hold a direction at the tape and LET GO on the green light (buzzer and
  the tape snapping)
- LEFT lays the bike into a slide (up to 40 degrees), RIGHT counter-steers
- the best racing line (a darker, worn-in groove) is different at every meeting;
  sliding on it costs less speed
- the title screen waits for FIRE / Return, then TOURNAMENT, HOW TO PLAY
  (rules, start, racing line), RECORDS (TOP 5 heats and laps by players) or CREDITS
- a player in the TOP 5 enters a name once, after the tournament (7 characters):
  UP/DOWN letter, LEFT/RIGHT position, or type on the keyboard, FIRE saves
- records (halloffame.dat) and settings (settings.dat) are saved when you quit
  (ESC on the title screen and ESC once more); resetting the Amiga during play
  loses the session's results
- menus: joystick in port 1 or 0 (FIRE = confirm) or cursor keys + Return;
  whatever you confirm TOURNAMENT with becomes PLAYER 1's control
  (JOY 0, JOY 1 or CURSORS)
- ESC or the second joystick button = pause / quit

Source code and description: https://github.com/b4rtiga/Speedway-Party
MIT License (c) 2026 b4rtiga
"""


def main():
    for d in (DIST, DIST_EN):
        if not (d / "SpeedwayParty").exists():
            sys.exit(f"brak {d.name}/SpeedwayParty - najpierw `make dist dist-en`")
    OUT.parent.mkdir(exist_ok=True)
    (DIST / "CZYTAJ.TXT").write_text(README_PL, encoding="latin-1", newline="\n")
    (DIST / "CZYTAJ.TXT.info").write_bytes(mkicons.ICONS["readme"][1]())
    (DIST_EN / "README.TXT").write_text(README_EN, encoding="latin-1", newline="\n")
    (DIST_EN / "README.TXT.info").write_bytes(mkicons.ICONS["readme_en"][1]())
    tool = mkicons.ICONS["tool"][1]()
    files = {                                                  # sciezka w zipie -> zawartosc
        "SpeedwayParty.info": mkicons.ICONS["drawer"][1](),    # ikona szuflady
        "SpeedwayParty/SpeedwayParty_PL": (DIST / "SpeedwayParty").read_bytes(),
        "SpeedwayParty/SpeedwayParty_PL.info": tool,
        "SpeedwayParty/SpeedwayParty_EN": (DIST_EN / "SpeedwayParty").read_bytes(),
        "SpeedwayParty/SpeedwayParty_EN.info": tool,
        "SpeedwayParty/s/startup-sequence": b"SpeedwayParty_PL\n",
    }
    for d, names in ((DIST, ("CZYTAJ.TXT", "CZYTAJ.TXT.info")), (DIST_EN, ("README.TXT", "README.TXT.info"))):
        for n in names:
            files["SpeedwayParty/" + n] = (d / n).read_bytes()
    for f in sorted((DIST / "data").rglob("*")):
        if f.is_file():
            files["SpeedwayParty/" + f.relative_to(DIST).as_posix()] = f.read_bytes()
    now = time.localtime()[:6]
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=now)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, files[name], compresslevel=9)
    print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size} B")


if __name__ == "__main__":
    main()
