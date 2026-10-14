"""genreferee.py - portret sedziego w HUD z ilustracji src/referee.png.

Kadr (glowa i gora koszulki w pasy), zmniejszenie do 32x38 z alfa premultiplikowana, sylwetka
bez okruchow alfy. Paleta HUD ma 32 kolory: portret dzieli stale kolory HUD (bialy, czern,
skora dloni, zolty, brazowy, ramka...) i dostaje wlasne odcienie w wolnych indeksach (SLOTS),
dobrane z obrazu (median cut + kilka krokow k-srednich). Indeksy 21/25 (kolor flagi zmieniany
w Copperze) i kolory kaskow/swiatel nie sa uzywane.

Animacja twarzy w src/hud.c (mruganie, zerkanie) rysuje zrenice w szklach okularow:
LENS_X/LENS_Y musza sie zgadzac z lens_x / FACE_Y0 w hud.c. Szkla dostaja jednolity kolor
H_LENS (22), na ktorym gra rysuje zrenice kolorem 11.

genart.py importuje palette() (do data/hud.pal) i image() (do data/referee.bin).
Podglad: build/preview/referee.png. Wymaga numpy i ffmpeg."""
import sys
from functools import lru_cache
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gengirls import read_rgba, box_resize, largest_component, median_cut   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src/referee.png"
W, H = 32, 38
CROP_X = (0.06, 0.94)                  # ulamki szerokosci zrodla; wysokosc z proporcji 32:38 od gory
SLOTS = [17, 18, 20, 23, 24, 26, 27, 28, 29, 30, 31]       # wolne kolory HUD na portret
# skora (10) i jej cien (12): wspolne z reka sedziego rysowana w grze (src/hud.c H_SKIN / H_SKIN_D)
SKIN = {10: (222, 138, 78), 12: (140, 70, 38)}
FIXED = {1: (255, 255, 255), 4: (240, 240, 240), 5: (255, 220, 0), **SKIN,
         11: (20, 20, 30), 14: (10, 10, 10), 15: (120, 70, 30), 16: (120, 140, 200), 19: (225, 225, 230)}
LENS = 22
LENS_X = (10, 18)                      # lewe szkla (4 piksele szerokosci) - hud.c lens_x
LENS_Y = 12                            # dwa wiersze szkiel - hud.c FACE_Y0
LENS_RGB = (58, 50, 66)                # ciemne, dymione szkla (zrenice kolorem 11)
DITHER = 0.0


@lru_cache(maxsize=1)
def portrait():
    """(rgb float 38x32x3, maska widocznosci)"""
    src = read_rgba(SRC)
    sh, sw = src.shape[:2]
    x0, x1 = int(sw * CROP_X[0]), int(sw * CROP_X[1])
    ch = min(sh, round((x1 - x0) * H / W))
    crop = src[:ch, x0:x1].copy()
    crop[..., :3] *= crop[..., 3:4] / 255.0
    s = box_resize(crop, W, H)
    a = s[..., 3]
    rgb = np.clip(s[..., :3] / np.maximum(a[..., None], 1e-6) * 255.0, 0, 255)
    return rgb, largest_component(a >= 128)


@lru_cache(maxsize=1)
def palette():
    """{indeks: (r, g, b)} - wlasne kolory portretu (SLOTS) i szkiel"""
    rgb, vis = portrait()
    px = rgb[vis]
    fixed = np.array(list(FIXED.values()), np.float64)
    far = ((px[:, None] - fixed[None]) ** 2).sum(2).min(1) > 28 ** 2
    cols = median_cut(px[far], len(SLOTS))
    for _ in range(6):                                  # k-srednie: stale kolory nieruchome
        allc = np.concatenate([fixed, cols])
        lab = ((px[:, None] - allc[None]) ** 2).sum(2).argmin(1) - len(fixed)
        for k in range(len(cols)):
            if (lab == k).any():
                cols[k] = px[lab == k].mean(0)
    cols = cols[np.argsort(cols.sum(1), kind="stable")]
    pal = {k: tuple(int(round(v)) for v in c) for k, c in zip(SLOTS, cols)}
    pal[LENS] = LENS_RGB
    pal.update(SKIN)
    return pal


@lru_cache(maxsize=1)
def image():
    """portret 38x32 w indeksach HUD (0 = tlo HUD)"""
    rgb, vis = portrait()
    pal = {**FIXED, **palette()}
    idxs = [k for k in pal if k != LENS]
    cand = np.array([pal[k] for k in idxs], np.float64)
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
    for lx in LENS_X:                                   # oprawka (czarna) i szkla pod animacje zrenic
        out[LENS_Y - 1:LENS_Y + 3, lx - 1:lx + 5] = 14
        out[LENS_Y:LENS_Y + 2, lx:lx + 4] = LENS
    out[LENS_Y, LENS_X[0] + 4:LENS_X[1]] = 14           # mostek
    return out


if __name__ == "__main__":
    from mksocial import write_png
    pal = np.zeros((32, 3))
    pal[0] = (16, 24, 64)
    for k, v in {**FIXED, **palette()}.items():
        pal[k] = v
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
    write_png(ROOT / "build/preview/referee.png", np.repeat(np.repeat(pal[image()], 10, 0), 10, 1))
    print("build/preview/referee.png", palette())
