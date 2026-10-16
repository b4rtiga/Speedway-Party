#!/bin/sh
# Zrzuty ekranu z prawdziwego kodu gry na PC (pip: ziglang). Wynik: art/screen_*.png
# LANG_EN=1 sh tools/shots/run.sh - wersja angielska, zrzuty do art/en/ (do dokumentacji angielskiej)
cd "$(dirname "$0")/../.." || exit 1
if [ -n "$LANG_EN" ]; then OUT=tools/shots/out-en; DEF=-DLANG_EN; EXE=tools/shots/shots_en.exe; DEST=art/en
else OUT=tools/shots/out; DEF=; EXE=tools/shots/shots.exe; DEST=art; fi
mkdir -p "$OUT" "$DEST"
rm -f "$OUT"/*.ppm                     # stare zrzuty (np. po zmianie nazwy) nie wracaja do art/
python -m ziglang cc -O1 -std=gnu99 -DHOST_SIM $DEF -Wall -Wno-unused-parameter \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Itools/ndkstub -Isrc \
    tools/shots/shots.c tools/shots/host.c src/ui.c src/text.c src/font.c src/hud.c \
    src/sprites.c src/tractors.c src/roost.c src/race.c src/start.c src/ai.c \
    src/tournament.c src/track.c src/fixmath.c src/records.c src/hof.c src/newspaper.c src/audio.c \
    -o "$EXE" || exit 1
# angielskie: tylko zrzuty z docs/DOCUMENTATION.md i ze strony gry (galeria)
EN_SHOTS=02_setup,03_table,04_gate_red,05_racing,06_false_start,07_result,08_grading,10_podium,13_sciezki,16_awaria,17_prezentacja,18_menu,23_gazeta,24_imie,25_rekordy,27_defekt,29_opcje,30_noc,31_upadek,32_fotofinisz,33_deszcz,34_trening,35_mecz,38_mistrzostwa,39_trening_ustawienia
"./$EXE" && python tools/shots/topng.py "$OUT" "$DEST" ${LANG_EN:+$EN_SHOTS}
