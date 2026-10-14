"""mkrelease.py - paczka do pobrania: release/SpeedwayParty.zip z katalogu dist/.

W srodku katalog SpeedwayParty/ (plik gry, data/, s/startup-sequence, CZYTAJ.TXT).
Uruchamiane przez `make release` (po `make dist`)."""
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
OUT = ROOT / "release" / "SpeedwayParty.zip"

README_TXT = """SPEEDWAY PARTY - gra zuzlowa na Amige 1200 (AGA)
==================================================

Wymagania: Amiga 1200 (lub zgodna z chipsetem AGA), 68020, 2 MB Chip RAM,
Kickstart 3.0/3.1. Fast RAM nie jest potrzebny. Dziala tez w WinUAE/FS-UAE.

URUCHOMIENIE
- z Workbencha/Shella: skopiuj katalog SpeedwayParty na dysk i w Shellu:
      cd SpeedwayParty
      SpeedwayParty
  (katalog data/ musi lezec obok pliku SpeedwayParty)
- w emulatorze: podepnij katalog SpeedwayParty jako dysk (Add Directory)
  z priorytetem startu >= 0 - s/startup-sequence uruchomi gre od razu.

STEROWANIE (kazdy gracz: tylko LEWO / PRAWO, gaz automatyczny)
- gracz 1: joystick w porcie 1, gracz 2: port 0, gracze 3-4: klawisze Z/X i N/M
  (do wyboru w ustawieniach takze strzalki i adapter 4 graczy na LPT;
  JOY 1 i strzalki dzialaja zamiennie, jesli nikt inny ich nie uzywa)
- start: trzymaj kierunek pod tasma, PUSC na zielonym swietle
- LEWO lamie motocykl w luk (do 40 st.), PRAWO to kontra
- plansza tytulowa czeka na FIRE / Return, potem JEDZIEMY! albo CREDITS (tworcy)
- menu: joystick w porcie 1 albo strzalki + Return; ESC = wyjscie z gry

Kod zrodlowy i opis: https://github.com/b4rtiga/Speedway-Party
Licencja MIT (c) 2026 b4rtiga
"""


def main():
    if not (DIST / "SpeedwayParty").exists():
        sys.exit("brak dist/SpeedwayParty - najpierw `make dist`")
    OUT.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(DIST.rglob("*")):
            if f.is_file():
                z.write(f, "SpeedwayParty/" + f.relative_to(DIST).as_posix())
        z.writestr("SpeedwayParty/CZYTAJ.TXT", README_TXT.replace("\n", "\n"))
    print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size} B")


if __name__ == "__main__":
    main()
