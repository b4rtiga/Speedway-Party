"""gennight.py - zawody w nocy: paleta nocna i mapa swiatla kazdego toru (data/trackNn.pal, data/trackNn.map).

    python tools/gennight.py

Grafika toru zostaje dzienna (data/track0.bpl). W nocy gra przelicza ja na nocna: kazdy piksel
o kolorze c w miejscu o jasnosci lv (0 = noc, 15 = pelne swiatlo reflektorow) dostaje indeks
night_map[lv][c] w palecie nocnej - tak samo przy rysowaniu ekranu, wyjezdzonych sciezek
(track.c: cell_rows), szprycy, dymu, wody i okienka maszyny startowej.

Swiatlo: 4 maszty w rogach trybun, plamy na lukach (plaski srodek, miekka krawedz), proste
w polmroku. Jasnosc w rogach komorek 8x8 (siatka 41x27), miedzy nimi interpolacja dwuliniowa -
ta sama w grze (track_light_at) i tutaj.
Kolor nocny = kolor dzienny x (noc chlodna ... swiatlo cieple). Paleta: 206 wolnych indeksow
(poza 0, 1 - biala tasma, 96..127 - napisy i tablice, 240..255 - duszki), dobrana k-srednimi
z par (kolor, jasnosc) w 6 wymiarach: RGB sucho + RGB mokro (polewanie dziala tez w nocy).

trackNn.pal: 256 RGB sucho + 256 RGB mokro (jak trackN.pal).
trackNn.map: night_map 16 x 256 bajtow, potem siatka swiatla 27 x 41 bajtow (0..15), potem
4 latki masztow 19 x 19 bajtow: gotowe indeksy pikseli wokol lampy (kolor nocny + miekka
poswiata dodana do tego, co pod nia - trybuny przeswituja jak na makiecie), 0 = bez zmian. Plamy swiatla na lukach wg geometrii toru
(tools/genart.py: PRESETS). Podglad: build/preview/noc_<tor>.png.
Wymaga numpy."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mksocial import write_png   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
W, H, P = 320, 208, 8
CELL = 8
GW, GH = W // CELL + 1, H // CELL + 1          # siatka swiatla w rogach komorek: 41 x 27
LEVELS = 16

MASTS = [(28, 22), (292, 22), (28, 190), (292, 190)]
POOLS = [(52, 48, 52, 40), (268, 48, 52, 40), (52, 162, 52, 40), (268, 162, 52, 40)]   # x, y, rx, ry (owal)
CX, CY = 160, 100


def pools_for(half_l, r_in, r_out):
    """plamy na lukach: dla owalu dokladnie POOLS, dla innych torow z jego proporcji"""
    if (half_l, r_in, r_out) == (60, 40, 96):
        return POOLS
    r_mid = (r_in + r_out) / 2
    dx, up, down = half_l + r_mid * 48 / 68, r_mid * 52 / 68, r_mid * 62 / 68
    rx, ry = 52 * r_mid / 68, 40 * r_mid / 68
    return [(CX - dx, CY - up, rx, ry), (CX + dx, CY - up, rx, ry), (CX - dx, CY + down, rx, ry), (CX + dx, CY + down, rx, ry)]
AMB = np.array([0.30, 0.33, 0.50])             # noc: ciemno i chlodno (tor w polmroku, nie w czerni)
LIT = np.array([1.25, 1.15, 0.95])             # swiatlo reflektorow: cieple
POOL_SCALE = 1.25                              # plamy wieksze niz na makiecie: wiecej swiatla na prostych
GLOW_R = 9                                     # latka masztu: 19 x 19 px
GLOW_RGB = np.array([255, 245, 210])           # poswiata lampy (jak makieta noc_A)
FIXED = [0, 1] + list(range(96, 128)) + list(range(240, 256))
SLOTS = [i for i in range(256) if i not in FIXED]


def light(x, y, pools):
    v = 0.0
    for cx, cy, rx, ry in pools:
        rx, ry = rx * POOL_SCALE, ry * POOL_SCALE
        d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
        v += np.exp(-(d ** 2) * 1.2)
    return np.clip(v, 0, 1)


def light_grid(pools):
    gy, gx = np.mgrid[0:GH, 0:GW]
    return np.rint(light(gx * CELL, gy * CELL, pools) * (LEVELS - 1)).astype(np.int32)


def level_map(grid):
    """jasnosc kazdego piksela - ta sama arytmetyka co track_light_at w track.c"""
    yy, xx = np.mgrid[0:H, 0:W]
    gx, gy, tx, ty = xx >> 3, yy >> 3, xx & 7, yy & 7
    a, b = grid[gy, gx], grid[gy, gx + 1]
    c, d = grid[gy + 1, gx], grid[gy + 1, gx + 1]
    return (a * (8 - tx) * (8 - ty) + b * tx * (8 - ty) + c * (8 - tx) * ty + d * tx * ty) >> 6


def factor(lv):
    k = lv / (LEVELS - 1)
    return AMB * (1 - k) + LIT * k


def read_indices(t):
    a = np.frombuffer((ROOT / f"data/track{t}.bpl").read_bytes(), np.uint8).reshape(H, P, W // 8)
    bits = np.unpackbits(a, axis=2)
    idx = np.zeros((H, W), np.int32)
    for p in range(P):
        idx |= bits[:, p, :].astype(np.int32) << p
    return idx


def kmeans(x, w, k, iters=40, seed=7):
    rng = np.random.default_rng(seed)
    order = np.argsort(-w)
    cent = x[order[:k]].copy() if len(x) >= k else x.copy()
    for _ in range(iters):
        d = ((x[:, None, :] - cent[None, :, :]) ** 2).sum(2)
        lab = d.argmin(1)
        for j in range(len(cent)):
            m = lab == j
            if m.any():
                cent[j] = (x[m] * w[m, None]).sum(0) / w[m].sum()
            else:
                cent[j] = x[rng.integers(len(x))]
    return cent


def build(tn, name, half_l, r_in, r_out):
    pal = np.frombuffer((ROOT / f"data/track{tn}.pal").read_bytes(), np.uint8).reshape(2, 256, 3).astype(np.float64)
    dry, wet = pal[0], pal[1]
    idx = read_indices(tn)
    grid = light_grid(pools_for(half_l, r_in, r_out))
    lv = level_map(grid)

    # probki (kolor, jasnosc) z wagami: piksele toru, mozliwe odcienie nawierzchni po ubiciu,
    # kolory rysowane w biegu (drobiny, dym, woda) i okienko maszyny startowej (pelne swiatlo)
    weight = {}
    for c, l in zip(idx.ravel(), lv.ravel()):
        weight[(c, l)] = weight.get((c, l), 0) + 1
    for c in range(16, 56):
        for l in range(LEVELS):
            weight[(c, l)] = weight.get((c, l), 0) + 12
    for c in (29, 37, 45, 53, 214, 215, 133, 134, 135):
        for l in range(LEVELS):
            weight[(c, l)] = weight.get((c, l), 0) + 4
    for c in [129, 19, 20, 21, 22, 24] + list(range(140, 172)):
        weight[(c, LEVELS - 1)] = weight.get((c, LEVELS - 1), 0) + 8
    # latki masztow: kolor nocny piksela + poswiata lampy (exp(-r^2/30), jak makieta noc_A)
    patch = []                                                # (maszt, dy, dx, wektor 6D)
    for m, (mx, my) in enumerate(MASTS):
        for dy in range(-GLOW_R, GLOW_R + 1):
            for dx in range(-GLOW_R, GLOW_R + 1):
                g = 0.9 * np.exp(-(dx * dx + dy * dy) / 30.0)
                x, y = mx + dx, my + dy
                if g < 0.05 or not (0 <= x < W and 0 <= y < H):
                    continue
                c, l = idx[y, x], lv[y, x]
                v = np.concatenate([dry[c] * factor(l) + g * GLOW_RGB, wet[c] * factor(l) + g * GLOW_RGB])
                patch.append((m, dy, dx, np.clip(v, 0, 255)))

    keys = list(weight)
    vec = np.array([np.concatenate([dry[c] * factor(l), wet[c] * factor(l)]) for c, l in keys])
    vec = np.clip(vec, 0, 255)
    w = np.array([weight[k] for k in keys], np.float64)
    vec = np.concatenate([vec, np.array([p[3] for p in patch])])
    w = np.concatenate([w, np.full(len(patch), 6.0)])

    cent = kmeans(vec, w, len(SLOTS))

    night = pal.copy()                                        # stale indeksy jak w palecie dziennej
    for s, cv in zip(SLOTS, cent):
        night[0, s] = cv[:3]
        night[1, s] = cv[3:]

    # night_map[lv][c]: najblizszy kolor palety nocnej dla kazdego koloru dziennego i jasnosci
    nm = np.zeros((LEVELS, 256), np.uint8)
    cand = np.array([np.concatenate([night[0, s], night[1, s]]) for s in SLOTS[:len(cent)]])
    for l in range(LEVELS):
        t = np.clip(np.concatenate([dry * factor(l), wet * factor(l)], axis=1), 0, 255)
        d = ((t[:, None, :] - cand[None, :, :]) ** 2).sum(2)
        nm[l] = np.array(SLOTS)[d.argmin(1)]
    pt = np.zeros((4, 2 * GLOW_R + 1, 2 * GLOW_R + 1), np.uint8)
    for m, dy, dx, v in patch:
        pt[m, dy + GLOW_R, dx + GLOW_R] = SLOTS[int(((cand - v) ** 2).sum(1).argmin())]

    (ROOT / f"data/track{tn}n.pal").write_bytes(bytes(np.clip(night, 0, 255).astype(np.uint8).ravel()))
    (ROOT / f"data/track{tn}n.map").write_bytes(bytes(nm.ravel()) + bytes(grid.astype(np.uint8).ravel())
                                               + bytes(pt.ravel()))

    # podglad: tak jak policzy gra (bez duszkow), z latkami masztow
    out = nm[lv, idx]
    for m, (mx, my) in enumerate(MASTS):
        for dy in range(-GLOW_R, GLOW_R + 1):
            for dx in range(-GLOW_R, GLOW_R + 1):
                v = pt[m, dy + GLOW_R, dx + GLOW_R]
                if v and 0 <= mx + dx < W and 0 <= my + dy < H:
                    out[my + dy, mx + dx] = v
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
    write_png(ROOT / f"build/preview/noc_{name}.png", np.repeat(np.repeat(night[0][out], 3, 0), 3, 1))
    print(f"data/track{tn}n.pal, data/track{tn}n.map, build/preview/noc_{name}.png")


def main():
    from genart import PRESETS                  # geometria torow (ta sama co w track.c)
    for t, (name, half_l, r_in, r_out, _surface) in enumerate(PRESETS):
        build(t, name, half_l, r_in, r_out)


if __name__ == "__main__":
    main()
