"""Miniaturka do podgladu linkow na GitHubie (Settings -> Social preview): art/social_preview.png, 1280x640.

Sklada zrzuty z prawdziwego kodu gry (sh tools/shots/run.sh): plansza tytulowa i ekran
biegu, oba 2x, obok siebie na granatowym tle z pasami kraweznika.
Tylko calkowite powiekszenia - piksele zostaja ostre.
"""
import struct
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "art"
W, H = 1280, 640


def read_png(path):
    """Dekoder PNG 8 bit RGB/RGBA (wszystkie filtry) - wystarczy do naszych zrzutow."""
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
        cur = np.zeros(stride, dtype=np.int32)
        if f == 0:
            cur = line
        elif f == 2:
            cur = (line + prev) & 255
        else:
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
    rgb = rgb.astype(np.uint8)
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def native(img):
    """zrzuty sa zapisane w 3x - z powrotem do 320x256"""
    return img[::3, ::3]


def up(img, k):
    return np.repeat(np.repeat(img, k, 0), k, 1)


def main():
    race = native(read_png(ART / "screen_05_racing.png"))
    title = native(read_png(ART / "screen_01_title.png"))

    # tlo: granat z pasami kraweznika (czerwono-biale) u gory i u dolu
    canvas = np.zeros((H, W, 3)) + np.array([10, 12, 34])
    white = ((np.arange(W) // 16) & 1)[:, None]
    stripe = white * np.array([235, 235, 235]) + (1 - white) * np.array([210, 30, 30])
    for y in (34, 598):
        canvas[y:y + 8] = stripe

    # dwa ekrany 2x obok siebie: plansza tytulowa i bieg (boki 8 px przyciete)
    panels = [up(title, 2)[:, 16:-16], up(race, 2)[:, 16:-16]]
    ph, pw = panels[0].shape[:2]
    gap = (W - 2 * pw) // 3
    y0 = (H - ph) // 2
    for i, p in enumerate(panels):
        x0 = gap + i * (pw + gap)
        canvas[y0 - 3:y0 + ph + 3, x0 - 3:x0 + pw + 3] = (245, 245, 245)
        canvas[y0:y0 + ph, x0:x0 + pw] = p

    out = ART / "social_preview.png"
    write_png(out, np.clip(canvas, 0, 255))
    print(out, canvas.shape[1], "x", canvas.shape[0])


if __name__ == "__main__":
    main()
