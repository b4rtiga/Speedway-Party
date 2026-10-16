#!/bin/sh
# Test zapisu i odczytu ustawien (settings.dat) na PC: sh tools/sim/settings_test.sh  (pip: ziglang)
cd "$(dirname "$0")/../.." || exit 1
python -m ziglang cc -O1 -std=gnu99 -Wall -Wno-unused-parameter -Itools/ndkstub -Isrc \
    tools/sim/settings_test.c src/records.c src/hof.c -o tools/sim/settings_test.exe || exit 1
./tools/sim/settings_test.exe
