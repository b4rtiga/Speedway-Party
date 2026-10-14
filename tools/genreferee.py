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


# ------------------------- reka z flaga (HUD) -------------------------
# Uniesiona reka sedziego w dwoch fazach machania, malowana jak portret: walce z cieniowaniem
# (swiatlo z lewej gory), rekaw w pasy, opalone przedramie i piesc w kolorach portretu.
# Obrazek ARM_W x ARM_H od (ARM_X, HUD_TOP + ARM_Y) w ukladzie HUD (src/hud.c), 0 = przezroczyste.
# Drzewce i flage rysuje gra; piesc obejmuje drzewce (x 256-257).
ARM_X, ARM_Y, ARM_W, ARM_H = 248, 14, 32, 24
SS = 4                                   # nadprobkowanie przy malowaniu


def _ramp(targets, exclude=(0, 21, 25, LENS)):
    """najblizsze kolory palety HUD (portret + stale) dla kolejnych tonow"""
    pal = {**FIXED, **palette()}
    keys = [k for k in pal if k not in exclude]
    cols = np.array([pal[k] for k in keys], np.float64)
    return [keys[int(((cols - np.array(t)) ** 2).sum(1).argmin())] for t in targets]


def _capsule(px, py, a, b, r0, r1):
    """odleglosc od odcinka a-b (promien zmienny r0..r1), parametr t i strona (-1..1)"""
    ax, ay = a; bx, by = b
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = np.clip(((px - ax) * dx + (py - ay) * dy) / L2, 0, 1)
    cx, cy = ax + t * dx, ay + t * dy
    r = r0 + (r1 - r0) * t
    ox, oy = px - cx, py - cy
    d = np.hypot(ox, oy)
    L = np.sqrt(L2)
    side = (ox * -dy + oy * dx) / L / np.maximum(r, 1e-6)      # znak: po ktorej stronie osi
    return d <= r, t, np.clip(side, -1, 1), ox / np.maximum(r, 1e-6), oy / np.maximum(r, 1e-6)


def _shade(nx, ny):
    """jasnosc walca: normalna (nx, ny) w plaszczyznie obrazu, swiatlo z lewej gory"""
    nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
    lx, ly, lz = -0.55, -0.6, 0.58
    return np.clip(0.25 + 0.75 * (nx * lx + ny * ly + nz * lz), 0, 1)


def arm_image(wave):
    skin = _ramp([(105, 50, 25), (145, 72, 38), (185, 100, 52), (218, 135, 78), (240, 172, 110)])
    white = _ramp([(120, 112, 105), (170, 160, 150), (215, 210, 205), (245, 245, 245)])
    black = _ramp([(12, 12, 12), (45, 38, 34), (80, 70, 62)])
    h, w = ARM_H * SS, ARM_W * SS
    yy, xx = (np.mgrid[0:h, 0:w] + 0.5) / SS
    out = np.zeros((h, w), np.int32)
    hy = 5.0 if wave else 7.0                                  # piesc (hy w hud.c: HUD_TOP+19 / +21)
    shoulder, elbow, wrist = (28.5, 17.5), (17.0, 18.5), (10.0, hy + 2.5)
    # przedramie (pod rekawem przy lokciu)
    m, t, s, nx, ny = _capsule(xx, yy, elbow, wrist, 2.3, 1.8)
    tone = _shade(nx, ny)
    out[m] = np.array(skin)[np.minimum((tone[m] * len(skin)).astype(int), len(skin) - 1)]
    # rekaw: od barku do lokcia, pasy w poprzek reki
    m, t, s, nx, ny = _capsule(xx, yy, shoulder, elbow, 3.0, 2.6)
    tone = _shade(nx, ny)
    stripe = ((t * 11.5 / 2).astype(int) % 2) == 0
    wt = np.array(white)[np.minimum((tone * len(white)).astype(int), len(white) - 1)]
    bk = np.array(black)[np.minimum((tone * len(black)).astype(int), len(black) - 1)]
    out[m] = np.where(stripe, wt, bk)[m]
    # piesc na drzewcu
    fx, fy = 8.8, hy + 0.8
    ox, oy = (xx - fx) / 2.7, (yy - fy) / 2.6
    m = ox * ox + oy * oy <= 1
    tone = _shade(ox, oy)
    out[m] = np.array(skin)[np.minimum((tone[m] * len(skin)).astype(int), len(skin) - 1)]
    # zmniejszenie: najczestszy kolor w bloku SS x SS, przezroczyste przy pokryciu < 1/2
    blocks = out.reshape(ARM_H, SS, ARM_W, SS).transpose(0, 2, 1, 3).reshape(ARM_H, ARM_W, SS * SS)
    img = np.zeros((ARM_H, ARM_W), np.int32)
    for y in range(ARM_H):
        for x in range(ARM_W):
            v = blocks[y, x]
            v = v[v > 0]
            if len(v) * 2 >= SS * SS:
                vals, cnt = np.unique(v, return_counts=True)
                img[y, x] = vals[cnt.argmax()]
    return img


if __name__ == "__main__":
    from mksocial import write_png
    pal = np.zeros((32, 3))
    pal[0] = (16, 24, 64)
    for k, v in {**FIXED, **palette()}.items():
        pal[k] = v
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
    write_png(ROOT / "build/preview/referee.png", np.repeat(np.repeat(pal[image()], 10, 0), 10, 1))
    arms = np.concatenate([np.pad(arm_image(w), 2) for w in (0, 1)], 1)
    write_png(ROOT / "build/preview/referee_arm.png", np.repeat(np.repeat(pal[arms], 10, 0), 10, 1))
    print("build/preview/referee.png, referee_arm.png", palette())
