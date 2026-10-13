"""Miniaturka do podgladu linkow na GitHubie (Settings -> Social preview): art/social_preview.jpg, 1280x640
(JPG, bo GitHub przyjmuje najwyzej 1 MB; potrzebny ffmpeg w PATH).

Kadr ilustracji z planszy tytulowej (src/title.png) z napisem SPEEDWAY PARTY pikselowym
fontem gry (tools/genfont.py), w kolorach planszy tytulowej. Bez zaleznosci poza numpy.
Funkcje read_png / write_png uzywaja tez inne narzedzia (gentitle.py)."""
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "art"
W, H = 1280, 640


def read_png(path):
    """Dekoder PNG 8 bit RGB/RGBA (wszystkie filtry)."""
    data = path.read_bytes()
    pos, idat = 8, b""
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        t = data[pos + 4:pos + 8]
        d = data[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", d[:10])
        elif t == b"IDAT":
            idat += d
        pos += 12 + n
    assert depth == 8 and ctype in (2, 6), (path, depth, ctype)
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = np.zeros((h, stride), dtype=np.int32)
    prev = np.zeros(stride, dtype=np.int32)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = np.frombuffer(raw, dtype=np.uint8, count=stride, offset=y * (stride + 1) + 1).astype(np.int32)
        if f == 0:
            cur = line
        elif f == 2:
            cur = (line + prev) & 255
        else:
            cur = np.zeros(stride, dtype=np.int32)
            for x in range(stride):
                a = cur[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                if f == 1:
                    p = a
                elif f == 3:
                    p = (a + b) >> 1
                else:
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[x] = (line[x] + p) & 255
        out[y] = cur
        prev = cur
    return out.reshape(h, w, bpp)[:, :, :3]


def write_png(path, rgb):
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def resize_bilinear(img, w, h):
    sh, sw = img.shape[:2]
    ys = (np.arange(h) + 0.5) * sh / h - 0.5
    xs = (np.arange(w) + 0.5) * sw / w - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, sh - 1); y1 = np.clip(y0 + 1, 0, sh - 1)
    x0 = np.clip(np.floor(xs).astype(int), 0, sw - 1); x1 = np.clip(x0 + 1, 0, sw - 1)
    fy = np.clip(ys - y0, 0, 1)[:, None, None]; fx = np.clip(xs - x0, 0, 1)[None, :, None]
    a = img[y0][:, x0]; b = img[y0][:, x1]; c = img[y1][:, x0]; d = img[y1][:, x1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def draw_text(canvas, text, x, y, scale, color, shadow=(10, 12, 30)):
    """napis fontem gry (litery 5x7, odstep 6) - kazdy piksel fontu = kwadrat scale x scale"""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from genfont import G
    for pass_, col, off in ((0, shadow, scale // 2 + 1), (1, color, 0)):
        cx = x
        for ch in text:
            rows = G.get(ch, G[' ']).split('|')
            for ry, row in enumerate(rows):
                for rx, c in enumerate(row):
                    if c == '#':
                        X, Y = cx + rx * scale + off, y + ry * scale + off
                        canvas[Y:Y + scale, X:X + scale] = col
            cx += 6 * scale


def main():
    src = read_png(ROOT / "src/title.png").astype(np.float64)
    sh, sw = src.shape[:2]
    # kadr 2:1 z zawodnikami (od reflektorow po nawierzchnie)
    ch = int(sw / 2)
    y0 = int(sh * 0.30)
    canvas = resize_bilinear(src[y0:y0 + ch], W, H)
    # lewa gorna czesc przyciemniona pod napis
    yy, xx = np.mgrid[0:H, 0:W]
    dark = np.clip(1.0 - 0.6 * np.clip(1 - xx / 900, 0, 1) * np.clip(1 - yy / 420, 0, 1), 0.35, 1)
    canvas *= dark[..., None]
    draw_text(canvas, "SPEEDWAY", 56, 48, 13, (60, 230, 60))
    draw_text(canvas, "PARTY", 56, 48 + 9 * 13, 13, (255, 200, 40))
    draw_text(canvas, "AMIGA 1200", 60, 48 + 18 * 13 + 6, 5, (245, 245, 245))
    # GitHub: obraz podgladu do 1 MB - ilustracja w PNG ma ~2 MB, wiec zapis JPG przez ffmpeg
    import shutil
    import subprocess
    import tempfile
    ff = shutil.which("ffmpeg")
    if not ff:
        out = ART / "social_preview.png"
        write_png(out, canvas)
        print(out, "(brak ffmpeg - PNG, sprawdz rozmiar: limit GitHuba 1 MB)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / "social.png"
        write_png(png, canvas)
        out = ART / "social_preview.jpg"
        subprocess.run([ff, "-v", "error", "-y", "-i", str(png), "-q:v", "2", str(out)], check=True)
    print(out, W, "x", H, f"{out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
