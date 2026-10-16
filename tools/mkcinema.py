#!/usr/bin/env python3
"""mkcinema.py - filmowy trailer gry: art/trailer.mp4 (1280x720, 50 fps, 35 s).

    python tools/mkcinema.py          po polsku  -> art/trailer.mp4, art/trailer_thumb.png
    python tools/mkcinema.py --en     po angielsku -> art/en/trailer.mp4, art/en/trailer_thumb.png
    (--reuse: bez ponownego renderowania gry - tylko kamera, napisy i dzwiek)

Material: prawdziwy kod gry skompilowany na PC (tools/shots, tryb "cinema") - sceny: start
spod tasmy, szpryca, noc, deszcz, defekt, upadek w 1. luku, fotofinisz (zwolnione tempo
i stopklatka), mistrzostwa z fajerwerkami, plansza koncowa. Tu: wirtualna kamera 16:9 (podaza
za motocyklami z zapisu pozycji, odjazdy i najazdy), skalowanie "ostre dwuliniowe" (wyrazne
piksele przy plynnym ruchu kamery), napisy fontem gry (src/font.c) i dzwiek: silniki z rejestrow
Pauli (tools/trailer_audio.py), muzyka Black Motor, gwar, brzeczyk i okrzyk z data/ (bez gwizdka - na zuzlu sie nie gwizdze).
Wymaga: numpy, ffmpeg w PATH, pip install ziglang (kompilacja tools/shots)."""
import os
import pathlib
import re
import subprocess
import sys
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genart  # noqa: E402  (write_png)

EN = "--en" in sys.argv
W, H, FPS = 1280, 720, 50
SW, SH, PLAY_H = 320, 256, 208
WORK = ROOT / "build" / ("cinema_en" if EN else "cinema")
OUT = ROOT / ("art/en" if EN else "art")

CAPTIONS = {        # scena: (tekst PL, tekst EN, od klatki sceny, do klatki sceny)
    1: ("JEDNA TAŚMA", "ONE TAPE", 15, 145),
    2: ("SZPRYCA W TWARZ", "DIRT IN YOUR FACE", 10, 150),
    3: ("ZAWODY W NOCY", "UNDER THE LIGHTS", 10, 150),
    4: ("DESZCZ", "RAIN", 10, 150),
    5: ("DEFEKT!", "ENGINE FAILURE!", 40, 200),
    6: ("UPADEK!", "CRASH!", 62, 165),
    7: ("FOTOFINISZ", "PHOTO FINISH", 84, 300),
    8: ("MECZE I MISTRZOSTWA", "MATCHES AND CHAMPIONSHIPS", 10, 150),
}
SUBTITLE = ("AMIGA 1200   1-4 GRACZY", "AMIGA 1200   1-4 PLAYERS")


# ------------------------------------------------------------------ material z gry
def render_game():
    WORK.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, CINEMA_META=str(WORK / "meta.txt"), TRAILER_AUDIO=str(WORK / "regs.bin"))
    if EN:
        env["LANG_EN"] = "1"
    subprocess.run(["sh", "tools/shots/run.sh"], cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)
    exe = ROOT / "tools/shots" / ("shots_en.exe" if EN else "shots.exe")
    with open(WORK / "frames.rgb", "wb") as f:
        r = subprocess.run([str(exe), "cinema"], cwd=ROOT, env=env, stdout=f, stderr=subprocess.PIPE, text=True)
    for line in r.stderr.splitlines():
        if line.startswith("cinema:"):
            print(line)
    if r.returncode:
        sys.exit("mkcinema: tryb cinema zakonczony bledem")


# ------------------------------------------------------------------ font gry (src/font.c)
def load_font():
    src = (ROOT / "src/font.c").read_text(encoding="utf-8")
    body = src.split("g_font[FONT_GLYPHS][FONT_H] = {", 1)[1].split("};", 1)[0]
    rows = [[int(v, 16) for v in m.split(",")] for m in re.findall(r"\{([^}]*)\}", body)]
    chars = [chr(c) for c in range(32, 96)] + list("ĄĆĘŁŃÓŚŹŻ")
    return {ch: rows[i] for i, ch in enumerate(chars)}


FONT = load_font()


def text_mask(s, scale):
    """napis -> maska (wysokosc 10*scale) fontem gry"""
    cols = []
    for ch in s.upper():
        g = FONT.get(ch, FONT[" "])
        cell = np.array([[(row >> (7 - b)) & 1 for b in range(8)] for row in g], np.uint8)
        cols.append(cell)
    m = np.concatenate(cols, axis=1) if cols else np.zeros((10, 8), np.uint8)
    return np.kron(m, np.ones((scale, scale), np.uint8)).astype(bool)


def draw_text(img, s, x, y, scale, color, shadow=(10, 8, 20), visible=None):
    if visible is not None:
        s = s[:visible]
    if not s:
        return
    m = text_mask(s, scale)
    h, w = m.shape
    for (dx, dy, col) in ((scale, scale, shadow), (0, 0, color)):
        x0, y0 = x + dx, y + dy
        sub = img[y0:y0 + h, x0:x0 + w]
        mm = m[:sub.shape[0], :sub.shape[1]]
        sub[mm] = col


# ------------------------------------------------------------------ kamera
def sharp_sample(src, cx, cy, ww):
    """okno ww x ww*9/16 (srodek cx, cy) -> 1280x720, piksele ostre, krawedzie wygladzone"""
    wh = ww * 9 / 16
    x0, y0 = cx - ww / 2, cy - wh / 2
    s = W / ww
    sx = x0 + (np.arange(W) + 0.5) * ww / W - 0.5
    sy = y0 + (np.arange(H) + 0.5) * wh / H - 0.5
    ix, iy = np.floor(sx).astype(int), np.floor(sy).astype(int)
    fx = np.clip((sx - ix - 0.5) * s + 0.5, 0, 1)
    fy = np.clip((sy - iy - 0.5) * s + 0.5, 0, 1)
    ix0, ix1 = np.clip(ix, 0, SW - 1), np.clip(ix + 1, 0, SW - 1)
    iy0, iy1 = np.clip(iy, 0, SH - 1), np.clip(iy + 1, 0, SH - 1)
    a = src[iy0][:, ix0].astype(np.float32)
    b = src[iy0][:, ix1].astype(np.float32)
    c = src[iy1][:, ix0].astype(np.float32)
    d = src[iy1][:, ix1].astype(np.float32)
    fx = fx[None, :, None]
    fy = fy[:, None, None]
    top = a + (b - a) * fx
    bot = c + (d - c) * fx
    return top + (bot - top) * fy


def clamp_cam(cx, cy, ww, track):
    wh = ww * 9 / 16
    ymax = PLAY_H if track else SH
    cx = min(max(cx, ww / 2), SW - ww / 2)
    cy = min(max(cy, wh / 2), ymax - wh / 2) if wh <= ymax else ymax / 2
    return cx, cy


def ease(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def pack(m):
    pts = [(m["bx"][i], m["by"][i]) for i in range(4) if m["st"][i] <= 3]
    if not pts:
        pts = [(m["bx"][i], m["by"][i]) for i in range(4) if m["st"][i] != 9]
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)) if pts else (160, 104)


def target(scene, f, n, m, first):
    """cel kamery (cx, cy, ww, tor?) dla klatki f z n klatek sceny"""
    fx, fy = m["bx"][m["focus"]], m["by"][m["focus"]]
    if scene == 1:
        if f < 150:                                   # zblizenie na tasme, lekki najazd
            cx, cy = pack(first)
            return cx, cy - 4, 120 - 20 * ease(f / 150), True
        k = ease((f - 150) / 70)                      # zielone: odjazd na caly tor
        px, py = pack(m)
        return px + (160 - px) * k, py + (104 - py) * k, 100 + 220 * k, True
    if scene == 2:
        return fx, fy, 150, True
    if scene == 3:
        k = ease(f / n)
        px, py = pack(m)
        return 160 + (px - 160) * k, 104 + (py - 104) * k, 320 - 120 * k, True
    if scene == 4:
        px, py = pack(m)
        return px, py, 170, True
    if scene == 5:
        return fx, fy, 130, True
    if scene == 6:
        if f >= n - 35:                               # sedzia z czerwona flaga (HUD)
            return 289, 232, 96, False
        if m["phase"] == 5:                           # bieg przerwany (HEAT_FALSE_START): tablica UPADEK!
            return 160, 80, 260, True
        return fx, fy, 130, True
    if scene == 7:
        sx, sy = m["bx"][m["second"]], m["by"][m["second"]]
        if m["flags"] & 2:                            # stopklatka: powolny najazd na linie
            return (fx + sx) / 2, (fy + sy) / 2, 110 - 30 * ease(f / n), True
        if m["flags"] & 1:
            return (fx + sx) / 2, (fy + sy) / 2, 120, True
        return (fx + sx) / 2, (fy + sy) / 2, 170, True
    if scene == 8:                                    # klasyfikacja: od tytulu w dol do mistrza
        return 160, 90 + 76 * ease(f / n), 320, False
    return 160, 90, 304 + 16 * ease(f / n), False   # plansza koncowa: lekki odjazd


# ------------------------------------------------------------------ obraz
def read_meta():
    meta = []
    for line in (WORK / "meta.txt").read_text().split("\n"):
        v = line.split()
        if not v:
            continue
        v = list(map(int, v))
        meta.append(dict(scene=v[0], phase=v[1], flags=v[2], focus=v[3], second=v[4],
                         bx=v[5::3][:4], by=v[6::3][:4], st=v[7::3][:4]))
    return meta


def make_video(meta):
    n = len(meta)
    raw = np.memmap(WORK / "frames.rgb", np.uint8, "r", shape=(n, SH, SW, 3))
    scenes = {}
    for i, m in enumerate(meta):
        scenes.setdefault(m["scene"], []).append(i)
    ff = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "slow",
                           "-crf", "21", "-tune", "animation", str(WORK / "video.mp4")], stdin=subprocess.PIPE)
    thumb = None
    cam = None
    for sc, idx in sorted(scenes.items()):
        first = meta[idx[0]]
        cam = None
        cap = CAPTIONS.get(sc)
        for f, i in enumerate(idx):
            m = meta[i]
            cx, cy, ww, track = target(sc, f, len(idx), m, first)
            cx, cy = clamp_cam(cx, cy, ww, track)
            # kamera: plynnie za celem (ciecie na sedziego - od razu)
            if cam is None or (sc == 6 and f == len(idx) - 35):
                cam = [cx, cy, ww]
            else:
                a = 0.10
                cam = [cam[0] + (cx - cam[0]) * a, cam[1] + (cy - cam[1]) * a, cam[2] + (ww - cam[2]) * 0.12]
            ccx, ccy = clamp_cam(cam[0], cam[1], cam[2], track)
            img = sharp_sample(raw[i], ccx, ccy, cam[2])
            # przejscia: z czerni na poczatku, do czerni na koncu
            g = i
            if g < 20:
                img *= g / 20
            if g >= n - 40:
                img *= max(0.0, (n - 1 - g) / 40)
            img = np.clip(img, 0, 255).astype(np.uint8)
            if sc == 6 and (m["phase"] == 5 or f >= len(idx) - 35):
                cap = None                                        # tablica UPADEK! z gry i sedzia - bez napisu
            if cap and cap[2] <= f < cap[3]:
                text = cap[1] if EN else cap[0]
                vis = (f - cap[2]) // 2 + 1                       # literka po literce
                draw_text(img, text, 56, H - 56 - 60, 6, (255, 204, 40), visible=vis)
            if sc == 9 and f >= 20:
                sub = SUBTITLE[1 if EN else 0]
                m3 = text_mask(sub, 3)
                draw_text(img, sub, (W - m3.shape[1]) // 2, H - 70, 3, (235, 235, 235),
                          visible=(f - 20) // 2 + 1)
            if sc == 7 and f == len(idx) - 60:
                thumb = img.copy()
            ff.stdin.write(img.tobytes())
    ff.stdin.close()
    ff.wait()
    return thumb


# ------------------------------------------------------------------ dzwiek
RATE = 48000


def load_s8(name, rate):
    a = np.frombuffer((ROOT / name).read_bytes(), np.int8).astype(np.float32) / 128
    n = int(len(a) * RATE / rate)
    return np.interp(np.arange(n) * rate / RATE, np.arange(len(a)), a).astype(np.float32)


def loop_to(a, n):
    return np.tile(a, n // len(a) + 1)[:n]


def make_audio(meta):
    n = len(meta)
    total = n * RATE // FPS
    subprocess.run([sys.executable, str(ROOT / "tools/trailer_audio.py"), str(WORK / "regs.bin"), str(WORK / "eng.wav")],
                   check=True, stdout=subprocess.DEVNULL)
    with wave.open(str(WORK / "eng.wav")) as w:
        eng = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32).reshape(-1, 2) / 32768
    eng = np.pad(eng, ((0, max(0, total - len(eng))), (0, 0)))[:total]

    fr = np.arange(total) * FPS // RATE                     # klatka obrazu dla probki
    scene = np.array([m["scene"] for m in meta])[np.minimum(fr, n - 1)]
    flags = np.array([m["flags"] for m in meta])[np.minimum(fr, n - 1)]

    def at(sc, f):                                          # probka: klatka f sceny sc
        idx = [i for i, m in enumerate(meta) if m["scene"] == sc]
        return idx[min(f, len(idx) - 1)] * RATE // FPS

    music = loop_to(load_s8("src/music/BlackMotor/black_motor_s8.raw", 15977), total)
    start = at(1, 150)                                      # muzyka wchodzi na zielonym
    mv = np.zeros(total, np.float32)
    mv[start:] = 0.62
    mv[(scene == 7) & (flags > 0)] = 0.25                   # zwolnione tempo: muzyka w tle
    mv[scene >= 8] = 0.8
    mv = np.convolve(mv, np.ones(2400) / 2400, mode="same")  # plynne zmiany (50 ms)
    music = np.roll(music, start) * mv
    music[:start] = 0

    crowd = loop_to(load_s8("data/crowd.raw", 11025), total)
    cv = np.where(scene <= 7, 0.35, 0.12).astype(np.float32)
    cv = np.convolve(cv, np.ones(9600) / 9600, mode="same")
    crowd *= cv

    fx = np.zeros(total, np.float32)

    def put(name, pos, vol, rate=11025):
        s = load_s8(name, rate) * vol
        e = min(total, pos + len(s))
        fx[pos:e] += s[:e - pos]

    put("data/buzzer.raw", at(1, 150), 0.8)
    put("data/cheer.raw", at(7, 60), 0.8)
    put("data/cheer.raw", at(8, 5), 0.5)

    mono = music + crowd + fx
    mix = eng * 0.9 + mono[:, None]
    fade = np.ones(total, np.float32)
    fade[-2 * RATE:] = np.linspace(1, 0, 2 * RATE)
    mix *= fade[:, None]
    mix /= max(1.0, np.abs(mix).max() / 0.95)
    with wave.open(str(WORK / "mix.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((mix * 32767).astype("<i2").tobytes())


def main():
    if "--reuse" not in sys.argv:                           # --reuse: klatki gry z poprzedniego razu
        render_game()
    meta = read_meta()
    print(f"mkcinema: {len(meta)} klatek ({len(meta) / FPS:.1f} s), kamera i napisy...")
    thumb = make_video(meta)
    make_audio(meta)
    OUT.mkdir(parents=True, exist_ok=True)
    mp4 = OUT / "trailer.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(WORK / "video.mp4"), "-i", str(WORK / "mix.wav"),
                    "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", str(mp4)],
                   check=True)
    if thumb is not None:
        genart.write_png(OUT / "trailer_thumb.png", thumb.astype(np.int32), 1)
    print(f"{mp4.relative_to(ROOT)} ({len(meta) / FPS:.1f} s), {(OUT / 'trailer_thumb.png').relative_to(ROOT)}")


if __name__ == "__main__":
    main()
