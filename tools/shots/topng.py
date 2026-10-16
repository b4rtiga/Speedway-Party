"""topng.py - tools/shots/out/*.ppm -> art/screen_*.png (powiekszenie 3x)
Opcjonalnie: topng.py <katalog ppm> <katalog png> [lista,nazw] (wersja angielska: out-en -> art/en,
tylko zrzuty uzywane w dokumentacji EN i na stronie). Zrzuty test_* (kontrolne) zostaja w out/."""
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genart  # noqa: E402

SRC = ROOT / (sys.argv[1] if len(sys.argv) > 1 else "tools/shots/out")
DEST = ROOT / (sys.argv[2] if len(sys.argv) > 2 else "art")
ONLY = set(sys.argv[3].split(",")) if len(sys.argv) > 3 else None
for ppm in sorted(SRC.glob("*.ppm")):
    if ppm.stem.startswith("test_") or (ONLY is not None and ppm.stem not in ONLY):
        continue
    body = ppm.read_bytes().split(b"255\n", 1)[1]
    img = np.frombuffer(body, np.uint8).reshape(256, 320, 3).astype(np.int32)
    out = DEST / f"screen_{ppm.stem}.png"
    genart.write_png(out, img, 3)
    print(out.name)
