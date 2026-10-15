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

SRC = src/main.c src/newspaper.c src/hof.c src/system.c src/video.c src/sprites.c src/fixmath.c src/track.c \
      src/input.c src/start.c src/race.c src/ai.c src/tournament.c src/hud.c \
      src/blit.c src/text.c src/font.c src/ui.c src/assets.c src/roost.c src/tractors.c src/audio.c src/records.c
OBJ = $(SRC:.c=.o)

SpeedwayParty: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c $< -o $@

DATA = data/track0.pal data/track0.bpl data/title.bpl data/title.pal data/girls.bin data/hud.pal data/referee.bin data/refarm.bin data/bikes.spr        data/tractors.spr data/engine.raw data/crowd.raw data/buzzer.raw data/whistle.raw data/cheer.raw

# Pliki pakowane na dyskietce (tools/pack.py, rozpakowuje assets.c) - w data/ zostaja niespakowane
PACKED = bikes.spr tractors.spr track0.bpl girls.bin

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
	python3 tools/pack.py $(addprefix dist/data/,$(PACKED)) >/dev/null
	python3 tools/mkicons.py tool dist >/dev/null
	python3 tools/mkicons.py disk dist >/dev/null
	echo SpeedwayParty > dist/s/startup-sequence

# Paczka do pobrania: release/SpeedwayParty.zip (plik gry + dane + instrukcja)
# i startowa dyskietka release/SpeedwayParty.adf (FFS, Kickstart 2.0+)
release: dist
	python3 tools/mkrelease.py
	python3 tools/mkadf.py dist release/SpeedwayParty.adf SpeedwayParty

# Wersja WHDLoad (NIEWYDANA, nieprzetestowana: WHDLoad nie dziala pod AROS-em z naszej konfiguracji
# WinUAE - test wymaga oryginalnego Kickstartu 3.1): ten sam kod z -DWHDLOAD (pliki i pamiec przez WHDLoad, bez systemu - src/whd.c,
# naglowki zastepcze src/whd/proto), start src/whdstart.s, slave tools/whdload/slave.s.
# Wynik: katalog dist-whd/ (slave + data/ z programem gry), paczke robi tools/mkrelease.py.
WHD_DIR = build/whd
WHD_OBJ = $(patsubst src/%.c,$(WHD_DIR)/%.o,$(SRC)) $(WHD_DIR)/whd.o
VASM    = vasmm68k_mot -quiet -m68020

$(WHD_DIR)/%.o: src/%.c src/*.h src/whd/proto/*.h
	@mkdir -p $(WHD_DIR)
	$(CC) $(CFLAGS) -DWHDLOAD -Isrc/whd -c $< -o $@

$(WHD_DIR)/whdstart.o: src/whdstart.s
	@mkdir -p $(WHD_DIR)
	$(VASM) -Fhunk -o $@ $<

$(WHD_DIR)/SpeedwayParty: $(WHD_DIR)/whdstart.o $(WHD_OBJ)
	$(CC) -m68020 -nostartfiles -nostdlib -s -o $@ $^ -lgcc

$(WHD_DIR)/SpeedwayParty.slave: tools/whdload/slave.s
	@mkdir -p $(WHD_DIR)
	$(VASM) -Fhunkexe -nosym -o $@ $<

whdload: dist $(WHD_DIR)/SpeedwayParty $(WHD_DIR)/SpeedwayParty.slave
	rm -rf dist-whd
	mkdir -p dist-whd/data
	cp $(WHD_DIR)/SpeedwayParty.slave dist-whd/
	cp $(WHD_DIR)/SpeedwayParty dist-whd/data/
	cp dist/data/* dist-whd/data/
	python3 tools/mkicons.py whdload dist-whd >/dev/null

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
	rm -rf dist dist-whd $(WHD_DIR) data/music.raw .music-*

.PHONY: clean dist release whdload assets check sim
