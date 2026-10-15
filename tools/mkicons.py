"""mkicons.py - ikony Workbencha (.info, klasyczny format OS 2.x/3.x, 4 kolory palety WB).

Ikony:
  tool(dir)        SpeedwayParty.info obok programu (narzedzie, podwojne klikniecie = gra)
  drawer(dir)      SpeedwayParty.info obok katalogu gry (szuflada)
  disk(dir)        Disk.info w katalogu glownym dyskietki
  readme(dir)      CZYTAJ.info obok CZYTAJ.TXT (MultiView)
  whdload(dir)     SpeedwayParty.info dla WHDLoad (projekt: domyslne narzedzie WHDLoad,
                   ToolTypes SLAVE=SpeedwayParty.slave, PRELOAD)
Obraz: owal toru z gory (niebieska murawa, bialy kraweznik, linia startu, motocykle).
Kolory WB: 0 szary (tlo), 1 czarny, 2 bialy, 3 niebieski. Zaznaczenie: odwrocenie kolorow
(GADGHCOMP), wiec wystarczy jeden obraz. Workbench w hires ma piksele 1:2 (wysokie), wiec
obraz jest rysowany 2x szerszy niz wysoki - podglad PNG (build/preview) w tych proporcjach.

Uzycie: python3 tools/mkicons.py <rodzaj> <katalog>   (rodzaj: tool drawer disk readme whdload)
        python3 tools/mkicons.py preview                (build/preview/icon_*.png)"""
import math
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WB_PALETTE = [(149, 149, 149), (0, 0, 0), (255, 255, 255), (59, 103, 162)]   # Workbench 3.x
NO_ICON_POSITION = 0x80000000
WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4


# ------------------------------------------------------------------ obrazy
def oval_image(w=64, h=24, bikes=True):
    """owal toru z gory: piksel = kolor 0..3; x skalowany 0.5 (piksele hires 1:2)"""
    img = [[0] * w for _ in range(h)]
    cx, cy = (w - 1) / 2, (h - 1) / 2
    r_out = (h - 1) / 2 - 0.5                    # banda dotyka gornej i dolnej krawedzi
    half = (w - 1) / 4 - r_out - 0.5             # prosta - owal miesci sie w szerokosci
    r_in = r_out * 0.52
    for y in range(h):
        for x in range(w):
            dx, dy = (x - cx) / 2.0, y - cy
            ax = abs(dx)
            r = abs(dy) if ax <= half else math.hypot(ax - half, dy)
            if r >= r_out + 1:
                continue                         # tlo (przezroczyste na Workbenchu)
            if r >= r_out:
                img[y][x] = 1                    # banda
            elif r >= r_in + 1:
                img[y][x] = 0                    # nawierzchnia (szara)
            elif r >= r_in:
                img[y][x] = 2 if (int(x / 3) % 2 == 0) else 1   # kraweznik bialo-czarny
            else:
                img[y][x] = 3                    # murawa
    if bikes:
        # linia startu na dolnej prostej i trzy motocykle (czarny motocykl, bialy kask)
        sx = int(cx + 6)
        for y in range(h):
            if r_in + 1 <= y - cy < r_out:
                img[y][sx] = 2
        lane = (r_in + r_out) / 2
        for bx, by in ((sx - 5, int(round(cy + lane - 1))), (sx - 12, int(round(cy + lane + 1))),
                       (int(cx - 4), int(round(cy - lane)))):
            for k in range(-3, 4):
                img[by][bx + k] = 1
            img[by - 1][bx] = 1
            img[by - 1][bx + 1] = 2
            img[by][bx + 1] = 2
    return img


def disk_image():
    """dyskietka 3.5": niebieska obudowa, metalowa przeslona, etykieta z malym owalem"""
    w, h = 44, 24
    img = [[3] * w for _ in range(h)]
    for x in range(w):
        img[0][x] = img[h - 1][x] = 1
    for y in range(h):
        img[y][0] = img[y][w - 1] = 1
    for y in range(1, 3):                         # sciety rog
        for x in range(w - 1 - (3 - y) * 2, w):
            img[y][x] = 0 if y == 1 and x >= w - 3 else img[y][x]
    for y in range(1, 9):                         # przeslona
        for x in range(12, 32):
            img[y][x] = 0 if not (y in (1, 8) or x in (12, 31)) else 1
        for x in range(25, 29):
            if 2 <= y <= 7:
                img[y][x] = 3
    for y in range(11, 22):                       # etykieta
        for x in range(6, 38):
            img[y][x] = 2 if not (y in (11, 21) or x in (6, 37)) else 1
    small = oval_image(28, 9, bikes=False)        # owal na etykiecie (tlo etykiety biale)
    for y in range(9):
        for x in range(28):
            img[12 + y][8 + x] = small[y][x] if small[y][x] else 2
    return img


def text_image():
    """kartka z tekstem (CZYTAJ.TXT)"""
    w, h = 30, 22
    img = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(4, 26):
            edge = y in (0, h - 1) or x in (4, 25)
            img[y][x] = 1 if edge else 2
    for y in range(3, h - 2, 3):
        for x in range(7, 23 if y % 2 else 19):
            img[y][x] = 1
    return img


def planes(img, depth=2):
    """obraz -> plany bitowe (kazdy wiersz wyrownany do slowa)"""
    h, w = len(img), len(img[0])
    row_bytes = (w + 15) // 16 * 2
    out = bytearray()
    for p in range(depth):
        for y in range(h):
            row = bytearray(row_bytes)
            for x in range(w):
                if (img[y][x] >> p) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += row
    return bytes(out)


# ------------------------------------------------------------------ format .info
def _string(s):
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def make_icon(img, kind, default_tool=None, tooltypes=None, stack=16384, drawer_box=(50, 50, 400, 120)):
    w, h = len(img[0]), len(img)
    has_drawer = kind in (WBDISK, WBDRAWER)
    gadget = struct.pack(">IhhhhHHHIIIiIHI",
                         0, 0, 0, w, h + 1,                  # Next, Left, Top, Width, Height (+1: WB rysuje podpis nizej)
                         0x0004,                             # Flags: GFLG_GADGIMAGE, zaznaczenie = GADGHCOMP
                         0x0003,                             # Activation: RELVERIFY | IMMEDIATE
                         0x0001,                             # GadgetType: BOOLGADGET
                         1, 0, 0, 0, 0, 0,                   # GadgetRender (jest obraz), SelectRender, Text, MutEx, SpecialInfo, ID
                         1)                                  # UserData: WB_DISKREVISION 1 (OS 2.x+)
    disk_object = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi", kind, 0,
        1 if default_tool else 0, 1 if tooltypes else 0,
        NO_ICON_POSITION - (1 << 32), NO_ICON_POSITION - (1 << 32),
        1 if has_drawer else 0, 0, stack)
    assert len(disk_object) == 78, len(disk_object)
    out = bytearray(disk_object)
    if has_drawer:
        x, y, dw, dh = drawer_box
        new_window = struct.pack(">hhhhBBIIIIIIIhhHHH", x, y, dw, dh, 255, 255, 0, 0, 0, 0, 0, 0, 0,
                                 90, 40, 0xFFFF, 0xFFFF, 1)  # Min/Max, Type WBENCHSCREEN
        assert len(new_window) == 48
        out += new_window + struct.pack(">ii", 0, 0)        # dd_CurrentX/Y
    out += struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0)   # Image: depth 2, PlanePick 3
    out += planes(img)
    if default_tool:
        out += _string(default_tool)
    if tooltypes:
        out += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            out += _string(t)
    if has_drawer:
        out += struct.pack(">IH", 0, 0)                      # OS 2.x: dd_Flags (domyslnie), dd_ViewModes
    return bytes(out)


def parse_icon(data):
    """czytnik kontrolny: (typ, szer., wys., obraz, narzedzie, tooltypes) - wyjatek przy bledzie"""
    magic, ver = struct.unpack_from(">HH", data, 0)
    assert magic == 0xE310 and ver == 1, "naglowek"
    w, h = struct.unpack_from(">hh", data, 4 + 8)
    flags, = struct.unpack_from(">H", data, 4 + 12)
    render, select = struct.unpack_from(">II", data, 4 + 18)
    kind, = struct.unpack_from(">B", data, 48)
    tool_p, tt_p, _, _, drawer_p, _, stack = struct.unpack_from(">IIiiIIi", data, 50)
    pos = 78
    if drawer_p:
        pos += 56
    assert render, "brak obrazu"
    imgs = []
    for _ in range(2 if select else 1):
        _, _, iw, ih, depth, data_p, pick, onoff, nxt = struct.unpack_from(">hhhhhIBBI", data, pos)
        pos += 20
        rb = (iw + 15) // 16 * 2
        size = rb * ih * depth
        raw = data[pos:pos + size]
        assert len(raw) == size, "obraz uciety"
        pos += size
        img = [[0] * iw for _ in range(ih)]
        for p in range(depth):
            for y in range(ih):
                for x in range(iw):
                    if raw[(p * ih + y) * rb + (x >> 3)] & (0x80 >> (x & 7)):
                        img[y][x] |= 1 << p
        imgs.append(img)
    tool = None
    if tool_p:
        n, = struct.unpack_from(">I", data, pos)
        tool = data[pos + 4:pos + 4 + n - 1].decode("latin-1")
        pos += 4 + n
    tts = []
    if tt_p:
        cnt, = struct.unpack_from(">I", data, pos)
        pos += 4
        for _ in range(cnt // 4 - 1):
            n, = struct.unpack_from(">I", data, pos)
            tts.append(data[pos + 4:pos + 4 + n - 1].decode("latin-1"))
            pos += 4 + n
    if drawer_p:
        pos += 6
    assert pos == len(data), f"nadmiarowe bajty: {len(data) - pos}"
    return kind, imgs[0], tool, tts, stack


ICONS = {
    "tool":    ("SpeedwayParty.info", lambda: make_icon(oval_image(), WBTOOL, stack=16384)),
    "drawer":  ("SpeedwayParty.info", lambda: make_icon(oval_image(), WBDRAWER, drawer_box=(60, 40, 360, 110))),
    "disk":    ("Disk.info", lambda: make_icon(disk_image(), WBDISK, default_tool="SYS:System/DiskCopy",
                                               drawer_box=(60, 40, 360, 110))),
    "readme":  ("CZYTAJ.TXT.info", lambda: make_icon(text_image(), WBPROJECT, default_tool="SYS:Utilities/MultiView")),
    "whdload": ("SpeedwayParty.info", lambda: make_icon(oval_image(), WBPROJECT, default_tool="WHDLoad",
                                                        tooltypes=["SLAVE=SpeedwayParty.slave", "PRELOAD"])),
}


def write_png(path, img, scale_x=2, scale_y=4):
    """podglad: piksel hires 1:2 -> skala x2 poziomo, x4 pionowo"""
    import zlib
    h, w = len(img), len(img[0])
    rows = []
    for y in range(h):
        line = b"".join(bytes(WB_PALETTE[img[y][x]]) * scale_x for x in range(w))
        rows += [b"\0" + line] * scale_y
    raw = b"".join(rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale_x, h * scale_y, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "preview":
        out = ROOT / "build/preview"
        out.mkdir(parents=True, exist_ok=True)
        for kind, (_, make) in ICONS.items():
            k, img, tool, tts, stack = parse_icon(make())
            write_png(out / f"icon_{kind}.png", img)
            print(f"icon_{kind}.png  typ {k}, {len(img[0])}x{len(img)}, narzedzie {tool!r}, tooltypes {tts}")
        return
    if len(sys.argv) < 3 or sys.argv[1] not in ICONS:
        sys.exit(__doc__)
    name, make = ICONS[sys.argv[1]]
    data = make()
    parse_icon(data)                                         # kontrola formatu
    path = pathlib.Path(sys.argv[2]) / name
    path.write_bytes(data)
    print(f"{path}: {len(data)} B")


if __name__ == "__main__":
    main()
