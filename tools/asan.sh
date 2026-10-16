#!/bin/sh
# Kontrola pamieci (AddressSanitizer + UBSan) na Linuksie / WSL: narzedzie zrzutow we wszystkich
# trybach, po polsku i po angielsku, oraz symulator. Wyjscia za bufor, zle wskazniki itp.,
# ktore na Amidze po cichu psuja stos (np. za krotki bufor napisu), tutaj koncza sie bledem.
# Uzycie (w WSL, w katalogu projektu): sh tools/asan.sh     Wymaga: gcc z libasan.
cd "$(dirname "$0")/.." || exit 1
B=/tmp/speedway-asan
mkdir -p "$B" tools/shots/out tools/shots/out-en
CF="-O1 -g -std=gnu99 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all \
    -Wall -Wno-unused-parameter -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Itools/ndkstub -Isrc"
SRC="tools/shots/host.c src/ui.c src/text.c src/font.c src/hud.c src/sprites.c src/tractors.c src/roost.c src/race.c \
     src/start.c src/ai.c src/tournament.c src/track.c src/fixmath.c src/records.c src/hof.c src/newspaper.c src/audio.c"
export ASAN_OPTIONS=detect_leaks=0
status=0
for lang in PL EN; do
    def=; [ $lang = EN ] && def=-DLANG_EN
    gcc $CF -DHOST_SIM $def tools/shots/shots.c $SRC -o "$B/shots_$lang" || exit 1
    for mode in "" trailer endcard present podium; do
        if ! "$B/shots_$lang" $mode >/dev/null 2>"$B/log"; then
            echo "BLAD: shots $lang ${mode:-zrzuty}"; grep -E "ERROR|runtime error|#[0-4] " "$B/log" | head -12; status=1
        fi
    done
done
gcc $CF tools/sim/sim.c tools/sim/stubs.c src/race.c src/start.c src/ai.c src/tournament.c src/track.c src/fixmath.c \
    src/roost.c src/text.c src/font.c -o "$B/sim" || exit 1
for seed in 1 2 3 4 5; do
    if ! "$B/sim" 15 $seed >/dev/null 2>"$B/log"; then
        echo "BLAD: symulator, ziarno $seed"; grep -E "ERROR|runtime error|#[0-4] " "$B/log" | head -12; status=1
    fi
done
gcc $CF tools/sim/settings_test.c src/records.c src/hof.c -o "$B/settings_test" || exit 1
if ! "$B/settings_test" >/dev/null 2>"$B/log"; then
    echo "BLAD: test ustawien"; grep -E "ERROR|runtime error|#[0-4] " "$B/log" | head -12; status=1
fi
python3 tools/adpcm.py src/music/BlackMotor/black_motor_s8.raw "$B/music.adp" >/dev/null &&
    python3 tools/adpcm.py --decode "$B/music.adp" "$B/music_ref.raw" || exit 1
gcc $CF -DHOST_SIM tools/sim/music_test.c -o "$B/music_test" || exit 1
for m in "$B/music.adp $B/music_ref.raw" "src/music/BlackMotor/black_motor_s8.raw src/music/BlackMotor/black_motor_s8.raw"; do
    if ! "$B/music_test" $m >/dev/null 2>"$B/log"; then
        echo "BLAD: test muzyki ($m)"; grep -E "ERROR|runtime error|#[0-4] " "$B/log" | head -12; status=1
    fi
done
[ $status = 0 ] && echo "OK: bez bledow pamieci (zrzuty PL/EN, trailer, symulator, ustawienia, muzyka)"
exit $status
