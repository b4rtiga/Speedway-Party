"""gentitle.py - plansza tytulowa z ilustracji: data/title.bpl + data/title.pal (+ art/title_art.png).

    python tools/gentitle.py [src/title.png]

Kadr 320x256 z obrazu (dowolny rozmiar PNG 8 bit RGB/RGBA), wlasna paleta 222 kolorow
(median cut) i dithering Floyda-Steinberga. Indeksy 0 (tlo z Coppera), 96..112 (kolory napisow
menu, m.in. 100 = teczowy kolor z Coppera) i 240..255 (duszki) zostaja nietkniete - ich RGB
kopiujemy z palety toru (data/track0.pal), wiec napisy w menu wygladaja jak dotad.
Bez zaleznosci poza numpy."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mksocial import read_png, write_png       # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
W, H = 320, 256
FREE = [i for i in range(1, 96)] + [i for i in range(113, 240)]   # indeksy dla obrazu

# kadr w ukladzie zrodla (ulamki szerokosci/wysokosci): zawodnicy, reflektory, trybuny
CROP = (0.0, 0.215, 1.0, 0.86)                   # x0, y0, x1, y1


def box_resize(img, w, h):
    """zmniejszenie z usrednianiem (kazdy piksel celu = srednia prostokata zrodla)"""
    sh, sw = img.shape[:2]
    ys = np.linspace(0, sh, h + 1).astype(int)
    xs = np.linspace(0, sw, w + 1).astype(int)
    c = np.cumsum(np.cumsum(np.pad(img.astype(np.float64), ((1, 0), (1, 0), (0, 0))), 0), 1)
    y0, y1 = ys[:-1][:, None], ys[1:][:, None]
    x0, x1 = xs[:-1][None, :], xs[1:][None, :]
    s = c[y1, x1] - c[y0, x1] - c[y1, x0] + c[y0, x0]
    return s / ((y1 - y0) * (x1 - x0))[..., None]


def median_cut(px, n):
    boxes = [px]
    while len(boxes) < n:
        i = max(range(len(boxes)), key=lambda k: len(boxes[k]) * np.ptp(boxes[k], axis=0).max() if len(boxes[k]) > 1 else -1)
        b = boxes.pop(i)
        ch = int(np.argmax(np.ptp(b, axis=0)))
        b = b[np.argsort(b[:, ch])]
        m = len(b) // 2
        boxes += [b[:m], b[m:]]
    return np.array([b.mean(axis=0) for b in boxes])


def to_planar(idx, depth=8):
    h, w = idx.shape
    out = bytearray()
    for y in range(h):
        for p in range(depth):
            out += np.packbits(((idx[y] >> p) & 1).astype(np.uint8)).tobytes()
    return bytes(out)


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "src/title.png"
    img = read_png(src).astype(np.float64)
    sh, sw = img.shape[:2]
    x0, y0, x1, y1 = CROP
    crop = img[int(y0 * sh):int(y1 * sh), int(x0 * sw):int(x1 * sw)]
    small = box_resize(crop, W, H)
    # gora i dol lagodnie przyciemnione - tam leza napisy menu (tytul u gory, podpisy u dolu)
    yy = np.arange(H)[:, None, None]
    shade = np.clip(0.5 + yy / 150.0, 0, 1) * np.clip(0.55 + (H - 1 - yy) / 120.0, 0, 1)
    small *= shade

    # paleta obrazu + zarezerwowane kolory z palety toru
    track_pal = np.frombuffer((ROOT / "data/track0.pal").read_bytes()[:768], np.uint8).reshape(256, 3)
    pal = track_pal.astype(np.float64).copy()
    cols = median_cut(small.reshape(-1, 3), len(FREE))
    pal[FREE] = cols
    cand = pal[FREE]

    # dithering Floyda-Steinberga tylko na kolorach obrazu
    work = small.copy()
    idx = np.zeros((H, W), np.int32)
    for y in range(H):
        for x in range(W):
            old = work[y, x]
            k = int(np.argmin(((cand - old) ** 2).sum(1)))
            idx[y, x] = FREE[k]
            err = old - cand[k]
            if x + 1 < W: work[y, x + 1] += err * 7 / 16
            if y + 1 < H:
                if x > 0: work[y + 1, x - 1] += err * 3 / 16
                work[y + 1, x] += err * 5 / 16
                if x + 1 < W: work[y + 1, x + 1] += err * 1 / 16

    (ROOT / "data/title.bpl").write_bytes(to_planar(idx, 8))
    (ROOT / "data/title.pal").write_bytes(bytes(np.clip(pal, 0, 255).astype(np.uint8).ravel()))
    write_png(ROOT / "art/title_art.png", np.repeat(np.repeat(pal[idx], 2, 0), 2, 1))
    print("data/title.bpl, data/title.pal, art/title_art.png")


if __name__ == "__main__":
    main()
