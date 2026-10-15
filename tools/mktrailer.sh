#!/bin/sh
# Trailer gry: art/trailer.mp4 (1280x1024, 50 fps, podklad "Black Motor" + silniki).
# Klatki rysuje prawdziwy kod gry skompilowany na PC (tools/shots, tryb "trailer"):
# plansza tytulowa, ustawienia, program zawodow, start i 1. okrazenie biegu 1,
# potem przejscie z efektem pikselizacji do ilustracji tytulowej z napisem Z ZUZLOWYM POZDROWIENIEM!
# na 5 s i wyciszenie (bez rozjasniania na poczatku - 1. klatka to plansza tytulowa,
# odtwarzacze pokazuja ja jako miniaturke). Silniki: rejestry Pauli z prawdziwej logiki
# audio.c zapisywane co klatke, dzwiek z data/engine.raw sklada tools/trailer_audio.py.
# Wymaga: pip install ziglang, ffmpeg w PATH.
# LANG_EN=1 sh tools/mktrailer.sh - wersja angielska: art/en/trailer.mp4 (napisy gry po angielsku,
# na koncu SEE YOU AT THE TAPE!) i miniaturka art/en/trailer_thumb.png (klatka z biegu jest bez napisow).
cd "$(dirname "$0")/.." || exit 1
if [ -n "$LANG_EN" ]; then O=tools/shots/out-en; EXE=./tools/shots/shots_en.exe; MP4=art/en/trailer.mp4
else O=tools/shots/out; EXE=./tools/shots/shots.exe; MP4=art/trailer.mp4; fi
sh tools/shots/run.sh >/dev/null || exit 1
TMP=$O/trailer.mkv
REGS=$O/trailer_audio.bin            # okres i glosnosc kanalow silnikow co klatke
ENG=$O/trailer_engines.wav
TITLE=$O/endcard.png                 # ostatnia plansza: ilustracja z napisem fontem gry (tryb endcard)
XF=1.2                               # dlugosc przejscia (s)
HOLD=5                               # ilustracja po przejsciu (s)

$EXE endcard \
  | ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s 320x256 -i - "$TITLE" || exit 1
TRAILER_LAPS=1 TRAILER_AUDIO="$REGS" $EXE trailer \
  | ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s 320x256 -r 50 -i - -c:v libx264rgb -crf 0 -preset veryfast "$TMP" || exit 1
python tools/trailer_audio.py "$REGS" "$ENG" >/dev/null || exit 1
# chwila, od ktorej slychac silniki (pierwsza klatka z glosnoscia kanalu 0) - wtedy muzyka ciszej
ES=$(python -c "import numpy as n; a=n.fromfile('$REGS','<u2').reshape(-1,4); print(round(int(n.nonzero(a[:,1])[0][0])/50,2))")
D=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP")
read OFF TOTAL FO AO <<EOF
$(python -c "d=$D; t=d+$HOLD; print(round(d-$XF,2), round(t,2), round(t-1.0,2), round(t-2.5,2))")
EOF

# dzwiek: muzyka 0.6, w biegu przyciszona do 0.3 (slychac silniki), po biegu wraca; silniki w stereo
# (kanal 0 lewy, 1 prawy jak na Amidze), po biegu cisza
ffmpeg -v error -y -i "$TMP" -loop 1 -t "$(python -c "print($HOLD+$XF)")" -i "$TITLE" \
  -stream_loop -1 -f s8 -ar 15977 -ac 1 -i src/music/BlackMotor/black_motor_s8.raw \
  -i "$ENG" \
  -filter_complex "\
[0:v]scale=1280:1024:flags=neighbor,fps=50,format=yuv420p,setsar=1[g];\
[1:v]scale=1280:1024:flags=neighbor,fps=50,format=yuv420p,setsar=1[t];\
[g][t]xfade=transition=pixelize:duration=$XF:offset=$OFF,fade=t=out:st=$FO:d=1,format=yuv420p[v];\
[2:a]aresample=48000,volume='0.6-0.3*min(1,max(0,(t-$ES)/0.8))*min(1,max(0,($OFF+0.6-t)/0.6))':eval=frame,pan=stereo|c0=c0|c1=c0,afade=t=in:st=0:d=0.6[m];\
[3:a]aresample=48000,apad[e];\
[m][e]amix=inputs=2:duration=first:normalize=0,afade=t=out:st=$AO:d=2.5[a]" \
  -map "[v]" -map "[a]" -t "$TOTAL" \
  -c:v libx264 -pix_fmt yuv420p -profile:v high -preset slow -crf 20 -tune animation -r 50 -c:a aac -b:a 160k -ac 2 -movflags +faststart "$MP4" || exit 1
rm -f "$TMP" "$REGS" "$ENG" "$TITLE"
[ -n "$LANG_EN" ] && cp art/trailer_thumb.png art/en/trailer_thumb.png
echo "$MP4 ($TOTAL s)"
