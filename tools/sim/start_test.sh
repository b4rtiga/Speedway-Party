#!/bin/sh
# Uczciwosc startu wg pola A-D (4 jednakowe boty): sh tools/sim/start_test.sh [turnieje]  (pip: ziglang)
cd "$(dirname "$0")/../.." || exit 1
python -m ziglang cc -O1 -std=gnu99 -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/start_test.c tools/sim/stubs.c src/race.c src/start.c src/ai.c src/tournament.c \
    src/track.c src/fixmath.c src/roost.c src/text.c src/font.c -o tools/sim/start_test.exe || exit 1
./tools/sim/start_test.exe "$@"
