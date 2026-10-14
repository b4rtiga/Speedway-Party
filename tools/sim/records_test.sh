#!/bin/sh
# Test tabeli rekordow (TOP 5, plik halloffame.dat) na PC: sh tools/sim/records_test.sh  (pip: ziglang)
cd "$(dirname "$0")/../.." || exit 1
python -m ziglang cc -O1 -std=gnu99 -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/records_test.c src/hof.c -o tools/sim/records_test.exe || exit 1
./tools/sim/records_test.exe
