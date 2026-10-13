#!/bin/sh
# Kilka turniejow z roznymi ziarnami: suma punktow wg poziomu AI.
cd "$(dirname "$0")/../.." || exit 1
for seed in 1 2 3 4 5; do ./tools/sim/sim.exe 15 $seed | sed -n '/KLASYFIKACJA/,$p' | grep -E "^  [0-9]\." ; done | \
awk '{lvl=$3; pts[lvl]+=$4; wins[lvl]+=$6; n[lvl]++} END {for (l in pts) printf "%-7s srednio %5.1f pkt, %4.1f zw. na turniej (na zawodnika)\n", l, pts[l]/n[l], wins[l]/n[l]}'
for seed in 1 2 3 4 5; do ./tools/sim/sim.exe 15 $seed; done | grep -cE "miejsce -" | sed 's/^/nieukonczone biegi (DNF) w 75 biegach: /'
