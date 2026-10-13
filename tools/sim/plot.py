"""plot.py - slady jazdy z tools/sim/trace.csv na tle toru -> art/sim_lines.png"""
import csv
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genart as g  # noqa: E402

COLORS = [(235, 25, 25), (30, 90, 255), (255, 255, 255), (255, 214, 0)]
scr = (g.pal[g.img] * 0.6).astype(np.int32)
with open(ROOT / "tools/sim/trace.csv") as f:
    for row in csv.DictReader(f):
        if int(row["state"]) not in (2, 3):
            continue
        x, y, gate = int(row["x"]), int(row["y"]), int(row["gate"])
        if 0 <= x < 320 and 0 <= y < 208:
            scr[y, x] = COLORS[gate]
g.write_png(ROOT / "art/sim_lines.png", scr, 3)
print("art/sim_lines.png")
