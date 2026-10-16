#!/usr/bin/env python3
"""Obraz plyty ISO 9660 dla Amigi CD32 (bez zewnetrznych narzedzi).

    python3 tools/mkiso.py <katalog> <wynik.iso> <ETYKIETA> [plik.TM]

Katalog (np. dist/ z s/startup-sequence) trafia na plyte 1:1. CD32 startuje tylko z plyty,
na ktorej jest plik znaku towarowego (CD32.TM, z dowolnej plyty CD32) - to plik Commodore,
wiec nie ma go w repozytorium: podaje sie go czwartym argumentem (trafia do korzenia plyty,
a jego nazwa do pola "copyright" deskryptora, jak robi to ISOCD). Ustawienia jak w znanym
przepisie mkisofs na CD32: identyfikator systemu CDTV, nazwy wielkimi literami z dowolnymi
znakami (-relaxed-filenames), bez kropki na koncu nazwy bez rozszerzenia (-d), do 31 znakow.
"""
import os
import struct
import sys
import time

SECTOR = 2048


def both16(v):
    return struct.pack('<H', v) + struct.pack('>H', v)


def both32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)


def text(s, n):
    return s.encode('ascii')[:n].ljust(n, b' ')


def rec_date(t):
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, 0])


def vol_date(t):
    return time.strftime('%Y%m%d%H%M%S', t).encode('ascii') + b'00' + b'\x00'


def iso_name(name, is_dir):
    n = name.upper()
    if len(n) > 31 or '/' in n or ';' in n:
        sys.exit('mkiso: nazwa nie pasuje do ISO 9660: ' + name)
    return n.encode('ascii') if is_dir else (n + ';1').encode('ascii')


class Node:
    def __init__(self, name, path, is_dir):
        self.name, self.path, self.is_dir = name, path, is_dir
        self.children = []
        self.lba = 0
        self.size = 0
        self.parent = None
        self.number = 0


def scan(path, name=''):
    node = Node(name, path, True)
    for e in sorted(os.listdir(path), key=lambda s: s.upper()):
        p = os.path.join(path, e)
        child = scan(p, e) if os.path.isdir(p) else Node(e, p, False)
        if not child.is_dir:
            child.size = os.path.getsize(p)
        child.parent = node
        node.children.append(child)
    return node


def dir_record(ident, lba, size, is_dir, t):
    n = len(ident)
    length = 33 + n + (1 if n % 2 == 0 else 0)
    r = bytes([length, 0]) + both32(lba) + both32(size) + rec_date(t) + bytes([2 if is_dir else 0, 0, 0]) + both16(1) + bytes([n]) + ident
    return r.ljust(length, b'\x00')


def dir_sectors(node):
    """Rekordy katalogu w sektorach (rekord nie moze przechodzic przez granice sektora)."""
    idents = [b'\x00', b'\x01'] + [iso_name(c.name, c.is_dir) for c in node.children]
    used, sectors = 0, 1
    for ident in idents:
        n = 33 + len(ident) + (1 if len(ident) % 2 == 0 else 0)
        if used + n > SECTOR:
            sectors += 1
            used = 0
        used += n
    return sectors


def build_dir(node, t):
    out, cur = [], b''
    entries = [(b'\x00', node), (b'\x01', node.parent or node)] + [(iso_name(c.name, c.is_dir), c) for c in node.children]
    for ident, c in entries:
        r = dir_record(ident, c.lba, c.size, c.is_dir, t)
        if len(cur) + len(r) > SECTOR:
            out.append(cur.ljust(SECTOR, b'\x00'))
            cur = b''
        cur += r
    out.append(cur.ljust(SECTOR, b'\x00'))
    return b''.join(out)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, out, label = sys.argv[1], sys.argv[2], sys.argv[3]
    tm = sys.argv[4] if len(sys.argv) > 4 else None
    t = time.gmtime(int(os.environ.get('SOURCE_DATE_EPOCH', time.time())))

    root = scan(src)
    if tm:
        if not os.path.isfile(tm):
            sys.exit('mkiso: brak pliku ' + tm)
        tm_node = Node(os.path.basename(tm), tm, False)
        tm_node.size = os.path.getsize(tm)
        tm_node.parent = root
        root.children = [tm_node] + [c for c in root.children if c.name.upper() != tm_node.name.upper()]
        root.children.sort(key=lambda c: c.name.upper())

    dirs, queue = [], [root]                   # kolejnosc tablicy sciezek: poziomami
    while queue:
        d = queue.pop(0)
        d.number = len(dirs) + 1
        dirs.append(d)
        queue += [c for c in d.children if c.is_dir]
    if len(dirs) > 1:                          # w obrebie poziomu: wg numeru rodzica, potem nazwy
        dirs = [dirs[0]] + sorted(dirs[1:], key=lambda d: (depth(d), d.parent.number, d.name.upper()))
        for i, d in enumerate(dirs):
            d.number = i + 1

    def path_table(big):
        b = b''
        for d in dirs:
            ident = b'\x00' if d is root else iso_name(d.name, True)
            pnum = d.parent.number if d.parent else 1
            b += bytes([len(ident), 0]) + struct.pack('>I' if big else '<I', 0)
            b += struct.pack('>H' if big else '<H', pnum) + ident + (b'\x00' if len(ident) % 2 else b'')
        return b

    pt_size = len(path_table(False))
    pt_sectors = (pt_size + SECTOR - 1) // SECTOR
    lba = 16 + 2                               # PVD, terminator
    l_pt, lba = lba, lba + pt_sectors
    m_pt, lba = lba, lba + pt_sectors
    for d in dirs:
        d.lba = lba
        d.size = dir_sectors(d) * SECTOR
        lba += dir_sectors(d)
    files = []
    stack = [root]
    while stack:                               # pliki w kolejnosci katalogow (plik TM pierwszy)
        d = stack.pop(0)
        for c in d.children:
            if c.is_dir:
                stack.append(c)
            else:
                files.append(c)
    for f in files:
        f.lba = lba
        lba += max(1, (f.size + SECTOR - 1) // SECTOR)
    total = lba

    def pt_with_lba(big):
        b = b''
        for d in dirs:
            ident = b'\x00' if d is root else iso_name(d.name, True)
            pnum = d.parent.number if d.parent else 1
            b += bytes([len(ident), 0]) + struct.pack('>I' if big else '<I', d.lba)
            b += struct.pack('>H' if big else '<H', pnum) + ident + (b'\x00' if len(ident) % 2 else b'')
        return b.ljust(pt_sectors * SECTOR, b'\x00')

    pvd = bytearray(SECTOR)
    pvd[0:8] = b'\x01CD001\x01\x00'
    pvd[8:40] = text('CDTV', 32)
    pvd[40:72] = text(label.upper(), 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(pt_size)
    pvd[140:144] = struct.pack('<I', l_pt)
    pvd[148:152] = struct.pack('>I', m_pt)
    pvd[156:190] = dir_record(b'\x00', root.lba, root.size, True, t)
    pvd[190:318] = text(label.upper(), 128)
    pvd[318:446] = text('', 128)
    pvd[446:574] = text('', 128)
    pvd[574:702] = text('', 128)
    pvd[702:739] = text(os.path.basename(tm).upper() if tm else '', 37)
    pvd[739:776] = text('', 37)
    pvd[776:813] = text('', 37)
    pvd[813:830] = vol_date(t)
    pvd[830:847] = vol_date(t)
    pvd[847:864] = b'0' * 16 + b'\x00'
    pvd[864:881] = b'0' * 16 + b'\x00'
    pvd[881] = 1
    term = bytearray(SECTOR)
    term[0:7] = b'\xffCD001\x01'

    with open(out, 'wb') as o:
        o.write(b'\x00' * 16 * SECTOR)
        o.write(pvd)
        o.write(term)
        o.write(pt_with_lba(False))
        o.write(pt_with_lba(True))
        for d in dirs:
            o.write(build_dir(d, t))
        for f in files:
            with open(f.path, 'rb') as i:
                data = i.read()
            o.write(data.ljust(max(1, (len(data) + SECTOR - 1) // SECTOR) * SECTOR, b'\x00'))
    print('%s: %d B, %d plikow%s' % (out, total * SECTOR, len(files), '' if tm else ' (BEZ pliku TM - CD32 z niej nie wystartuje)'))


def depth(d):
    n = 0
    while d.parent:
        n += 1
        d = d.parent
    return n


if __name__ == '__main__':
    main()
