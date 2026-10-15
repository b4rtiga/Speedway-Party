"""pack.py - pakowanie danych gry na dyskietke (prosty LZ, rozpakowuje assets.c).

Format pliku: "SPZ1", dlugosc po rozpakowaniu (4 bajty big-endian), potem tokeny:
  c < 0x80   literaly: c + 1 bajtow (1..128) kopiowanych wprost
  c >= 0x80  powtorzenie: (c & 0x7F) + 3 bajtow (3..130) z odleglosci d (2 bajty
             big-endian, 1..65535) wstecz w tym, co juz rozpakowane (moze nachodzic -
             d = 1 to ciag tego samego bajtu)
Gra rozpoznaje spakowany plik po tym, ze jest krotszy od oczekiwanego rozmiaru danych;
plik pelnej dlugosci czyta jak dotad. Pliki w data/ (repozytorium) zostaja niespakowane -
pakowane sa tylko kopie w dist/ (make dist), z ktorych powstaja zip i dyskietka.

Uzycie: python3 tools/pack.py plik... (pakuje w miejscu, gdy wychodzi mniejszy;
kazdy wynik jest od razu rozpakowany i porownany z oryginalem)."""
import struct
import sys

MAGIC = b"SPZ1"
MIN_MATCH, MAX_MATCH, MAX_LIT, MAX_OFF = 4, 130, 128, 65535
CHAIN = 48                     # ile wczesniejszych pozycji z tym samym poczatkiem sprawdzac


def pack(data):
    n = len(data)
    out = bytearray(MAGIC + struct.pack(">I", n))
    lit = bytearray()
    head = {}

    def flush():
        while lit:
            k = min(len(lit), MAX_LIT)
            out.append(k - 1)
            out.extend(lit[:k])
            del lit[:k]

    def add(j):
        if j + 3 <= n:
            head.setdefault(data[j:j + 3], []).append(j)

    i = 0
    while i < n:
        best_len = best_off = 0
        if i + MIN_MATCH <= n:
            cands = head.get(data[i:i + 3])
            if cands:
                maxl = min(MAX_MATCH, n - i)
                for p in reversed(cands[-CHAIN:]):
                    off = i - p
                    if off > MAX_OFF:
                        break
                    ln = 0
                    while ln + 16 <= maxl and data[p + ln:p + ln + 16] == data[i + ln:i + ln + 16]:
                        ln += 16
                    while ln < maxl and data[p + ln] == data[i + ln]:
                        ln += 1
                    if ln > best_len:
                        best_len, best_off = ln, off
                        if ln == maxl:
                            break
        if best_len >= MIN_MATCH:
            flush()
            out += bytes((0x80 | (best_len - 3), best_off >> 8, best_off & 0xFF))
            for j in range(i, i + best_len):
                add(j)
            i += best_len
        else:
            lit.append(data[i])
            add(i)
            i += 1
            if len(lit) == MAX_LIT:
                flush()
    flush()
    return bytes(out)


def unpack(blob):
    assert blob[:4] == MAGIC
    n = struct.unpack(">I", blob[4:8])[0]
    out = bytearray()
    s = 8
    while s < len(blob):
        c = blob[s]
        s += 1
        if c < 0x80:
            out += blob[s:s + c + 1]
            s += c + 1
        else:
            k, off = (c & 0x7F) + 3, (blob[s] << 8) | blob[s + 1]
            s += 2
            assert 0 < off <= len(out)
            for _ in range(k):
                out.append(out[-off])
    assert len(out) == n
    return bytes(out)


def main():
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] == MAGIC:
            print(f"{path}: juz spakowany")
            continue
        blob = pack(data)
        assert unpack(blob) == data, path
        if len(blob) < len(data):
            with open(path, "wb") as f:
                f.write(blob)
            print(f"{path}: {len(data)} -> {len(blob)} B")
        else:
            print(f"{path}: {len(data)} B (pakowanie nic nie daje - bez zmian)")


if __name__ == "__main__":
    main()
