"""mkadf.py - startowa dyskietka ADF (Amiga DD, 880 KB, FastFileSystem) z katalogu gry.

    python3 tools/mkadf.py dist release/SpeedwayParty.adf [ETYKIETA]

Kopiuje cale drzewo katalogu (np. dist/: SpeedwayParty, data/, s/startup-sequence)
na obraz 1760 blokow po 512 B. Blok startowy "DOS\\1" ze standardowym kodem
(dos.library), wiec Kickstart 2.0+ uruchamia s/startup-sequence prosto z dyskietki.
Bez zaleznosci - sama biblioteka standardowa Pythona."""
import struct
import sys
from pathlib import Path

BLOCKS = 1760
BSIZE = 512
ROOT = 880
HT_SIZE = 72                  # wpisow tablicy mieszajacej w bloku katalogu
T_HEADER, T_DATA, T_LIST = 2, 8, 16
ST_ROOT, ST_DIR, ST_FILE = 1, 2, -3

# standardowy kod bloku startowego (jak z Install): FindResident("dos.library") i skok do init
BOOTCODE = bytes.fromhex("43FA00184EAEFFA04A80670A20402068001670004E7570FF4E75") + b"dos.library\0"


def be32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


class Disk:
    def __init__(self, label):
        self.img = bytearray(BLOCKS * BSIZE)
        self.used = {0, 1, ROOT, ROOT + 1}             # boot, root, bitmapa
        self.next_free = 2
        self.label = label
        self.dirs = {}                                 # blok katalogu -> {hash: [bloki]}

    # ------------------------------------------------------------------ bloki
    def alloc(self):
        # kolejne wolne bloki od poczatku dysku (po bloku startowym), z pominieciem root/bitmapy
        while self.next_free in self.used:
            self.next_free += 1
        if self.next_free >= BLOCKS:
            raise SystemExit("ADF: brak miejsca na dyskietce (880 KB)")
        b = self.next_free
        self.used.add(b)
        return b

    def put32(self, block, off, v):
        o = block * BSIZE + off
        self.img[o:o + 4] = be32(v)

    def get32(self, block, off):
        o = block * BSIZE + off
        return struct.unpack(">I", self.img[o:o + 4])[0]

    def set_name(self, block, name):
        n = name.encode("latin-1")[:30]
        o = block * BSIZE + 432
        self.img[o] = len(n)
        self.img[o + 1:o + 1 + len(n)] = n

    def set_dates(self, block):
        for off in (420, 472, 484):                    # days/mins/ticks (1978-01-01 + dni) - stala data
            self.put32(block, off, 17800)
            self.put32(block, off + 4, 0)
            self.put32(block, off + 8, 0)

    def header_checksum(self, block):
        self.put32(block, 20, 0)
        s = sum(self.get32(block, i * 4) for i in range(128))
        self.put32(block, 20, -s)

    # --------------------------------------------------------------- katalogi
    @staticmethod
    def hash_name(name):
        h = len(name)
        for c in name.upper():
            h = (h * 13 + ord(c)) & 0x7FF
        return h % HT_SIZE

    def link(self, parent, block, name):
        """dopisanie wpisu do tablicy mieszajacej katalogu (lancuch przez hash_chain)"""
        slot = 24 + self.hash_name(name) * 4
        prev = self.get32(parent, slot)
        self.put32(block, 496, prev)                   # hash_chain = poprzednia glowa
        self.put32(parent, slot, block)

    def make_root(self):
        r = ROOT
        self.put32(r, 0, T_HEADER)
        self.put32(r, 12, HT_SIZE)
        self.put32(r, 312, 0xFFFFFFFF)                 # bm_flag: bitmapa poprawna
        self.put32(r, 316, ROOT + 1)                   # bm_pages[0]
        self.set_dates(r)
        self.set_name(r, self.label)
        self.put32(r, 508, ST_ROOT)

    def make_dir(self, parent, name):
        b = self.alloc()
        self.put32(b, 0, T_HEADER)
        self.put32(b, 4, b)
        self.set_dates(b)
        self.set_name(b, name)
        self.put32(b, 500, parent)
        self.put32(b, 508, ST_DIR)
        self.link(parent, b, name)
        return b

    def make_file(self, parent, name, data):
        hdr = self.alloc()
        nblocks = (len(data) + BSIZE - 1) // BSIZE
        data_blocks = []
        for i in range(nblocks):
            d = self.alloc()
            data_blocks.append(d)
            chunk = data[i * BSIZE:(i + 1) * BSIZE]
            self.img[d * BSIZE:d * BSIZE + len(chunk)] = chunk    # FFS: dane bez naglowka
        # naglowek pliku + bloki rozszerzen (po 72 wskazniki, w odwrotnej kolejnosci)
        chunks = [data_blocks[i:i + HT_SIZE] for i in range(0, max(1, len(data_blocks)), HT_SIZE)] or [[]]
        holders = [hdr] + [self.alloc() for _ in chunks[1:]]
        for k, (blk, ptrs) in enumerate(zip(holders, chunks)):
            self.put32(blk, 0, T_HEADER if k == 0 else T_LIST)
            self.put32(blk, 4, blk)
            self.put32(blk, 8, len(ptrs))              # high_seq
            if k == 0 and ptrs:
                self.put32(blk, 16, ptrs[0])           # first_data
            for j, p in enumerate(ptrs):
                self.put32(blk, 308 - j * 4, p)        # table[71 - j]
            self.put32(blk, 500, hdr if k else parent)
            self.put32(blk, 504, holders[k + 1] if k + 1 < len(holders) else 0)
            self.put32(blk, 508, ST_FILE)
        self.put32(hdr, 324, len(data))                # byte_size
        self.set_dates(hdr)
        self.set_name(hdr, name)
        self.link(parent, hdr, name)
        for blk in holders[1:]:                        # naglowek liczy add_tree (po dopisaniu do katalogu)
            self.header_checksum(blk)
        return hdr

    def add_tree(self, parent, path):
        for p in sorted(path.iterdir(), key=lambda q: q.name.lower()):
            if p.is_dir():
                d = self.make_dir(parent, p.name)
                self.add_tree(d, p)
                self.header_checksum(d)
            else:
                h = self.make_file(parent, p.name, p.read_bytes())
                self.header_checksum(h)

    # ---------------------------------------------------------------- koniec
    def finish(self):
        self.header_checksum(ROOT)
        # bitmapa: bit ustawiony = blok wolny; bit 0 pierwszego slowa = blok 2
        bm = ROOT + 1
        for i in range(2, BLOCKS):
            if i not in self.used:
                w, bit = divmod(i - 2, 32)
                off = 4 + w * 4
                self.put32(bm, off, self.get32(bm, off) | (1 << bit))
        self.put32(bm, 0, 0)
        s = sum(self.get32(bm, i * 4) for i in range(128))
        self.put32(bm, 0, -s)
        # blok startowy: DOS\1 (FFS), suma kontrolna z przeniesieniem, wskaznik na root
        self.img[0:4] = b"DOS\x01"
        self.img[8:12] = be32(ROOT)
        self.img[12:12 + len(BOOTCODE)] = BOOTCODE
        s = 0
        for i in range(0, 1024, 4):
            if i == 4:
                continue
            s += struct.unpack(">I", self.img[i:i + 4])[0]
            if s > 0xFFFFFFFF:
                s = (s & 0xFFFFFFFF) + 1
        self.img[4:8] = be32(~s)
        return bytes(self.img)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    label = sys.argv[3] if len(sys.argv) > 3 else "SpeedwayParty"
    disk = Disk(label)
    disk.make_root()
    disk.add_tree(ROOT, src)
    img = disk.finish()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(img)
    free = BLOCKS - len(disk.used)
    print(f"{out}: {len(img)} B, wolne {free} blokow ({free * BSIZE // 1024} KB)")


if __name__ == "__main__":
    main()
