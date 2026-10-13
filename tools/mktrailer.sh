#!/bin/sh
# Trailer gry: art/trailer.mp4 (1280x1024, 50 fps, podklad "Black Motor").
# Klatki rysuje prawdziwy kod gry skompilowany na PC (tools/shots, tryb "trailer"):
# plansza tytulowa, ustawienia, program zawodow, bieg 1 od startu do mety,
# wyniki i tabela z punktacja. Wymaga: pip install ziglang, ffmpeg w PATH.
cd "$(dirname "$0")/.." || exit 1
sh tools/shots/run.sh >/dev/null || exit 1
TMP=tools/shots/out/trailer.mkv
./tools/shots/shots.exe trailer \
  | ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s 320x256 -r 50 -i - -c:v libx264rgb -crf 0 -preset veryfast "$TMP" || exit 1
D=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP")
FO=$(python -c "print(round($D - 2.5, 2))")
ffmpeg -v error -y -i "$TMP" -stream_loop -1 -f s8 -ar 15977 -ac 1 -i src/music/BlackMotor/black_motor_s8.raw -t "$D" \
  -vf "scale=1280:1024:flags=neighbor,fade=t=in:st=0:d=0.6,fade=t=out:st=$FO:d=2.5,format=yuv420p" \
  -af "aresample=48000,volume=0.9,afade=t=in:st=0:d=0.6,afade=t=out:st=$FO:d=2.5" \
  -c:v libx264 -preset slow -crf 20 -tune animation -r 50 -c:a aac -b:a 160k -ac 2 -movflags +faststart art/trailer.mp4 || exit 1
rm -f "$TMP"
echo "art/trailer.mp4 ($D s)"
