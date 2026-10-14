# Speedway Party - Amiga 1200 AGA
# Bebbo GCC: https://github.com/bebbo/amiga-gcc  (NDK 3.9 dolaczony do toolchaina)

# ustawienia budowania (np. wybor muzyki) - patrz build.cfg
-include build.cfg
MUSIC ?= BlackMotor

CC      = m68k-amigaos-gcc
# -fbbb=- : wylaczony dodatkowy optymalizator Bebbo ("bbb") - przy skokach przez tablice
#           switch usuwal potrzebne przepisanie rejestru adresowego (zly odczyt pola struktury)
CFLAGS  = -m68020 -mtune=68020 -O2 -fbbb=- -fomit-frame-pointer -noixemul -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s
CFLAGS += $(EXTRA)

SRC = src/main.c src/newspaper.c src/system.c src/video.c src/sprites.c src/fixmath.c src/track.c \
      src/input.c src/start.c src/race.c src/ai.c src/tournament.c src/hud.c \
      src/blit.c src/text.c src/font.c src/ui.c src/assets.c src/roost.c src/tractors.c src/audio.c src/records.c
OBJ = $(SRC:.c=.o)

SpeedwayParty: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c $< -o $@

DATA = data/track0.pal data/track0.bpl data/title.bpl data/title.pal data/girls.bin data/hud.pal data/referee.bin data/refarm.bin data/bikes.spr        data/tractors.spr data/engine.raw data/crowd.raw data/buzzer.raw data/whistle.raw

# Muzyka w menu: miks z src/music/$(MUSIC)/ kopiowany jako data/music.raw (MUSIC=BRAK - bez muzyki).
# Plik jest odtwarzany przy kazdej zmianie build.cfg albo wybranego utworu.
MUSIC_RAW = $(wildcard src/music/$(MUSIC)/*_s8.raw)
ifneq ($(MUSIC),BRAK)
ifeq ($(MUSIC_RAW),)
$(error Brak utworu src/music/$(MUSIC)/*_s8.raw - sprawdz MUSIC w build.cfg)
endif
DATA += data/music.raw
endif

data/music.raw: $(MUSIC_RAW) build.cfg .music-$(MUSIC)
	cp $(MUSIC_RAW) $@

# znacznik wybranego utworu: zmiana MUSIC z linii polecen tez wymusza ponowne skopiowanie
.music-$(MUSIC):
	rm -f .music-*
	touch $@

# Katalog gotowy do podpiecia w WinUAE jako dysk startowy (bez Workbencha):
# Kickstart 3.x uruchamia s/startup-sequence, a ta od razu gre.
dist: SpeedwayParty $(DATA)
	mkdir -p dist/data dist/s
	cp SpeedwayParty dist/
	rm -f dist/data/music.raw
	cp $(DATA) dist/data/
	echo SpeedwayParty > dist/s/startup-sequence

# Paczka do pobrania: release/SpeedwayParty.zip (plik gry + dane + instrukcja)
# i startowa dyskietka release/SpeedwayParty.adf (FFS, Kickstart 2.0+)
release: dist
	python3 tools/mkrelease.py
	python3 tools/mkadf.py dist release/SpeedwayParty.adf SpeedwayParty

# Grafika i dzwiek (Python 3 + numpy) - wynik jest juz w repozytorium, to tylko regeneracja
assets:
	python tools/genart.py
	python tools/gentitle.py
	python tools/gengirls.py
	python tools/genaudio.py
	python tools/import_engine.py src/speedway_500cc_10000rpm_loop.wav
	python tools/genfont.py

# Kontrola skladni Clangiem i symulator logiki na PC (pip install ziglang)
check:
	sh tools/check.sh

sim:
	sh tools/sim/run.sh

clean:
	rm -f $(OBJ) SpeedwayParty
	rm -rf dist data/music.raw .music-*

.PHONY: clean dist release assets check sim
