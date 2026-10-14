"""import_engine.py - pętla silnika dla Pauli z prawdziwego nagrania WAV.

Uzycie:
  python tools/import_engine.py nagranie.wav                 # automatyczny wybor fragmentu
  python tools/import_engine.py nagranie.wav --start 3.2     # fragment od 3.2 s

Co robi:
  1. szuka fragmentu 0.4 s z rowna wysokoscia tonu i duza glosnoscia
     (motocykl przejezdzajacy obok zmienia ton - efekt Dopplera - tego unikamy),
  2. mierzy okres zaplonu autokorelacja (60-260 Hz = 7000-31000 obr/min 4-suwu),
  3. wycina 8 kolejnych cykli od przejscia przez zero i kazdy przeprobkowuje do
     48 probek (tyle zaklada audio.c), wiec petla jest bez szwu, a wysokosc tonu
     w grze dalej liczy sie z obrotow,
  4. zapisuje data/engine.raw (8-bit ze znakiem) i podglad build/preview/engine_real_sweep.wav.
Obsluguje WAV 8/16/24/32-bit, mono i stereo (MP3 trzeba najpierw zamienic na WAV)."""
import argparse
import pathlib
import sys
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import genaudio  # noqa: E402

CYCLES = 8
CYCLE_LEN = genaudio.ENGINE_CYCLE
F_MIN, F_MAX = 60.0, 260.0


def load_wav(path):
    with wave.open(str(path), "rb") as w:
        n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if width == 1:
        x = np.frombuffer(raw, np.uint8).astype(np.float64) - 128
    elif width == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64)
    elif width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float64)
    else:
        x = np.frombuffer(raw, "<i4").astype(np.float64)
    x = x.reshape(-1, ch).mean(axis=1)
    return x - x.mean(), rate


def period_of(seg, rate):
    """okres zaplonu w probkach (ulamkowy) albo None"""
    seg = seg - seg.mean()
    ac = np.correlate(seg, seg, "full")[seg.size - 1:]
    lo, hi = int(rate / F_MAX), int(rate / F_MIN)
    if hi + 2 >= ac.size or ac[0] <= 0:
        return None, 0.0
    k = lo + int(np.argmax(ac[lo:hi]))
    a, b, c = ac[k - 1], ac[k], ac[k + 1]                   # dokladnosc ponizej probki
    shift = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return k + shift, b / ac[0]


def pick_segment(x, rate, win_s=0.4):
    win, hop = int(rate * win_s), int(rate * 0.05)
    best, best_score = 0, -1.0
    rms_max = np.sqrt(np.mean(x ** 2)) * 3 + 1e-9
    for st in range(0, max(1, x.size - win), hop):
        seg = x[st:st + win]
        p1, c1 = period_of(seg[: win // 2], rate)
        p2, c2 = period_of(seg[win // 2:], rate)
        if not p1 or not p2:
            continue
        stability = 1 - min(1, abs(p1 - p2) / p1 * 20)      # rowny ton w obu polowkach
        loud = min(1, np.sqrt(np.mean(seg ** 2)) / rms_max)
        score = stability * min(c1, c2) * (0.3 + loud)
        if score > best_score:
            best, best_score = st, score
    return best, best_score


def extract_loop(x, rate, start):
    seg = x[start:start + int(rate * 0.4)]
    T, clarity = period_of(seg, rate)
    if not T:
        sys.exit("nie znaleziono okresu zaplonu - sprobuj innego fragmentu (--start)")
    # przejscie przez zero w gore na poczatku pierwszego cyklu
    z = start + int(np.argmax((x[start:start + int(T)][:-1] < 0) & (x[start + 1:start + int(T)] >= 0)))
    # wygladzenie przed zmniejszeniem liczby probek (antyaliasing)
    k = max(1, int(T / CYCLE_LEN))
    sm = np.convolve(x, np.ones(k) / k, mode="same")
    pos = z + np.arange(CYCLES * CYCLE_LEN) * (T / CYCLE_LEN)
    loop = np.interp(pos, np.arange(sm.size), sm)
    loop -= loop.mean()
    return genaudio.to_s8(loop, 0.95), T, clarity


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--start", type=float, help="poczatek fragmentu w sekundach")
    args = ap.parse_args()

    x, rate = load_wav(args.wav)
    if args.start is not None:
        start, score = int(args.start * rate), None
    else:
        start, score = pick_segment(x, rate)
    loop, T, clarity = extract_loop(x, rate, start)
    f = rate / T
    print(f"fragment od {start / rate:.2f} s, zaplon {f:.1f} Hz (~{f * 120:.0f} obr/min), "
          f"czytelnosc okresu {clarity:.2f}" + (f", ocena {score:.2f}" if score is not None else ""))
    if clarity < 0.3:
        print("UWAGA: slabo okresowy fragment (szum, tlum?) - sprobuj --start z czystym przejazdem")

    (ROOT / "data/engine.raw").write_bytes(loop.tobytes())
    sweep, r = genaudio.engine_sweep(loop)
    genaudio.write_wav("engine_real_sweep.wav", sweep, r)
    print("zapisano data/engine.raw i build/preview/engine_real_sweep.wav")


if __name__ == "__main__":
    main()
