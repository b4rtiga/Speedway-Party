#!/usr/bin/env python3
"""Muzyka w menu w 4-bitowym IMA ADPCM - o polowe mniej pamieci niz PCM 8 bit.

    python3 tools/adpcm.py <utwor_s8.raw> <music.adp>          kodowanie
    python3 tools/adpcm.py --decode <music.adp> <wynik_s8.raw>  dekodowanie (odsluch, test)
    python3 tools/adpcm.py --wav <music.adp> <wynik.wav>        dekodowanie do WAV (~16 kHz)

Plik: "SPA4", liczba probek (ULONG, big endian, parzysta), potem po 2 probki na bajt
(starsza polowka pierwsza). Dekoder zaczyna od stanu zerowego (przewidywanie 0, indeks 0)
i wraca do niego na poczatku kazdej petli utworu - tak jak koder, wiec petla gra dokladnie
to samo. Dekoder w grze: src/audio.c (adpcm_decode) - te same tablice i ta sama arytmetyka.
"""
import struct
import sys
import wave

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
        449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]
MAGIC = b'SPA4'
PER_MUSIC = 222                 # PAL: 3546895 / 222 = 15977 Hz


def step_state(pred, idx, n):
    """Jeden kod 4-bitowy -> nowy stan (dokladnie jak adpcm_decode w audio.c)."""
    st = STEP[idx]
    diff = st >> 3
    if n & 4:
        diff += st
    if n & 2:
        diff += st >> 1
    if n & 1:
        diff += st >> 2
    pred = pred - diff if n & 8 else pred + diff
    pred = max(-32768, min(32767, pred))
    idx = max(0, min(88, idx + INDEX[n & 7]))
    return pred, idx


def out8(pred):
    return pred >> 8                     # -128..127


def encode(samples):
    """Kod dla kazdej probki: 4 najlepsze z 16 i z nich ten, po ktorym najlepiej trafia
    tez nastepna probka (o ok. 1 dB lepiej niz wybor zachlanny)."""
    pred, idx, codes = 0, 0, []
    n = len(samples)
    for i, s in enumerate(samples):
        t0 = s * 256
        t1 = samples[i + 1] * 256 if i + 1 < n else t0
        cand = []
        for k in range(16):
            p, x = step_state(pred, idx, k)
            cand.append((abs(p - t0), k, p, x))
        cand.sort()
        best = None
        for e0, k, p, x in cand[:4]:
            e1 = min(abs(step_state(p, x, m)[0] - t1) for m in range(16))
            tot = e0 * e0 + e1 * e1
            if best is None or tot < best[0]:
                best = (tot, k, p, x)
        codes.append(best[1])
        pred, idx = best[2], best[3]
    return codes


def decode_codes(codes):
    pred, idx, out = 0, 0, []
    for n in codes:
        pred, idx = step_state(pred, idx, n)
        out.append(out8(pred))
    return out


def pack(codes):
    if len(codes) & 1:
        codes = codes + [0]
    b = bytearray(MAGIC + struct.pack('>I', len(codes)))
    for i in range(0, len(codes), 2):
        b.append((codes[i] << 4) | codes[i + 1])
    return bytes(b)


def unpack(data):
    if data[:4] != MAGIC:
        sys.exit('adpcm: to nie jest plik SPA4')
    n = struct.unpack('>I', data[4:8])[0]
    codes = []
    for b in data[8:8 + n // 2]:
        codes += [b >> 4, b & 15]
    return codes


def s8(data):
    return [b - 256 if b > 127 else b for b in data]


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] in ('--decode', '--wav'):
        pcm = decode_codes(unpack(open(a[1], 'rb').read()))
        raw = bytes(v & 255 for v in pcm)
        if a[0] == '--decode':
            open(a[2], 'wb').write(raw)
        else:
            w = wave.open(a[2], 'wb')
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(round(3546895 / PER_MUSIC))
            w.writeframes(bytes((v + 128) & 255 for v in pcm))   # WAV 8 bit: bez znaku
            w.close()
        return
    if len(a) != 2:
        sys.exit(__doc__)
    src = s8(open(a[0], 'rb').read())
    if len(src) & 1:
        src.append(0)
    codes = encode(src)
    open(a[1], 'wb').write(pack(codes))
    dec = decode_codes(codes)
    sig = sum(v * v for v in src) or 1
    err = sum((x - y) ** 2 for x, y in zip(src, dec)) or 1
    import math
    print('%s: %d -> %d B, SNR %.1f dB' % (a[1], len(src), 8 + len(codes) // 2, 10 * math.log10(sig / err)))


if __name__ == '__main__':
    main()
