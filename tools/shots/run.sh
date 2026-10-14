#!/bin/sh
# Zrzuty ekranu z prawdziwego kodu gry na PC (pip: ziglang). Wynik: art/screen_*.png
cd "$(dirname "$0")/../.." || exit 1
mkdir -p tools/shots/out
python -m ziglang cc -O1 -std=gnu99 -DHOST_SIM -Wall -Wno-unused-parameter \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Itools/ndkstub -Isrc \
    tools/shots/shots.c tools/shots/host.c src/ui.c src/text.c src/font.c src/hud.c \
    src/sprites.c src/tractors.c src/roost.c src/race.c src/start.c src/ai.c \
    src/tournament.c src/track.c src/fixmath.c src/records.c src/newspaper.c \
    -o tools/shots/shots.exe || exit 1
./tools/shots/shots.exe && python tools/shots/topng.py
