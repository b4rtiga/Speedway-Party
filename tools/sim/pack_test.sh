#!/bin/sh
# Test rozpakowywania danych (tools/pack.py + asset_load z src/assets.c): sh tools/sim/pack_test.sh
cd "$(dirname "$0")/../.." || exit 1
mkdir -p tools/sim/out
for f in bikes.spr tractors.spr track0.bpl girls.bin; do cp "data/$f" tools/sim/out/; done
python tools/pack.py tools/sim/out/bikes.spr tools/sim/out/tractors.spr tools/sim/out/track0.bpl tools/sim/out/girls.bin >/dev/null || exit 1
python -m ziglang cc -O1 -std=gnu99 -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/pack_test.c src/assets.c -o tools/sim/pack_test.exe || exit 1
./tools/sim/pack_test.exe
