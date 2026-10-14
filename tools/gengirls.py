"""gengirls.py - podprowadzajace na ekran prezentacji przed biegiem: data/girls.bin.

Cztery postacie 48x96 (rece w gorze - trzymaja tablice w kolorze pola, rysowana w grze),
kazda z inna glowa: dlugie blond wlosy, czarny bob z grzywka, rudy kucyk, brazowy kok
z opaska. Cieniowanie: swiatlo z lewej gory, kontur 1 px. Akcent stroju w kolorze pola
(kolory UI 103..106, dla bialego pola szary 102).

Kolory postaci leza w palecie toru (data/track0.pal, tools/genart.py) w indeksach
180..191 i 221..239 - tools/gentitle.py zostawia je w palecie planszy tytulowej.
Plik: 4 postacie po 96 wierszy x 8 planow x 6 bajtow (planarnie, jak bitplany gry).
Podglad: build/preview/girls.png. Bez zaleznosci poza numpy."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mksocial import write_png    # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
W, H = 48, 96

# indeksy palety (wartosci RGB ustawia genart.py - PALETA_POSTACI)
OUT = 241                                         # kontur (kolor duszkow, wspolny)
WHITE = 244
DRESS_SH, DRESS_DEEP = 237, 238
IRIS = 239
BLUSH = 183
LIPS, LIPS_D = 224, 225
SKIN = [(221, 222, 223), (180, 181, 182)]         # jasna, opalona: swiatlo / srodek / cien
HAIR = {
    'blond': (226, 227, 228),
    'black': (229, 230, 241),
    'red':   (231, 232, 233),
    'brown': (234, 235, 236),
}
GATE = [103, 104, 102, 106]                       # akcent: czerwony, niebieski, (bialy->szary), zolty

# RGB do palety toru (importuje genart.py)
PALETTE = {
    221: (250, 208, 176), 222: (228, 172, 138), 223: (186, 124, 98),
    180: (214, 158, 112), 181: (178, 122, 82), 182: (128, 84, 56),
    224: (214, 74, 96), 225: (150, 40, 62),
    226: (255, 232, 150), 227: (228, 182, 84), 228: (160, 116, 44),
    229: (96, 96, 118), 230: (46, 46, 58),
    231: (252, 142, 72), 232: (212, 82, 36), 233: (140, 46, 26),
    234: (162, 108, 62), 235: (112, 68, 36), 236: (72, 42, 26),
    237: (212, 216, 230), 238: (160, 166, 188), 239: (64, 116, 168),
    183: (236, 160, 140),
}


def ellipse(cx, cy, rx, ry):
    yy, xx = np.mgrid[0:H, 0:W]
    return ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1.0


def shade3(mask, cx, cy, rx, ry, tones):
    """trzy tony na ksztalcie: swiatlo z lewej gory"""
    yy, xx = np.mgrid[0:H, 0:W]
    l = (-(xx - cx) / rx * 0.6 - (yy - cy) / ry * 0.8)
    out = np.where(l > 0.35, tones[0], np.where(l > -0.45, tones[1], tones[2]))
    return np.where(mask, out, 0)


def thick_line(img, x0, y0, x1, y1, w, tones):
    """reka: gruby odcinek, jasniejsza krawedz od lewej"""
    n = int(max(abs(x1 - x0), abs(y1 - y0))) * 2 + 1
    for t in np.linspace(0, 1, n):
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        for dx in range(-w, w + 1):
            for dy in range(-w, w + 1):
                if dx * dx + dy * dy <= w * w + 1:
                    X, Y = int(round(x + dx)), int(round(y + dy))
                    if 0 <= X < W and 0 <= Y < H:
                        img[Y, X] = tones[0] if dx < -w // 2 else tones[2] if dx > w // 2 else tones[1]


def girl(hair_style, skin, gate):
    s0, s1, s2 = SKIN[skin]
    h0, h1, h2 = HAIR[hair_style]
    acc = GATE[gate]
    img = np.zeros((H, W), np.int32)
    yy, xx = np.mgrid[0:H, 0:W]

    # wlosy z tylu (dlugie / kucyk) - pod glowa
    if hair_style == 'blond':
        img[shade3(ellipse(24, 30, 12, 17), 24, 26, 12, 17, (h0, h1, h2)) > 0] = 0
        m = ellipse(24, 30, 12, 17) & (yy > 18)
        img[m] = np.where(((xx[m] + yy[m] // 3) % 5) == 0, h2, np.where(xx[m] < 24, h0, h1))
    if hair_style == 'red':                         # kucyk w prawo w dol
        for k in range(14):
            cx, cy = 33 + k * 0.6, 14 + k * 1.6
            m = ellipse(cx, cy, 3.2 - k * 0.12, 3)
            img[m] = h1 if k % 3 else h2

    # rece w gorze: dlonie przy gornej krawedzi (trzymaja tablice)
    thick_line(img, 7, 3, 15, 36, 2, (s0, s1, s2))
    thick_line(img, 41, 3, 33, 36, 2, (s0, s1, s2))
    for hx in (7, 41):                               # dlonie
        img[ellipse(hx, 3, 3.2, 3)] = s1
        img[ellipse(hx - 1, 2, 1.5, 1.5)] = s0

    # szyja i glowa
    img[(xx >= 21) & (xx <= 26) & (yy >= 30) & (yy <= 37)] = s1
    img[(xx >= 25) & (xx <= 26) & (yy >= 30) & (yy <= 37)] = s2
    head = ellipse(24, 22, 8.5, 10.5)
    img[head] = shade3(head, 24, 22, 8.5, 10.5, (s0, s1, s2))[head]

    # strój: gora (dopasowana), pasek, rozkloszowana spodniczka - cien po prawej i faldy
    for y in range(36, 76):
        if y < 58:
            half = 7 + (y - 36) * 0.12
        else:
            half = 9.6 + (y - 58) * 0.75
        for x in range(int(24 - half), int(24 + half) + 1):
            rel = (x - (24 - half)) / (2 * half)
            c = WHITE if rel < 0.55 else DRESS_SH if rel < 0.82 else DRESS_DEEP
            if y >= 60 and (x - 24) % 5 == 0 and rel > 0.2:   # faldy spodniczki
                c = DRESS_SH if c == WHITE else DRESS_DEEP
            img[y, x] = c
    for k, y in enumerate(range(37, 42)):             # dekolt w szpic
        img[y, 21 + k // 2 + (k > 2):27 - k // 2 - (k > 2)] = s1
    for y in range(55, 58):                           # pasek w kolorze pola
        half = 7 + (y - 36) * 0.12
        img[y, int(24 - half):int(24 + half) + 1] = acc
    for y in range(72, 76):                           # lamowka spodniczki
        half = 9.6 + (y - 58) * 0.75
        img[y, int(24 - half):int(24 + half) + 1] = acc

    # nogi i buty
    for lx in (19, 27):
        img[76:90, lx:lx + 4] = s1
        img[76:90, lx:lx + 1] = s0
        img[76:90, lx + 3:lx + 4] = s2
        img[89:94, lx - 1:lx + 5] = OUT
        img[90, lx:lx + 2] = 242                      # blysk na bucie

    # twarz: brwi, oczy z teczowka, nos, usta, rumience
    for ex in (20, 27):
        img[20, ex:ex + 3] = h2 if hair_style != 'blond' else h1   # brwi
        img[22, ex:ex + 3] = WHITE
        img[22, ex + 1] = IRIS
        img[23, ex:ex + 3] = OUT                      # rzesy / dolna linia oka
        img[23, ex + 1] = IRIS
    img[25, 24] = s2                                  # nos
    img[26, 23] = s2
    img[28, 22:27] = LIPS
    img[29, 23:26] = LIPS_D
    img[26, 19:21] = BLUSH                            # delikatne rumience
    img[26, 28:30] = BLUSH

    # wlosy z przodu - rozne fryzury
    top = ellipse(24, 17, 9.5, 8) & (yy <= 19)
    if hair_style == 'blond':                         # dlugie, przedzialek
        img[top] = np.where(xx[top] < 24, h0, h1)
        img[(yy >= 14) & (yy <= 34) & (((xx >= 14) & (xx <= 16)) | ((xx >= 32) & (xx <= 34)))] = h1
        img[13:20, 24] = h2
    elif hair_style == 'black':                       # bob z prosta grzywka
        img[top] = np.where(xx[top] < 22, h0, h1)
        img[16:20, 16:33] = h1
        img[19, 16:33:2] = h0
        for side in ((14, 17), (31, 34)):
            img[14:31, side[0]:side[1]] = h1
            img[30, side[0]:side[1]] = h2
    elif hair_style == 'red':                         # gladko zaczesane, kucyk w gore
        img[top] = np.where(xx[top] < 24, h0, h1)
        img[12:15, 30:34] = acc                       # gumka w kolorze pola
    else:                                             # brazowy kok na czubku + opaska
        img[top] = np.where(xx[top] < 24, h0, h1)
        bun = ellipse(24, 8, 5.5, 4.5)
        img[bun] = shade3(bun, 24, 8, 5.5, 4.5, (h0, h1, h2))[bun]
        img[13:15, 16:33] = acc                       # opaska
        img[15:24, 15:17] = h1                        # kosmyki przy skroniach
        img[15:24, 32:34] = h1

    # kontur 1 px dookola postaci
    filled = img > 0
    pad = np.pad(filled, 1)                           # bez zawijania krawedzi
    ring = pad[:-2, 1:-1] | pad[2:, 1:-1] | pad[1:-1, :-2] | pad[1:-1, 2:]
    img[ring & ~filled] = OUT
    img[0, :] = np.where(img[0, :] == OUT, 0, img[0, :])   # dlonie dotykaja tablicy - bez konturu u gory
    return img


def to_planar(a, depth=8):
    out = bytearray()
    for y in range(a.shape[0]):
        for p in range(depth):
            out += np.packbits(((a[y] >> p) & 1).astype(np.uint8)).tobytes()
    return bytes(out)


GIRLS = [('blond', 0), ('black', 1), ('red', 0), ('brown', 1)]   # fryzura, skora - pole A..D


def main():
    imgs = [girl(h, s, g) for g, (h, s) in enumerate(GIRLS)]
    (ROOT / "data/girls.bin").write_bytes(b"".join(to_planar(i) for i in imgs))
    # podglad na tle
    pal = np.zeros((256, 3))
    pal[0] = (30, 30, 70)
    pal[OUT] = (16, 16, 20); pal[WHITE] = (255, 255, 255); pal[242] = (70, 72, 80)
    pal[102] = (150, 150, 175); pal[103] = (235, 25, 25); pal[104] = (30, 90, 255); pal[106] = (255, 220, 0)
    for k, v in PALETTE.items():
        pal[k] = v
    row = np.concatenate([np.pad(i, ((4, 4), (4, 4))) for i in imgs], 1)
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
    write_png(ROOT / "build/preview/girls.png", np.repeat(np.repeat(pal[row], 4, 0), 4, 1))
    print("data/girls.bin, build/preview/girls.png")


if __name__ == "__main__":
    main()
