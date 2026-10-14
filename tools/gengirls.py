"""gengirls.py - podprowadzajace na ekran prezentacji przed biegiem: data/girls.bin.

Zrodlo: ilustracja src/girls.png (cztery postacie obok siebie na przezroczystym tle,
rece w gorze - trzymaja tablice w kolorze pola, rysowana w grze). Kolejnosc = pola A..D:
blondynka / czerwony pasek, ciemny bob / niebieski, rudy kucyk / szary, brazowy kok / zolty.

Kazda postac: kadr z kanalu alfa (bez drobnych okruchow wokol sylwetki), zmniejszenie
z usrednianiem (alfa premultiplikowana) do 48x96, wspolna paleta 31 kolorow dobrana z obrazu
(median cut) w indeksach 180..191 i 221..239 + kolor pola z UI (103/104/102/106),
lekki dithering Floyda-Steinberga. Paleta trafia do palety toru (tools/genart.py importuje
palette()) i zostaje w palecie planszy tytulowej (tools/gentitle.py).

Plik: 4 postacie po 96 wierszy x 8 planow x 6 bajtow (planarnie, jak bitplany gry), indeks 0
= przezroczystosc. Podglad: build/preview/girls.png. Wymaga numpy i ffmpeg (dekodowanie PNG)."""
import shutil
import subprocess
import sys
from functools import lru_cache
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mksocial import write_png    # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src/girls.png"
W, H = 48, 96
SLOTS = list(range(180, 192)) + list(range(221, 240))      # 31 kolorow postaci
GATE = [103, 104, 102, 106]                                # pasek/lamowka: czerwony, niebieski, (bialy->szary), zolty
UI_RGB = {102: (150, 150, 175), 103: (235, 25, 25), 104: (30, 90, 255), 106: (255, 220, 0)}
DITHER = 0.45                                              # sila ditheringu (twarze bez szumu)


def read_rgba(path):
    """PNG -> tablica HxWx4 przez ffmpeg (8 bit RGBA)."""
    ff = shutil.which("ffmpeg") or sys.exit("gengirls: potrzebny ffmpeg w PATH")
    probe = subprocess.run([shutil.which("ffprobe") or "ffprobe", "-v", "error", "-select_streams", "v:0",
                            "-show_entries", "stream=width,height", "-of", "csv=p=0", str(path)],
                           capture_output=True, text=True, check=True).stdout.strip().split(",")
    w, h = int(probe[0]), int(probe[1])
    raw = subprocess.run([ff, "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(h, w, 4).astype(np.float64)


def box_resize(img, w, h):
    """zmniejszenie z usrednianiem (kazdy piksel celu = srednia prostokata zrodla)"""
    sh, sw = img.shape[:2]
    ys = np.linspace(0, sh, h + 1).astype(int)
    xs = np.linspace(0, sw, w + 1).astype(int)
    c = np.cumsum(np.cumsum(np.pad(img, ((1, 0), (1, 0), (0, 0))), 0), 1)
    y0, y1 = ys[:-1][:, None], ys[1:][:, None]
    x0, x1 = xs[:-1][None, :], xs[1:][None, :]
    s = c[y1, x1] - c[y0, x1] - c[y1, x0] + c[y0, x0]
    return s / ((y1 - y0) * (x1 - x0))[..., None]


def largest_component(mask):
    """najwiekszy spojny (8-sasiedztwo) obszar maski - sylwetka bez okruchow"""
    lab = np.zeros(mask.shape, np.int32)
    best, best_n, n = 0, 0, 0
    for sy, sx in zip(*np.nonzero(mask)):
        if lab[sy, sx]:
            continue
        n += 1
        stack, cnt = [(sy, sx)], 0
        lab[sy, sx] = n
        while stack:
            y, x = stack.pop()
            cnt += 1
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    Y, X = y + dy, x + dx
                    if 0 <= Y < mask.shape[0] and 0 <= X < mask.shape[1] and mask[Y, X] and not lab[Y, X]:
                        lab[Y, X] = n
                        stack.append((Y, X))
        if cnt > best_n:
            best, best_n = n, cnt
    return lab == best


@lru_cache(maxsize=1)
def sprites():
    """cztery postacie 48x96: (rgb float, maska widocznosci)"""
    src = read_rgba(SRC)
    sh, sw = src.shape[:2]
    out = []
    for g in range(4):
        tile = src[:, round(g * sw / 4):round((g + 1) * sw / 4)]
        a = tile[..., 3]
        # kadr: wiersze/kolumny z wyraznie kryjacymi pikselami (okruchy wokol sylwetki sa rzadkie)
        rows = np.nonzero((a >= 200).sum(1) >= 4)[0]
        cols = np.nonzero((a >= 200).sum(0) >= 4)[0]
        crop = tile[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]
        ch, cw = crop.shape[:2]
        th = H - 1                                          # wysokosc postaci (palce na gorze kadru)
        tw = min(W - 2, max(1, round(cw * th / ch)))
        pm = crop.copy()
        pm[..., :3] *= pm[..., 3:4] / 255.0                 # alfa premultiplikowana - bez obwodek
        small = box_resize(pm, tw, th)
        alpha = small[..., 3]
        rgb = small[..., :3] / np.maximum(alpha[..., None], 1e-6) * 255.0
        full_rgb = np.zeros((H, W, 3))
        full_a = np.zeros((H, W))
        x0 = (W - tw) // 2
        full_rgb[:th, x0:x0 + tw] = rgb
        full_a[:th, x0:x0 + tw] = alpha
        vis = largest_component(full_a >= 128)
        out.append((np.clip(full_rgb, 0, 255), vis))
    return out


def median_cut(px, n):
    boxes = [px]
    while len(boxes) < n:
        i = max(range(len(boxes)), key=lambda k: len(boxes[k]) * np.ptp(boxes[k], axis=0).max() if len(boxes[k]) > 1 else -1)
        b = boxes.pop(i)
        ch = int(np.argmax(np.ptp(b, axis=0)))
        b = b[np.argsort(b[:, ch], kind="stable")]
        m = len(b) // 2
        boxes += [b[:m], b[m:]]
    return np.array([b.mean(axis=0) for b in boxes])


@lru_cache(maxsize=1)
def palette():
    """{indeks: (r, g, b)} - wspolne kolory postaci (deterministycznie z obrazu)"""
    px = np.concatenate([rgb[vis] for rgb, vis in sprites()])
    # piksele bliskie kolorom pol nie zajmuja slotow - dostana kolor z UI
    acc = np.array(list(UI_RGB.values()), np.float64)
    d = ((px[:, None, :] - acc[None]) ** 2).sum(2).min(1)
    px = px[d > 60 ** 2]
    cols = median_cut(px, len(SLOTS))
    cols = cols[np.argsort(cols.sum(1), kind="stable")]     # od ciemnych do jasnych
    return {k: tuple(int(round(v)) for v in c) for k, c in zip(SLOTS, cols)}


def quantize(rgb, vis, gate):
    pal = palette()
    idxs = list(pal) + [GATE[gate]]
    cand = np.array([pal[k] for k in pal] + [UI_RGB[GATE[gate]]], np.float64)
    wgt = np.array([0.30, 0.59, 0.11]) * 3
    work = rgb.copy()
    out = np.zeros((H, W), np.int32)
    for y in range(H):
        for x in range(W):
            if not vis[y, x]:
                continue
            old = work[y, x]
            k = int(np.argmin((((cand - old) ** 2) * wgt).sum(1)))
            out[y, x] = idxs[k]
            err = (old - cand[k]) * DITHER
            for dx, dy, f in ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)):
                X, Y = x + dx, y + dy
                if 0 <= X < W and Y < H and vis[Y, X]:
                    work[Y, X] += err * f / 16
    return out


def to_planar(a, depth=8):
    out = bytearray()
    for y in range(a.shape[0]):
        for p in range(depth):
            out += np.packbits(((a[y] >> p) & 1).astype(np.uint8)).tobytes()
    return bytes(out)


def main():
    imgs = [quantize(rgb, vis, g) for g, (rgb, vis) in enumerate(sprites())]
    (ROOT / "data/girls.bin").write_bytes(b"".join(to_planar(i) for i in imgs))
    # podglad na tle menu
    pal = np.zeros((256, 3))
    pal[0] = (20, 16, 60)
    for k, v in {**UI_RGB, **palette()}.items():
        pal[k] = v
    row = np.concatenate([np.pad(i, ((4, 4), (4, 4))) for i in imgs], 1)
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
    write_png(ROOT / "build/preview/girls.png", np.repeat(np.repeat(pal[row], 4, 0), 4, 1))
    print("data/girls.bin, build/preview/girls.png")


if __name__ == "__main__":
    main()
