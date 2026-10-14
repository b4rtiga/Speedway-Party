"""genart.py - pikselart Speedway Party: tor ze stadionem, motocykle, sedzia.

Wyjscie (dla Amigi, ladowane z PROGDIR:data/ przed przejeciem sprzetu):
  data/track.pal    256 x RGB sucho + 256 x RGB mokro (1536 B)
  data/track.bpl    tlo 320x208, 8 bitplanow przeplatanych (66560 B)
  data/bikes.spr    4 kaski x 32 kierunki x 3 uslizgi x 2 polowki duszka 64x32 (417792 B)
  data/hud.pal      32 x RGB palety HUD (96 B)
  data/referee.bin  portret sedziego 32x38, 5 bitplanow przeplatanych (760 B)
Podglady PNG w art/.

Geometria toru MUSI byc zgodna z src/track.c (CX, CY, HALF_L, R_IN, R_OUT).
Uruchomienie: python tools/genart.py"""
import math
import pathlib
import struct
import sys
import zlib

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genfont  # noqa: E402

W, H = 320, 208
CX, CY = 160, 100
GATE_FIRST = 7
BIKE_SCALE = 0.78          # motocykl ~17x9 px: czterech miesci sie obok siebie na luku
VEH_SCALE = 0.85

# Tory - MUSZA byc zgodne z tabela track_defs w src/track.c (kolejnosc i liczby).
#   name, half_l (pol prostej), r_in (kraweznik), r_out (banda), nawierzchnia
PRESETS = [
    ("owal",    60, 40, 96, "bakalar"),   # klasyczny owal z bakalaru
]


def set_preset(i):
    """Ustawia geometrie toru i (globalnie) wszystko, co od niej zalezy."""
    global HALF_L, R_IN, R_OUT, R_MID, TAPE_X, GATE_X, GATE_STEP, GATE_Y, SURFACE
    _, HALF_L, R_IN, R_OUT, SURFACE = PRESETS[i]
    R_MID = (R_IN + R_OUT) // 2
    TAPE_X = CX                              # linia startu na srodku prostej (jak track.c)
    GATE_X = TAPE_X - 9
    GATE_STEP = (R_OUT - R_IN - 2 * GATE_FIRST) // 3
    GATE_Y = [CY + R_IN + GATE_FIRST + i * GATE_STEP for i in range(4)]


set_preset(0)
SPR_W, SPR_H, DIRS, SKIDS = 32, 32, 32, 3      # duszki 32 px (SPRITE_W w src/video.h)

rng = np.random.default_rng(1977)

# ============================== PALETA ===================================

pal = np.zeros((256, 3), dtype=np.int32)


def setc(i, r, g, b):
    pal[i] = (r, g, b)


def ramp(i0, n, c0, c1):
    for k in range(n):
        t = k / max(1, n - 1)
        setc(i0 + k, *(round(c0[j] + (c1[j] - c0[j]) * t) for j in range(3)))


setc(0, 8, 10, 20)
setc(1, 255, 255, 255)                      # tasma
# 16..47 nawierzchnia: 4 strefy (kraweznik ubity -> odsypisko luzne) x 8 odcieni
SURFACES = {
    #           strefy ciemne (4)                                  strefy jasne (4)                                       koleiny                   kamyczki
    "granit":  ([(96, 52, 30), (112, 62, 36), (128, 74, 42), (140, 84, 50)],
                [(176, 104, 62), (196, 122, 74), (214, 140, 88), (228, 160, 104)], ((70, 36, 22), (100, 56, 34)), ((210, 150, 100), (245, 200, 150))),
    "bakalar": ([(44, 46, 54), (52, 54, 62), (60, 62, 70), (68, 70, 78)],
                [(104, 106, 116), (116, 118, 128), (128, 130, 140), (142, 142, 152)], ((28, 30, 36), (46, 48, 56)), ((168, 168, 178), (210, 210, 220))),
    "piasek":  ([(150, 118, 68), (160, 126, 74), (170, 134, 80), (180, 144, 88)],
                [(220, 186, 126), (228, 196, 136), (236, 206, 146), (244, 218, 160)], ((118, 90, 50), (148, 116, 68)), ((250, 236, 190), (255, 248, 215))),
}


def track_colors(surface):
    dark, light, groove, stone = SURFACES[surface]
    for z in range(4):
        ramp(16 + z * 8, 8, dark[z], light[z])
    ramp(48, 4, *groove)                     # koleiny
    ramp(52, 4, *stone)                      # kamyczki / luzny material
    for k in range(3):                       # kaluze: na sucho = nawierzchnia
        setc(59 + k, *pal[16 + 3 + k])


track_colors("bakalar")
setc(56, 225, 225, 215)                      # malowane linie
setc(57, 210, 30, 30)                        # kraweznik czerwony
setc(58, 245, 245, 245)                      # kraweznik bialy
ramp(64, 8, (24, 70, 30), (60, 130, 50))     # trawa ciemna
ramp(72, 8, (40, 100, 40), (96, 170, 70))    # trawa jasna (pasy koszenia)
# 80..95 bandy reklamowe
for i, c in enumerate([(220, 30, 40), (250, 250, 250), (20, 80, 220), (255, 210, 0),
                       (20, 150, 70), (250, 120, 20), (120, 40, 160), (30, 30, 36),
                       (150, 20, 25), (190, 190, 200), (12, 50, 140), (190, 150, 0),
                       (12, 100, 45), (190, 80, 10), (80, 25, 110), (230, 230, 230)]):
    setc(80 + i, *c)
# 96..112 interfejs - TE SAME wartosci co track.c (100 nadpisuje Copper)
for i, c in {96: (18, 24, 64), 97: (36, 52, 128), 98: (130, 160, 255), 99: (255, 255, 255),
             100: (255, 255, 255), 101: (255, 200, 40), 102: (150, 150, 175), 103: (235, 25, 25),
             104: (30, 90, 255), 105: (240, 240, 240), 106: (255, 220, 0), 107: (170, 30, 110),
             108: (0, 0, 0), 109: (200, 200, 215), 110: (205, 125, 60), 111: (60, 220, 90),
             112: (255, 60, 50)}.items():
    setc(i, *c)
ramp(128, 8, (60, 62, 70), (200, 200, 205))  # beton
ramp(136, 4, (90, 95, 105), (170, 175, 185))  # plot
for k in range(32):                          # koszulki kibicow
    h = (k * 0.618) % 1.0
    s = 0.35 + 0.4 * ((k * 7) % 5) / 4
    v = 0.45 + 0.4 * ((k * 3) % 4) / 3
    i = int(h * 6); f = h * 6 - i
    p_, q_, t_ = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r, g, b = [(v, t_, p_), (q_, v, p_), (p_, v, t_), (p_, q_, v), (t_, p_, v), (v, p_, q_)][i % 6]
    setc(140 + k, int(r * 255), int(g * 255), int(b * 255))
for i, c in enumerate([(240, 190, 150), (200, 140, 100), (120, 80, 50), (60, 40, 30)]):
    setc(172 + i, *c)                        # glowy / wlosy
ramp(176, 4, (20, 50, 120), (50, 90, 180))   # puste krzeselka
setc(192, 107, 224, 255); setc(193, 40, 115, 150)   # baner DEV84: cyjan jasny / ciemny
ramp(196, 4, (70, 75, 85), (150, 155, 165))  # dachy
setc(200, 230, 30, 40); setc(201, 30, 90, 240); setc(202, 245, 245, 245); setc(203, 255, 210, 0)  # baner: 200 czerwony, 202 bialy
setc(204, 40, 110, 40); setc(205, 255, 120, 180)  # (dawne klomby)
ramp(208, 4, (20, 90, 30), (90, 190, 70))    # traktor
setc(212, 25, 25, 25); setc(213, 80, 80, 80); setc(214, 160, 200, 230); setc(215, 230, 230, 230)
ramp(216, 4, (110, 120, 135), (190, 200, 215))  # wieza sedziowska
setc(220, 60, 120, 170)                      # szyby
# 180..191, 221..239: podprowadzajace (ekran prezentacji) - kolory z tools/gengirls.py
from gengirls import palette as girls_palette   # noqa: E402
for i, c in girls_palette().items():
    setc(i, *c)
# 240..255 duszki (pary attached dziela te kolory)
for i, c in {241: (16, 16, 20), 242: (70, 72, 80), 243: (170, 175, 190), 244: (255, 255, 255),
             245: (52, 58, 80), 246: (112, 124, 160), 247: (250, 120, 20), 248: (150, 152, 162),
             249: (88, 90, 100), 250: (232, 30, 30), 251: (30, 95, 255), 252: (236, 236, 240),
             253: (255, 214, 0), 254: (60, 60, 72), 255: (225, 185, 70)}.items():
    setc(i, *c)

def make_wet():
    w = pal.copy()
    for i in range(16, 56):                  # nawierzchnia i drobiny; linie i kraweznik bez zmian
        r, g, b = pal[i]
        w[i] = (r * 0.52, g * 0.50, b * 0.52 + 10)
    for i in range(56, 59):
        w[i] = pal[i] * 0.88
    for k in range(3):                       # kaluze: ciemna woda z odbiciem nieba
        w[59 + k] = (48 + k * 14, 56 + k * 16, 82 + k * 22)
    return np.clip(w, 0, 255).astype(np.int32)


wet = make_wet()

# HUD 32 kolory (Copper laduje je w strefie HUD)
hud_pal = np.zeros((32, 3), dtype=np.int32)
for i, c in {0: (16, 24, 64), 1: (255, 255, 255), 2: (235, 25, 25), 3: (30, 90, 255), 4: (240, 240, 240),
             5: (255, 220, 0), 6: (255, 40, 30), 7: (40, 255, 60), 8: (70, 10, 10), 9: (10, 60, 15),
             10: (240, 190, 150), 11: (20, 20, 30), 12: (40, 60, 150), 13: (255, 255, 255), 14: (10, 10, 10),
             15: (120, 70, 30), 16: (120, 140, 200), 17: (200, 140, 105), 18: (18, 26, 80), 19: (225, 225, 230),
             20: (90, 55, 30), 21: (255, 40, 30), 22: (190, 60, 70), 23: (240, 150, 140), 24: (80, 110, 210),
             25: (150, 20, 15)}.items():
    hud_pal[i] = c

# ============================ GEOMETRIA ==================================


def spine(x, y):
    sx = np.clip(x, CX - HALF_L, CX + HALF_L)
    return sx, np.full_like(y, CY, dtype=np.float64)


def progress(x, y):
    """s wzdluz toru (jak track_progress w C), ciagle."""
    pi_r = math.pi * R_MID
    dx = x - CX
    s = np.zeros_like(x, dtype=np.float64)
    st = np.abs(dx) <= HALF_L
    bottom = st & (y >= CY)
    top = st & (y < CY)
    s[bottom] = dx[bottom] + HALF_L
    s[top] = 2 * HALF_L + pi_r + (HALF_L - dx[top])
    rb = dx > HALF_L
    a = np.arctan2(y[rb] - CY, x[rb] - (CX + HALF_L))          # 0 = wschod, +pi/2 = poludnie
    s[rb] = 2 * HALF_L + ((math.pi / 2 - a) % (2 * math.pi)).clip(0, math.pi) * R_MID
    lb = dx < -HALF_L
    a = np.arctan2(y[lb] - CY, x[lb] - (CX - HALF_L))
    s[lb] = 4 * HALF_L + pi_r + ((-math.pi / 2 - a) % (2 * math.pi)).clip(0, math.pi) * R_MID
    return s


def value_noise(a, b, scale_a, scale_b, seed):
    g = np.random.default_rng(seed).random((257, 257))
    fa, fb = a / scale_a, b / scale_b
    ia, ib = np.floor(fa).astype(int), np.floor(fb).astype(int)
    ta, tb = fa - ia, fb - ib
    ta, tb = ta * ta * (3 - 2 * ta), tb * tb * (3 - 2 * tb)
    ia %= 256; ib %= 256
    v00, v10 = g[ia, ib], g[ia + 1, ib]
    v01, v11 = g[ia, ib + 1], g[ia + 1, ib + 1]
    return (v00 * (1 - ta) + v10 * ta) * (1 - tb) + (v01 * (1 - ta) + v11 * ta) * tb


# ============================== TOR ======================================

def build_track(i):
    """Tlo toru nr i (geometria + nawierzchnia) -> globalne img, pal, wet."""
    global yy, xx, sx, sy, rr, nx, ny, ss, LAP, img, lat, fine, streak, blot, wet
    set_preset(i)
    track_colors(SURFACE)
    wet = make_wet()
    rng_t = np.random.default_rng(1977 + i)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    sx, sy = spine(xx, yy)
    rr = np.hypot(xx - sx, yy - sy)
    nx = np.where(rr > 0, (xx - sx) / np.maximum(rr, 1e-6), 0)
    ny = np.where(rr > 0, (yy - sy) / np.maximum(rr, 1e-6), 0)
    ss = progress(xx, yy)
    LAP = 4 * HALF_L + 2 * math.pi * R_MID



    img = np.zeros((H, W), dtype=np.int32)
    lat = rr - R_MID
    fine = rng_t.random((H, W))
    streak = value_noise(ss, lat + 40, 9.0, 1.3, 5)        # smugi wzdluz kierunku jazdy
    blot = value_noise(xx, yy, 14.0, 14.0, 9)

    # --- nawierzchnia ---
    track = (rr >= R_IN) & (rr < R_OUT)
    zone = np.clip(((rr - R_IN) * 4 / (R_OUT - R_IN)).astype(int), 0, 3)
    shade = 3.5 + (fine - 0.5) * 2.6 + (streak - 0.5) * 3.2 + (blot - 0.5) * 1.5
    groove = np.exp(-((lat + 20) / 3.0) ** 2) + 0.7 * np.exp(-((lat - 1) / 2.6) ** 2)  # linie jazdy
    shade -= groove * 2.2 * (0.6 + streak)
    light_side = -(nx * -0.707 + ny * -0.707)              # >0: banda od strony swiatla -> cien
    wall_shadow = (rr > R_OUT - 4) & (light_side < -0.25)
    shade = np.where(wall_shadow, shade - 3, shade)
    shade = np.clip(shade.round().astype(int), 0, 7)
    img[track] = (16 + zone * 8 + shade)[track]
    deep_groove = track & (groove * (0.6 + streak) > 1.25) & (fine < 0.5)
    img[deep_groove] = 48 + (fine[deep_groove] * 8).astype(int) % 4
    stones = track & (rr > R_OUT - 9) & (fine > 0.9) & ~wall_shadow
    img[stones] = 52 + ((fine[stones] - 0.9) * 40).astype(int).clip(0, 3)
    pn = value_noise(xx, yy, 3.2, 2.6, 21) + (fine - 0.5) * 0.12      # postrzepione brzegi
    puddle = track & (pn > 0.80) & (lat < -12) & (np.abs(xx - CX) > HALF_L - 20)
    img[puddle] = 59 + (fine[puddle] * 3).astype(int)

    # --- kraweznik ---
    kerb = (rr >= R_IN - 2) & (rr < R_IN)
    img[kerb] = np.where(((ss / 6).astype(int) % 2) == 0, 57, 58)[kerb]

    # --- murawa ---
    grass_in = rr < R_IN - 2
    stripe = ((xx + yy * 0.5) // 11).astype(int) % 2
    img[grass_in] = (64 + stripe * 8 + (2 + (fine - 0.5) * 3 + blot * 3).clip(0, 7).astype(int))[grass_in]


    stamp_text("SPEEDWAY", CX - text_w("SPEEDWAY") // 2, 76, 79)
    stamp_text("PARTY", CX - text_w("PARTY") // 2, 94, 79)
    stamp_banner('3dpjoy', CX - 48, 121)                   # banery po bokach, pod napisem
    stamp_banner('dev84', CX + 48, 121)

    tx_l = CX - HALF_L - R_IN // 2 - 6                      # traktory na murawie (kazdy tor)
    stamp(TRACTOR, tx_l, 96, TCOL)
    stamp([r[::-1] for r in TRACTOR], 2 * CX - tx_l - 12, 96, {**TCOL, 'G': 210})

    # --- banda, strefa bezpieczenstwa, plot ---
    wall = (rr >= R_OUT) & (rr < R_OUT + 3)
    seg = (ss / 22).astype(int)
    in_bend = np.abs(xx - CX) > HALF_L
    board_bg = 80 + (seg * 5) % 8
    board_fg = 88 + (seg * 5) % 8
    air = np.where((seg % 2) == 0, 80, 95)                 # dmuchane bandy na lukach
    wall_px = np.where(rr < R_OUT + 1, 88 + 7, np.where(((ss % 22) > 3) & ((ss % 22) < 18) & (rr < R_OUT + 2), board_fg, board_bg))
    img[wall] = np.where(in_bend, air, wall_px)[wall]
    safety = (rr >= R_OUT + 3) & (rr < R_OUT + 6)
    img[safety] = (64 + (fine * 3).astype(int))[safety]
    fence = (rr >= R_OUT + 6) & (rr < R_OUT + 8)
    img[fence] = np.where((ss.astype(int) % 8) == 0, 136, 138 + (rr[fence.nonzero()].mean() > 0))[fence] if False else \
        np.where((ss.astype(int) % 8) == 0, 136, 139)[fence]

    # --- trybuny: koncentryczne rzedy kibicow ---
    stand = rr >= R_OUT + 8
    k = (rr - (R_OUT + 8)).astype(int)
    row_m = k % 3
    aisle = ((ss / 3).astype(int) % 26) == 0
    # kibic = komorka 2 px wzdluz rzedu; sektory kibicuja "swoim" kolorem
    cell = (ss / 2).astype(np.int64)
    rown = k // 3
    hsh = (cell * 73856093 ^ rown * 19349663 ^ 0x5bd1e995) & 0xFFFFFF
    section = (ss / 46).astype(np.int64)
    team = 140 + (section * 11) % 32
    own = 140 + (hsh % 32)
    shirt = np.where((hsh >> 5) % 10 < 6, team, own)
    empty = (hsh >> 9) % 100 < 14
    head = np.where((hsh >> 12) % 4 == 3, 174, np.where((hsh >> 14) % 3 == 0, 175, 172 + (hsh >> 16) % 2))
    gap = (xx.astype(int) + yy.astype(int)) % 2 == 0          # przeswity miedzy glowami
    st = np.where(row_m == 0, 129 + rown % 2,
                  np.where(empty, 176 + rown % 2,
                           np.where(row_m == 1, shirt, np.where(gap & ((hsh >> 18) % 2 == 0), shirt, head))))
    st = np.where(aisle, 132, st)
    img[stand] = st[stand]

    # --- wieza sedziowska przy linii startu ---
    tx0, ty0 = TAPE_X - 16, 199
    img[ty0:ty0 + 10, tx0:tx0 + 32] = 216
    img[ty0, tx0:tx0 + 32] = 219
    img[ty0 + 2:ty0 + 5, tx0 + 2:tx0 + 30] = 220
    img[ty0 + 2:ty0 + 5, tx0 + 2:tx0 + 30:5] = 217

    # --- linia startu i pola startowe ---
    for y in range(CY + R_IN, CY + R_OUT):
        img[y, TAPE_X:TAPE_X + 2] = 56
    for i in range(5):
        yb = CY + R_IN + GATE_FIRST - GATE_STEP // 2 + i * GATE_STEP
        img[yb, TAPE_X - 14:TAPE_X] = 56

    return img


# Baner z logo 3DPJOY (zrodlo: src/banners/Logo_3DPJOY.svg - obraz 1024x1024 zbudowany
# z duzych "pikseli"): przerysowany recznie w wersji kompaktowej 23x15, litery 3x5 px.
# '#' czarny, 'R' czerwony przycisk, '.' biala tablica.
_LOGO_LETTERS = {
    '3': ["###", "..#", ".##", "..#", "###"], 'D': ["##.", "#.#", "#.#", "#.#", "##."],
    'P': ["###", "#.#", "###", "#..", "#.."], 'J': ["..#", "..#", "..#", "#.#", ".#."],
    'O': ["###", "#.#", "#.#", "#.#", "###"], 'Y': ["#.#", "#.#", ".#.", ".#.", ".#."],
}
BANNER_LOGO = [
    ".........###...........",
    ".........###...........",
    "..........#............",
    "..........#............",
    "........#####.RR.......",
    "......###########......",
    "......#.........#......",
    "......###########......",
    ".......#########.......",
    ".......................",
] + [".".join(_LOGO_LETTERS[c][r] for c in "3DPJOY") for r in range(5)]
# Drugi baner: logo DEV84 (zrodlo: src/banners/dev84-logo.svg - konturowe litery w kolorze
# cyjan), przerysowane recznie 34x9: '#' jasny kontur, 'c' ciemniejsza linia wewnetrzna, tlo czarne.
_DEV84_LETTERS = {
    'D': ["#####..", "#....#.", "#.cc..#", "#.c.c.#", "#.c.c.#", "#.c.c.#", "#.cc..#", "#....#.", "#####.."],
    'E': ["######", "#.....", "#.cccc", "#.c...", "#.ccc.", "#.c...", "#.cccc", "#.....", "######"],
    'V': ["#.....#", "#.c.c.#", "#.c.c.#", ".#.c.#.", ".#.c.#.", ".#.c.#.", "..#.#..", "..#.#..", "...#..."],
    '8': [".#####.", "#.....#", "#.ccc.#", "#.....#", ".#####.", "#.....#", "#.ccc.#", "#.....#", ".#####."],
    '4': ["....##.", "...#.#.", "..#c.#.", ".#c..#.", "#c...#.", "#######", ".....#.", ".....#.", ".....#."],
}
DEV84_LOGO = [".".join(_DEV84_LETTERS[c][r] for c in "DEV84") for r in range(9)]

# baner: logo, kolory znakow, tlo, margines (x, y); ramka 213 (ciemnoszara)
BANNERS = {
    '3dpjoy': (BANNER_LOGO, {'#': 212, 'R': 200}, 202, (2, 1)),
    'dev84': (DEV84_LOGO, {'#': 192, 'c': 193}, 108, (2, 2)),
}


def banner_size(name):
    logo, _, _, (px, py) = BANNERS[name]
    return len(logo[0]) + 2 * px + 2, len(logo) + 2 * py + 2


def stamp_banner(name, cx, cy):
    """tablica na murawie, wysrodkowana w (cx, cy): cien na trawie, ramka, tlo, logo"""
    logo, colors, bg, (px, py) = BANNERS[name]
    bw, bh = banner_size(name)
    x0, y0 = cx - bw // 2, cy - bh // 2
    for y in range(y0 + 1, y0 + bh + 1):                  # cien: najciemniejszy odcien pasa trawy
        for x in range(x0 + 1, x0 + bw + 1):
            if 64 <= img[y, x] < 80:
                img[y, x] = 64 + (img[y, x] - 64) // 8 * 8
    img[y0:y0 + bh, x0:x0 + bw] = 213                      # ramka
    img[y0 + 1:y0 + bh - 1, x0 + 1:x0 + bw - 1] = bg
    lx, ly = x0 + 1 + px, y0 + 1 + py
    for ry, row in enumerate(logo):
        for rx, c in enumerate(row):
            if c in colors:
                img[ly + ry, lx + rx] = colors[c]


def stamp_text(text, x0, y0, color, scale=2, spacing=6):
    x = x0
    for ch in text:
        rows = genfont.G.get(ch, genfont.G[' ']).split('|')
        for ry, row in enumerate(rows):
            for rx, c in enumerate(row):
                if c == '#':
                    img[y0 + ry * scale:y0 + ry * scale + scale, x + rx * scale:x + rx * scale + scale] = color
        x += spacing * scale


def text_w(text, scale=2, spacing=6):
    return len(text) * spacing * scale - scale


TRACTOR = ["..KKK.......",
           ".KGGGK.KKK..",
           "KGGLGGKGGGK.",
           "KGGGGGGGGGGK",
           "KGGLGGKGGGK.",
           ".KGGGK.KKK..",
           "..KKK......."]
TCOL = {'K': 212, 'G': 209, 'L': 214}


def stamp(rows, x0, y0, cmap):
    for ry, row in enumerate(rows):
        for rx, c in enumerate(row):
            if c in cmap:
                img[y0 + ry, x0 + rx] = cmap[c]



build_track(0)

# ============================ MOTOCYKLE ==================================
# kolory wzgledne duszka (0..15) -> paleta 240+n
T, TYRE, DGREY, SILVER, WHITE, KEV, KEV_L, TANK, DUST_L, DUST_D, HR, HB, HW, HY, HSHADE, GOLD = range(16)
HELMETS = [HR, HB, HW, HY]
HELMET_SLOT = 10       # w renderze kask ma kolor 10, potem podmiana na kolor pola

SS = 4                 # nadprobkowanie 4x4
cw = 32                # klatka motocykla 32x32 (caly duszek)


def render_bike(dir_, skid, canvas=None, scale=None, stripe=True):
    """Motocykl z gory; dir_ moze byc ulamkowy (0..32). canvas/scale: duzy render do ilustracji,
    stripe=False: bez pasa w kolorze kasku na plecach (w powiekszeniu wyglada jak kreska z kasku)."""
    n = canvas or cw
    th = dir_ * 2 * math.pi / DIRS
    c, s = math.cos(th), math.sin(th)
    o = (np.arange(n * SS) + 0.5) / SS - n / 2
    py, px = np.meshgrid(o, o, indexing='ij')
    u = (px * c + py * s) / (scale or BIKE_SCALE)
    v = (-px * s + py * c) / (scale or BIKE_SCALE)
    col = np.zeros(u.shape, dtype=np.int32)

    lean = [0.0, -1.0, -2.0][skid]            # pochylenie w lewo (do wnetrza luku)
    steer = [0.0, 0.35, 0.62][skid]           # kontra: przod skrecony w prawo

    def ell(cu, cv, ru, rv, uu=None, vv=None):
        uu = u if uu is None else uu
        vv = v if vv is None else vv
        return ((uu - cu) / ru) ** 2 + ((vv - cv) / rv) ** 2 <= 1

    def box(u0, u1, v0, v1, uu=None, vv=None):
        uu = u if uu is None else uu
        vv = v if vv is None else vv
        return (uu >= u0) & (uu <= u1) & (vv >= v0) & (vv <= v1)

    def seg(a, b, w, uu=None, vv=None):
        uu = u if uu is None else uu
        vv = v if vv is None else vv
        (u0, v0), (u1, v1) = a, b
        du, dv = u1 - u0, v1 - v0
        t = np.clip(((uu - u0) * du + (vv - v0) * dv) / (du * du + dv * dv), 0, 1)
        return np.hypot(uu - (u0 + t * du), vv - (v0 + t * dv)) <= w

    # wspolrzedne przodu obrocone wokol glowki ramy (u=4)
    cs, sn = math.cos(-steer), math.sin(-steer)
    fu = (u - 4) * cs - v * sn + 4
    fv = (u - 4) * sn + v * cs

    def world_light(du, dv):
        wx, wy = du * c - dv * s, du * s + dv * c
        n = np.hypot(wx, wy) + 1e-6
        return (wx * -0.707 + wy * -0.707) / n

    # kurz spod tylnego kola przy uslizgu: stozek na zewnatrz luku (+v)
    if skid:
        g = np.random.default_rng(int(dir_ * 7) + skid)
        for _ in range(14 + skid * 12):
            t = g.random()
            du = -11.5 - t * (2.5 + skid * 1.5)
            dv = 1.5 + t * (2 + skid * 1.5) + g.normal(0, 0.8)
            dv += g.normal(0, 0.9)
            r = 1.15 - t * 0.45
            col[ell(du, dv, r, r)] = DUST_L if g.random() < 0.6 else DUST_D

    # tylne kolo (szeroka opona) + pomaranczowy blotnik
    col[ell(-8, 0, 3.4, 1.5)] = TYRE
    col[ell(-8.6, 0, 2.4, 1.1)] = TANK
    col[ell(-8.6, 0.1, 1.4, 0.5)] = WHITE
    # tlumik (krotki, przy prawym boku)
    col[seg((-4, 1.8), (-8.5, 2.4), 0.6)] = SILVER
    # rama i silnik
    col[seg((-6, 0), (4.5, 0), 0.9)] = DGREY
    col[box(-3.5, 0.5, -1.3, 1.3)] = SILVER
    # przednie kolo, widelec, kierownica (skrecone o kontre)
    col[ell(8.4, 0, 2.9, 1.0, fu, fv)] = TYRE
    col[seg((4.5, 0), (7.5, 0), 0.5, fu, fv)] = SILVER
    col[seg((5.0, -4.6), (5.0, 4.6), 0.45, fu, fv)] = DGREY
    col[ell(5.0, -4.6, 0.7, 0.7, fu, fv) | ell(5.0, 4.6, 0.7, 0.7, fu, fv)] = TYRE
    # nogi: lewa wysunieta do przodu przy uslizgu (stalowy but), prawa na podnozku
    foot = [(-3.0, -3.4), (0.8, -5.6), (3.4, -6.4)][skid]
    col[seg((-3, -1.6 + lean * 0.5), foot, 0.95)] = KEV
    col[ell(foot[0] + 0.5, foot[1], 1.1, 0.8)] = SILVER
    col[seg((-3, 1.6 + lean * 0.5), (-2.4, 3.0), 0.9)] = KEV
    # tulow: jasny kevlar, cien po stronie odwrotnej do swiatla
    bu, bv = -2.4, lean
    body = ell(bu, bv, 3.3, 2.7)
    col[body] = np.where(world_light(u - bu, v - bv) > -0.1, KEV_L, KEV)[body]
    if stripe:
        col[body & (np.abs(v - bv) < 0.5)] = HELMET_SLOT     # pas w kolorze kasku na plecach
    # rece do kierownicy
    for side in (-1, 1):
        hand_v = side * 4.4
        col[seg((-1.2, bv + side * 2.2), (4.6 + side * steer * -1.0, hand_v), 0.75)] = KEV
    # kask: duzy, z blikiem
    hu, hv = 1.3, lean * 1.1
    helm = ell(hu, hv, 2.7, 2.6)
    lit = world_light(u - hu, v - hv)
    dist = np.hypot(u - hu, v - hv)
    hc = np.where(lit < -0.75, HSHADE, HELMET_SLOT)
    hc = np.where((lit > 0.55) & (dist > 0.8) & (dist < 1.9), WHITE, hc)
    col[helm] = hc[helm]

    return downsample(col, n)


def downsample(col, n=None):
    # redukcja 4x4 -> 1 px: wiekszosc, przezroczystosc gdy > 10/16
    n = n or cw
    blocks = col.reshape(n, SS, n, SS).transpose(0, 2, 1, 3).reshape(n, n, SS * SS)
    out = np.zeros((n, n), dtype=np.int32)
    for yv in range(n):
        for xv in range(n):
            b = blocks[yv, xv]
            cnt = np.bincount(b, minlength=16)
            if cnt[0] > 10:
                continue
            cnt[0] = 0
            # drobne wazne elementy (kask, but) wygrywaja przy remisie
            out[yv, xv] = int(np.argmax(cnt))
    # ciemny kontur 1 px wokol motocykla (bez kurzu) - czytelnosc na torze
    solid = (out != 0) & (out != DUST_L) & (out != DUST_D)
    edge = np.zeros_like(solid)
    edge[1:] |= solid[:-1]; edge[:-1] |= solid[1:]
    edge[:, 1:] |= solid[:, :-1]; edge[:, :-1] |= solid[:, 1:]
    out[(out == 0) & edge] = TYRE
    return out


def bike_frames():
    frames = {}
    for d in range(DIRS):
        for k in range(SKIDS):
            frames[(d, k)] = render_bike(d, k)
    return frames



# ============================ TRAKTORY ===================================
# te same kolory duszkow co motocykle (wspolna paleta 240..255)


def render_vehicle(kind, dir_):
    th = dir_ * 2 * math.pi / DIRS
    c, s = math.cos(th), math.sin(th)
    o = (np.arange(cw * SS) + 0.5) / SS - cw / 2
    py, px = np.meshgrid(o, o, indexing='ij')
    u = (px * c + py * s) / VEH_SCALE
    v = (-px * s + py * c) / VEH_SCALE
    col = np.zeros(u.shape, dtype=np.int32)

    def box(u0, u1, v0, v1):
        return (u >= u0) & (u <= u1) & (v >= v0) & (v <= v1)

    def ell(cu, cv, ru, rv):
        return ((u - cu) / ru) ** 2 + ((v - cv) / rv) ** 2 <= 1

    def lit(cu, cv):
        wx, wy = (u - cu) * c - (v - cv) * s, (u - cu) * s + (v - cv) * c
        return (wx * -0.707 + wy * -0.707) / (np.hypot(wx, wy) + 1e-6)

    if kind == 0:
        # czerwony traktor z rownarka (wloka) z tylu
        col[box(-13, -11.5, -8, 8)] = SILVER
        col[box(-13, -12.5, -8, 8) & (np.abs(v) % 3 < 1)] = DGREY
        col[(np.abs(np.abs(v) - 4 + (u + 11) * 0.5) < 0.5) & (u > -11.5) & (u < -5)] = DGREY  # lancuchy
        col[box(-6, -1.5, -5.6, -3.4) | box(-6, -1.5, 3.4, 5.6)] = TYRE                       # duze kola
        col[box(3, 6, -4.6, -3.2) | box(3, 6, 3.2, 4.6)] = TYRE                                # male kola
        body = box(-5, 7, -3.2, 3.2)
        col[body] = np.where(lit(1, 0) < -0.8, HSHADE, HR)[body]
        col[box(-4.5, -0.5, -2.6, 2.6)] = WHITE                                                # dach kabiny
        col[box(-4.5, -3.8, -2.6, 2.6)] = SILVER
        col[ell(5.5, -1.6, 0.6, 0.6)] = GOLD                                                   # rura wydechowa
    else:
        # zolty traktor + niebieska beczka z rampa zraszajaca
        col[box(-14.5, -13.5, -6.5, 6.5)] = SILVER
        col[box(-12, 2, -4.2, 4.2) & ell(-5, 0, 7.5, 4.2)] = TYRE
        tank = box(-12, 2, -3.8, 3.8)
        col[tank] = np.where((lit(-5, 0) > 0.6) & (np.abs(v) > 1.5), KEV_L, np.where(lit(-5, 0) < -0.8, KEV, HB))[tank]
        col[box(-12, 2, -0.4, 0.4)] = KEV_L
        col[box(-6, -3, -4.6, -4.0) | box(-6, -3, 4.0, 4.6)] = TYRE                           # kola beczki
        col[box(2, 4, -0.5, 0.5)] = DGREY                                                     # dyszel
        col[box(4, 7.5, -5.0, -3.4) | box(4, 7.5, 3.4, 5.0)] = TYRE
        col[box(11, 13.5, -4.2, -3.0) | box(11, 13.5, 3.0, 4.2)] = TYRE
        body = box(4, 14, -3.0, 3.0)
        col[body] = np.where(lit(9, 0) < -0.8, HSHADE, HY)[body]
        col[box(4.5, 8, -2.4, 2.4)] = WHITE
        col[ell(12.5, 1.4, 0.6, 0.6)] = GOLD
    return downsample(col)


def vehicle_blob():
    out = bytearray()
    wb = SPR_W // 8
    frames = {}
    for kind in range(2):
        for d in range(DIRS):
            f = render_vehicle(kind, d)
            frames[(kind, d)] = f
            full = np.zeros((SPR_H, SPR_W), dtype=np.int32)
            full[:, (SPR_W - cw) // 2:(SPR_W + cw) // 2] = f
            for half in range(2):
                out += bytes(2 * wb)
                for y in range(SPR_H):
                    for bit in (0, 1):
                        out += np.packbits(((full[y] >> (half * 2 + bit)) & 1).astype(np.uint8)).tobytes()
                out += bytes(2 * wb)
    return bytes(out), frames


# ============================ SEDZIA (HUD) ===============================

# Portret z ilustracji src/referee.png (tools/genreferee.py): 32x38 w palecie HUD.
from genreferee import image as referee_image, palette as referee_palette   # noqa: E402
for i, c in referee_palette().items():
    hud_pal[i] = c


def referee_img():
    return referee_image()



# ========================== PLANSZA TYTULOWA =============================
# 320x256, paleta toru. Indeks 0 = przezroczyste:
# tam widac gradient z Coppera (gora i dol planszy, pod napisami).

def make_title():
    set_preset(0)
    track_colors("bakalar")
    TH = 256
    rg = np.random.default_rng(2026)
    ty, tx = np.mgrid[0:TH, 0:W].astype(np.float64)
    ccx, ccy = -30.0, 345.0                       # srodek wielkiego luku (poza ekranem)
    r = np.hypot(tx - ccx, ty - ccy)
    ang = np.arctan2(ty - ccy, tx - ccx)
    k_in, k_out, wall = 236.0, 236.0 + 190.0, 236.0 + 196.0
    t = np.zeros((TH, W), dtype=np.int32)
    fine_t = rg.random((TH, W))
    along = ang * 330.0                           # wspolrzedna wzdluz luku
    streak_t = value_noise(along + 500, r, 11.0, 1.6, 7)
    blot_t = value_noise(tx, ty, 18.0, 18.0, 13)

    grass = r < k_in - 6
    stripe = ((tx * 0.7 - ty * 0.7) // 13).astype(int) % 2
    t[grass] = (64 + stripe * 8 + (2 + (fine_t - 0.5) * 3 + blot_t * 3).clip(0, 7).astype(int))[grass]
    kerb_t = (r >= k_in - 6) & (r < k_in)
    t[kerb_t] = np.where(((along / 14).astype(int) % 2) == 0, 57, 58)[kerb_t]
    surf = (r >= k_in) & (r < k_out)
    zone_t = np.clip(((r - k_in) * 4 / (k_out - k_in)).astype(int), 0, 3)
    sh = 3.5 + (fine_t - 0.5) * 2.4 + (streak_t - 0.5) * 3.4 + (blot_t - 0.5) * 1.4
    lat_t = r - (k_in + k_out) / 2
    groove_t = np.exp(-((lat_t + 60) / 9.0) ** 2) + 0.7 * np.exp(-((lat_t + 5) / 8.0) ** 2)
    sh -= groove_t * 2.4 * (0.6 + streak_t)
    sh = np.clip(sh.round().astype(int), 0, 7)
    t[surf] = (16 + zone_t * 8 + sh)[surf]
    stones_t = surf & (r > k_out - 30) & (fine_t > 0.88)
    t[stones_t] = 52 + ((fine_t[stones_t] - 0.88) * 33).astype(int).clip(0, 3)
    wall_t = (r >= k_out) & (r < wall)
    seg_t = (along / 40).astype(int)
    t[wall_t] = np.where(r[wall_t] < k_out + 2, 95, 80 + (seg_t[wall_t] * 5) % 8)
    stand_t = r >= wall
    kk = (r - wall).astype(int)
    hsh = ((along / 3).astype(np.int64) * 73856093 ^ (kk // 3) * 19349663) & 0xFFFFFF
    crowd = np.where(kk % 3 == 0, 129, np.where((hsh >> 9) % 100 < 12, 176, 140 + (hsh % 32)))
    crowd = np.where((kk % 3 == 2) & ((hsh >> 5) % 2 == 0), 172 + (hsh >> 7) % 4, crowd)
    t[stand_t] = crowd[stand_t]

    # zuzlowiec: na luku, jedzie w gore-lewo, rama zlamana o ~40 st. w luk
    bx, by = 172.0, 136.0
    rad = math.atan2(by - ccy, bx - ccx)
    course = math.atan2(-(bx - ccx), (by - ccy))   # styczna (jazda przeciwnie do wskazowek)
    frame = course - math.radians(38)
    big = 112
    bike = render_bike((frame % (2 * math.pi)) / (2 * math.pi) * DIRS, 2, canvas=big, scale=4.0, stripe=False)
    fdx, fdy = math.cos(frame), math.sin(frame)
    # cien pod motocyklem: przyciemnienie nawierzchni (o 3 odcienie w strefie)
    m = bike != 0
    ys, xs = np.nonzero(m)
    for yy_, xx_ in zip(ys, xs):
        X, Y = int(bx - big / 2 + xx_ + 6), int(by - big / 2 + yy_ + 6)
        if 0 <= X < W and 0 <= Y < TH and 16 <= t[Y, X] < 48:
            base = 16 + ((t[Y, X] - 16) // 8) * 8
            t[Y, X] = base + max(0, (t[Y, X] - base) - 4)

    # szpryca: wachlarz grudek spod tylnego kola - do tylu i na zewnatrz luku
    rear_x, rear_y = bx - fdx * 26, by - fdy * 26
    out_x, out_y = math.cos(rad), math.sin(rad)          # na zewnatrz luku
    back_x, back_y = -math.cos(course), -math.sin(course)
    for i in range(1500):
        d = rg.exponential(68.0)
        mix = rg.uniform(0.15, 1.0)
        vx = back_x * (1 - mix) + out_x * mix
        vy = back_y * (1 - mix) + out_y * mix
        nrm = math.hypot(vx, vy)
        px = rear_x + vx / nrm * d + rg.normal(0, 3 + d * 0.12)
        py = rear_y + vy / nrm * d + rg.normal(0, 3 + d * 0.12)
        size = max(1, int(4.5 - d / 30 + rg.normal(0, 0.8)))
        col = [240 + DUST_L, 240 + DUST_D, 49, 50, 52, 53, 18][rg.integers(0, 7)]
        if d > 110 and rg.random() < 0.6:
            col = 240 + DUST_L                        # daleko: rzadki kurz
        X0, Y0 = int(px), int(py)
        for yy_ in range(size):
            for xx_ in range(size):
                if (xx_ - size / 2 + 0.5) ** 2 + (yy_ - size / 2 + 0.5) ** 2 <= (size / 2) ** 2 + 0.3:
                    X, Y = X0 + xx_, Y0 + yy_
                    if 0 <= X < W and 0 <= Y < TH:
                        t[Y, X] = col

    # motocykl na wierzchu (kolory duszkow 240..255 sa tez w palecie pola gry);
    # kask caly czerwony: cien kasku = ciemna czerwien z palety band (88) zamiast szarosci
    bike = np.where(bike == HELMET_SLOT, HR, bike)
    for yy_, xx_ in zip(ys, xs):
        X, Y = int(bx - big / 2 + xx_), int(by - big / 2 + yy_)
        if 0 <= X < W and 0 <= Y < TH:
            v_ = bike[yy_, xx_]
            t[Y, X] = 88 if v_ == HSHADE else 240 + v_

    # gora i dol: miekkie przejscie w gradient Coppera (indeks 0) pod napisy
    for y in range(TH):
        if y < 98:                            # napisy tytulu: linie 0..74 czyste
            keep = max(0.0, (y - 76) / 22.0)
        elif y > 178:                         # napisy dolne: linie 200..255 czyste
            keep = max(0.0, (200 - y) / 22.0)
        else:
            continue
        mask = fine_t[y] >= keep
        t[y, mask] = 0
    return t


def title_preview(t):
    """podglad: indeks 0 jak gradient Coppera w menu"""
    rgb = pal[t].copy()
    for y in range(t.shape[0]):
        tt = int((y if y < 128 else 255 - y) * 13 / 16)
        c12 = ((tt // 26) * 17, (tt // 52) * 17, (2 + tt // 18) * 17)
        rgb[y][t[y] == 0] = c12
    return rgb


# ============================== ZAPIS ====================================


def to_planar(a, depth):
    """indeksy (h,w) -> bitplany przeplatane: dla kazdego wiersza plan 0..depth-1"""
    h, w = a.shape
    out = bytearray()
    for y in range(h):
        for p in range(depth):
            out += np.packbits(((a[y] >> p) & 1).astype(np.uint8)).tobytes()
    return bytes(out)


def sprite_blob(frames):
    out = bytearray()
    wb = SPR_W // 8
    for gate in range(4):
        for d in range(DIRS):
            for k in range(SKIDS):
                f = frames[(d, k)].copy()
                f[f == HELMET_SLOT] = HELMETS[gate]
                full = np.zeros((SPR_H, SPR_W), dtype=np.int32)
                full[:, (SPR_W - cw) // 2:(SPR_W + cw) // 2] = f
                for half in range(2):
                    out += bytes(2 * wb)                          # POS/CTL - wypelnia C
                    for y in range(SPR_H):
                        for bit in (0, 1):
                            b = (full[y] >> (half * 2 + bit)) & 1
                            out += np.packbits(b.astype(np.uint8)).tobytes()
                    out += bytes(2 * wb)                          # koniec duszka
    return bytes(out)


def write_png(path, rgb, scale=1):
    rgb = np.repeat(np.repeat(rgb, scale, 0), scale, 1).astype(np.uint8)
    h, w, _ = rgb.shape
    raw = b''.join(b'\x00' + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    path.write_bytes(png)


def compose_screen(palette, frames, bikes):
    """makieta ekranu 320x256: tor + motocykle + HUD (jak na Amidze)"""
    scr = np.zeros((256, 320, 3), dtype=np.int32)
    scr[:H] = palette[img]
    for (bx, by, d, k, gate) in bikes:
        f = frames[(d, k)].copy()
        f[f == HELMET_SLOT] = HELMETS[gate]
        for y in range(cw):
            for x in range(cw):
                if f[y, x]:
                    X, Y = bx - cw // 2 + x, by - cw // 2 + y
                    if 0 <= X < 320 and 0 <= Y < H:
                        scr[Y, X] = palette[240 + f[y, x]]
    # HUD: gradient Coppera + elementy
    for y in range(48):
        t = max(0, y - 4) * 15 // 44
        scr[H + y] = (17 * 1, 17 * (1 + t // 3), 17 * (4 + t // 2)) if y >= 4 else (0, 0, 0)
    hud = scr[H:]
    for i, g in enumerate([1, 0, 3, 2]):
        hud[14:32, 8 + i * 22:26 + i * 22] = hud_pal[16]
        hud[16:30, 10 + i * 22:24 + i * 22] = hud_pal[2 + g]
    for gx, on in ((172, 6), (196, 9)):
        hud[14:32, gx:gx + 18] = hud_pal[on]
    ref = referee_img()
    for y in range(38):
        for x in range(32):
            if ref[y, x]:
                hud[6 + y, 272 + x] = hud_pal[ref[y, x]]
    return scr


def main():
    (ROOT / "data").mkdir(exist_ok=True)
    (ROOT / "art").mkdir(exist_ok=True)
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)   # podglady robocze (poza repo)

    # tory: data/trackN.pal (sucho + mokro) i data/trackN.bpl, podglady art/track_<nazwa>.png
    for i, (name, *_rest) in enumerate(PRESETS):
        build_track(i)
        (ROOT / f"data/track{i}.pal").write_bytes(bytes(pal.astype(np.uint8).ravel()) + bytes(wet.astype(np.uint8).ravel()))
        (ROOT / f"data/track{i}.bpl").write_bytes(to_planar(img, 8))
        write_png(ROOT / f"build/preview/track_{name}.png", pal[img], 2)
        write_png(ROOT / f"build/preview/track_{name}_mokry.png", wet[img], 2)
    build_track(0)                           # reszta podgladow na torze 0
    # plansza tytulowa: tools/gentitle.py (ilustracja src/title.png, wlasna paleta)
    (ROOT / "data/hud.pal").write_bytes(bytes(hud_pal.astype(np.uint8).ravel()))
    (ROOT / "data/referee.bin").write_bytes(to_planar(referee_img(), 5))

    print("motocykle: renderowanie 96 klatek...")
    frames = bike_frames()
    blob = sprite_blob(frames)
    assert len(blob) == 4 * DIRS * SKIDS * 2 * (SPR_H + 2) * (SPR_W // 4), len(blob)
    (ROOT / "data/bikes.spr").write_bytes(blob)

    vblob, vframes = vehicle_blob()
    assert len(vblob) == 2 * DIRS * 2 * (SPR_H + 2) * (SPR_W // 4), len(vblob)
    (ROOT / "data/tractors.spr").write_bytes(vblob)
    vs = np.zeros((2 * 34, 16 * 34, 3), dtype=np.int32) + pal[20]
    for kind in range(2):
        for j in range(16):
            f = vframes[(kind, j * 2)]
            cell = vs[kind * 34 + 1:kind * 34 + 33, j * 34 + 1:j * 34 + 33]
            m = f != 0
            cell[m] = pal[240 + f[m]]
    write_png(ROOT / "build/preview/tractors_sheet.png", vs, 3)


    # arkusz klatek: 32 kierunki x (3 uslizgi) na tle toru, + rzad kaskow
    sheet = np.zeros(((SKIDS + 1) * 34, DIRS * 34, 3), dtype=np.int32) + pal[20]
    for k in range(SKIDS):
        for d in range(DIRS):
            f = frames[(d, k)]
            cell = sheet[k * 34 + 1:k * 34 + 33, d * 34 + 1:d * 34 + 33]
            m = f != 0
            cell[m] = pal[240 + f[m]]
    for g in range(4):
        for j in range(8):
            d = (j * 4 + g) % DIRS
            f = frames[(d, 2 if j % 2 else 0)].copy()
            f[f == HELMET_SLOT] = HELMETS[g]
            cell = sheet[SKIDS * 34 + 1:SKIDS * 34 + 33, (g * 8 + j) * 34 + 1:(g * 8 + j) * 34 + 33]
            m = f != 0
            cell[m] = pal[240 + f[m]]
    write_png(ROOT / "build/preview/bikes_sheet.png", sheet, 3)

    ref = referee_img()
    write_png(ROOT / "build/preview/referee.png", hud_pal[ref], 6)

    bikes = [(GATE_X, GATE_Y[g], 0, 0, g) for g in range(2)] + \
            [(272, 120, 24, 2, 2), (262, 140, 26, 1, 3)]
    write_png(ROOT / "build/preview/mockup_race.png", compose_screen(pal, frames, bikes), 3)
    write_png(ROOT / "build/preview/mockup_wet.png", compose_screen(wet, frames, bikes), 3)
    print("gotowe: data/*.{pal,bpl,spr,bin}, podglady: build/preview/")


if __name__ == "__main__":
    main()
