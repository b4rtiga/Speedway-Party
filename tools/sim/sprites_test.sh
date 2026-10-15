#!/bin/sh
# Test kopii klatek motocykli (1 komplet + przemalowanie kasku) wzgledem dawnego pliku
# z 4 kompletami z wersji v0.84: sh tools/sim/sprites_test.sh  (pip: ziglang)
cd "$(dirname "$0")/../.." || exit 1
mkdir -p tools/sim/out
git show v0.84:data/bikes.spr > tools/sim/out/bikes_4sets.spr || exit 1
python -m ziglang cc -O1 -std=gnu99 -DHOST_SIM -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/sprites_test.c src/sprites.c -o tools/sim/sprites_test.exe || exit 1
./tools/sim/sprites_test.exe tools/sim/out/bikes_4sets.spr
