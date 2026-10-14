#!/bin/sh
# Test gazety (wybor naglowka, migawka turnieju) na PC: sh tools/sim/news_test.sh  (pip: ziglang)
cd "$(dirname "$0")/../.." || exit 1
python -m ziglang cc -O1 -std=gnu99 -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/news_test.c tools/sim/stubs.c src/newspaper.c src/tournament.c src/fixmath.c src/track.c src/roost.c \
    src/text.c src/font.c -o tools/sim/news_test.exe || exit 1
./tools/sim/news_test.exe
