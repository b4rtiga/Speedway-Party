#!/bin/sh
# Buduje symulator kompilatorem hosta (pip: ziglang) i uruchamia: sh tools/sim/run.sh [biegi] [ziarno]
cd "$(dirname "$0")/../.." || exit 1
python -m ziglang cc -O1 -std=gnu99 -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/sim.c tools/sim/stubs.c src/race.c src/start.c src/ai.c src/tournament.c \
    src/track.c src/fixmath.c src/roost.c -o tools/sim/sim.exe || exit 1
./tools/sim/sim.exe "$@"
