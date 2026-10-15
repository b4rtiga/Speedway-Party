"""mkrelease.py - paczka do pobrania: release/SpeedwayParty.zip z katalogu dist/.

W srodku katalog SpeedwayParty/ (plik gry z ikona, data/, s/startup-sequence, CZYTAJ.TXT
z ikona) i ikona szuflady SpeedwayParty.info obok niego. CZYTAJ.TXT z ikona trafia tez do
dist/ - dyskietke (tools/mkadf.py) buduje make release po tym skrypcie. Disk.info jest tylko
na dyskietce. Uruchamiane przez `make release` (po `make dist`)."""
import pathlib
import sys
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mkicons  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
OUT = ROOT / "release" / "SpeedwayParty.zip"

README_TXT = """SPEEDWAY PARTY - gra zuzlowa na Amige 1200 (AGA)
==================================================

Wymagania: Amiga 1200 (lub zgodna z chipsetem AGA), 68020, 2 MB Chip RAM,
Kickstart 3.0/3.1/3.2. Fast RAM nie jest potrzebny (jesli jest - gra z niego
korzysta). Gra powinna ruszyc na Amidze 1200 (takze bez Fast RAM), A1200 z karta
turbo, Amidze 4000 i innych komputerach z AGA oraz w emulatorach (WinUAE, FS-UAE,
EmulatorJS) w konfiguracji A1200.

URUCHOMIENIE
- z Workbencha: skopiuj katalog SpeedwayParty (z ikona SpeedwayParty.info) na dysk
  i kliknij dwa razy ikone gry w szufladzie SpeedwayParty
- z Shella:
      cd SpeedwayParty
      SpeedwayParty
  (katalog data/ musi lezec obok pliku SpeedwayParty)
- z dyskietki SpeedwayParty.adf: wloz do DF0: i wlacz Amige - gra rusza sama
- w emulatorze: podepnij katalog SpeedwayParty jako dysk (Add Directory)
  z priorytetem startu >= 0 - s/startup-sequence uruchomi gre od razu.

STEROWANIE (kazdy gracz: tylko LEWO / PRAWO, gaz automatyczny)
- gracz 1: joystick w porcie 1, gracz 2: port 0, gracze 3-4: klawisze Z/X i N/M
  (do wyboru w ustawieniach takze strzalki i adapter 4 graczy na LPT;
  JOY 1 i strzalki dzialaja zamiennie, jesli nikt inny ich nie uzywa)
- start: trzymaj kierunek pod tasma, PUSC na zielonym swietle (brzeczyk i trzask tasmy)
- LEWO lamie motocykl w luk (do 40 st.), PRAWO to kontra
- najlepsza linia (ciemniejsza, wyjezdzona sciezka) jest inna na kazdych zawodach;
  na niej uslizg mniej hamuje
- plansza tytulowa czeka na FIRE / Return, potem JEDZIEMY!, INSTRUKCJA
  (zasady, start, linia jazdy), REKORDY (TOP 5 biegow i okrazen graczy) albo CREDITS
- gracz w TOP 5 wpisuje imie raz, po turnieju (7 znakow): GORA/DOL znak, LEWO/PRAWO pozycja,
  mozna tez pisac z klawiatury, FIRE zapisz
- rekordy zapisuja sie do halloffame.dat przy wyjsciu z gry (ESC na ekranie
  wyboru i ESC jeszcze raz); reset Amigi w trakcie gry traci wyniki z sesji
- menu: joystick w porcie 1 albo 0 (FIRE = zatwierdz) albo strzalki + Return;
  czym zatwierdzisz JEDZIEMY!, tym steruje GRACZ 1 (JOY 0, JOY 1 albo STRZALKI)
- ESC albo drugi przycisk joysticka = pauza / wyjscie

Kod zrodlowy i opis: https://github.com/b4rtiga/Speedway-Party
Licencja MIT (c) 2026 b4rtiga
"""


def main():
    if not (DIST / "SpeedwayParty").exists():
        sys.exit("brak dist/SpeedwayParty - najpierw `make dist`")
    OUT.parent.mkdir(exist_ok=True)
    (DIST / "CZYTAJ.TXT").write_text(README_TXT, encoding="latin-1", newline="\n")
    (DIST / "CZYTAJ.TXT.info").write_bytes(mkicons.ICONS["readme"][1]())
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("SpeedwayParty.info", mkicons.ICONS["drawer"][1]())     # ikona szuflady
        for f in sorted(DIST.rglob("*")):
            if f.is_file() and f.name != "Disk.info":
                z.write(f, "SpeedwayParty/" + f.relative_to(DIST).as_posix())
    print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size} B")


if __name__ == "__main__":
    main()
