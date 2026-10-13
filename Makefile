# Speedway Party - Amiga 1200 AGA
# Bebbo GCC: https://github.com/bebbo/amiga-gcc  (NDK 3.9 dolaczony do toolchaina)

CC      = m68k-amigaos-gcc
CFLAGS  = -m68020 -mtune=68020 -O2 -fomit-frame-pointer -noixemul -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s

SRC = src/main.c src/system.c src/video.c src/sprites.c src/fixmath.c src/track.c \
      src/input.c src/start.c src/race.c src/ai.c src/tournament.c src/hud.c \
      src/blit.c src/text.c src/font.c src/ui.c src/assets.c src/roost.c src/tractors.c src/audio.c
OBJ = $(SRC:.c=.o)

SpeedwayParty: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c $< -o $@

DATA = data/track.pal data/track.bpl data/hud.pal data/referee.bin data/bikes.spr        data/tractors.spr data/engine.raw data/crowd.raw data/buzzer.raw data/whistle.raw

# Katalog gotowy do podpiecia w WinUAE jako dysk startowy (bez Workbencha):
# Kickstart 3.x uruchamia s/startup-sequence, a ta od razu gre.
dist: SpeedwayParty $(DATA)
	mkdir -p dist/data dist/s
	cp SpeedwayParty dist/
	cp $(DATA) dist/data/
	echo SpeedwayParty > dist/s/startup-sequence

# Grafika i dzwiek (Python 3 + numpy) - wynik jest juz w repozytorium, to tylko regeneracja
assets:
	python tools/genart.py
	python tools/genaudio.py
	python tools/genfont.py

# Kontrola skladni Clangiem i symulator logiki na PC (pip install ziglang)
check:
	sh tools/check.sh

sim:
	sh tools/sim/run.sh

clean:
	rm -f $(OBJ) SpeedwayParty
	rm -rf dist

.PHONY: clean dist assets check sim
