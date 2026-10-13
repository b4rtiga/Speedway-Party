"""topng.py - tools/shots/out/*.ppm -> art/screen_*.png (powiekszenie 3x)"""
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genart  # noqa: E402

for ppm in sorted((ROOT / "tools/shots/out").glob("*.ppm")):
    body = ppm.read_bytes().split(b"255\n", 1)[1]
    img = np.frombuffer(body, np.uint8).reshape(256, 320, 3).astype(np.int32)
    out = ROOT / "art" / f"screen_{ppm.stem}.png"
    genart.write_png(out, img, 3)
    print(out.name)
