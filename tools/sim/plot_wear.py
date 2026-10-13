"""plot_wear.py - mapa ubicia toru po symulacji (tools/sim/wear.csv) -> build/preview/sim_wear.png"""
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genart as g  # noqa: E402

pack = np.loadtxt(ROOT / "tools/sim/wear.csv", delimiter=",")
cell = np.kron(pack, np.ones((8, 8)))[:208, :320]
base = g.pal[g.img].astype(np.float64)
heat = np.zeros_like(base)
heat[..., 0] = 255
heat[..., 1] = 230 - cell * 0.6
a = (cell / 255.0)[..., None] * 0.85
out = (base * 0.55 * (1 - a) + heat * a).clip(0, 255).astype(np.int32)
(ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
g.write_png(ROOT / "build/preview/sim_wear.png", out, 3)
print("build/preview/sim_wear.png")
