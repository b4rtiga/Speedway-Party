"""preview_roost.py - podglad szprycy: ten sam model emisji/ruchu co src/roost.c
(liczony na float), nalozony na makiete toru. Zapisuje art/mockup_roost.png."""
import math
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import genart as g  # noqa: E402

rng = np.random.default_rng(3)
MAX_SPEED = 1.8
DUST = [48, 49, 50, 17]


def bend_pos(t, lateral):
    """pozycja na prawym luku: t=0 dol, t=1 gora (jazda przeciwnie do wskazowek)"""
    a = math.pi / 2 - t * math.pi                       # 90st (poludnie) -> -90st (polnoc)
    r = g.R_MID + lateral
    x = g.CX + g.HALF_L + math.cos(a) * r
    y = g.CY + math.sin(a) * r
    heading = a - math.pi / 2                           # styczna przeciwnie do wskazowek
    return x, y, heading


def simulate(riders, frames=40):
    parts = []
    for f in range(frames):
        for p in parts:
            p[0] += p[2]; p[1] += p[3]
            p[2] -= p[2] / 4; p[3] -= p[3] / 4
            p[4] -= 1
        parts = [p for p in parts if p[4] > 0]
        for (t0, lat, drift) in riders:
            t = t0 + f * 0.012
            x, y, h = bend_pos(t, lat)
            fc, fs = math.cos(h), math.sin(h)
            n = 1 + (drift - 14) // 10
            for _ in range(n):
                back = MAX_SPEED * 0.9 / 2 + drift / 20 + rng.uniform(-1, 1)
                side = drift / 22 + rng.uniform(-1, 1)
                parts.append([x - fc * 9, y - fs * 9, -fc * back - fs * side, -fs * back + fc * side,
                              8 + rng.integers(0, 9), DUST[rng.integers(0, 4)], rng.random() < 0.75])
    bikes = []
    for (t0, lat, drift) in riders:
        x, y, h = bend_pos(t0 + frames * 0.012, lat)
        d = round(h / (2 * math.pi) * 32) % 32
        bikes.append((round(x), round(y), d, 2 if drift > 26 else 1))
    return parts, bikes


def main():
    riders = [(0.30, 6, 44), (0.18, 4, 30)]             # lider szeroko, rywal tuz za nim w szprycy
    parts, bikes = simulate(riders)
    frames = {}
    for (_, _, d, k) in bikes:
        frames[(d, k)] = g.render_bike(d, k)
    scr = g.compose_screen(g.pal, frames, [])
    for p in parts:
        x, y = int(p[0]), int(p[1])
        size = 2 if p[6] else 1
        scr[y:y + size, x:x + size] = g.pal[p[5]]
    for gate, (x, y, d, k) in zip((0, 2), bikes):
        f = frames[(d, k)].copy()
        f[f == g.HELMET_SLOT] = g.HELMETS[gate]
        for yy in range(32):
            for xx in range(32):
                if f[yy, xx]:
                    scr[y - 16 + yy, x - 16 + xx] = g.pal[240 + f[yy, xx]]
    crop = scr[20:200, 150:320]
    (g.ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
    g.write_png(g.ROOT / "build/preview/mockup_roost.png", crop, 4)
    print("build/preview/mockup_roost.png,", len(parts), "drobin")


if __name__ == "__main__":
    main()
