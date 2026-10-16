#!/bin/sh
# Test muzyki w menu (ADPCM i PCM, strumien przez bufory DMA): sh tools/sim/music_test.sh  (pip: ziglang)
cd "$(dirname "$0")/../.." || exit 1
OUT=tools/sim/out
mkdir -p "$OUT"
RAW=src/music/BlackMotor/black_motor_s8.raw
python tools/adpcm.py "$RAW" "$OUT/music.adp" || exit 1
python tools/adpcm.py --decode "$OUT/music.adp" "$OUT/music_ref.raw" || exit 1
python -m ziglang cc -O1 -std=gnu99 -DHOST_SIM -Wall -Wno-unused-parameter -Wno-pointer-to-int-cast \
    -Wno-int-to-pointer-cast -Itools/ndkstub -Isrc tools/sim/music_test.c -o tools/sim/music_test.exe || exit 1
./tools/sim/music_test.exe "$OUT/music.adp" "$OUT/music_ref.raw" || exit 1
./tools/sim/music_test.exe "$RAW" "$RAW"
