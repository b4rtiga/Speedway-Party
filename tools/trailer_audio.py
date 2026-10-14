"""trailer_audio.py - silniki do trailera: dzwiek jak z Pauli z rejestrow zapisanych przez gre.

Wejscie: plik z tools/shots (TRAILER_AUDIO) - co klatke (50 fps) 4 slowa little-endian:
AUD0PER, AUD0VOL, AUD1PER, AUD1VOL (kanal 0 lewy, 1 prawy - jak w audio.c), oraz probka
data/engine.raw (8 bit ze znakiem, petla). Paula czyta probke bez interpolacji z czestotliwoscia
3546895 / okres - tak samo tutaj, wiec brzmi jak na Amidze.
Wyjscie: WAV 48 kHz stereo 16 bit.
Uzycie: python tools/trailer_audio.py rejestry.bin wyjscie.wav"""
import pathlib
import sys
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAULA_CLOCK = 3546895          # PAL
FPS = 50
OUT_RATE = 48000


def main():
    regs = np.fromfile(sys.argv[1], "<u2").reshape(-1, 4).astype(np.int64)
    smp = np.frombuffer((ROOT / "data/engine.raw").read_bytes(), np.int8).astype(np.float64) / 128
    per_frame = OUT_RATE // FPS
    out = np.zeros((regs.shape[0] * per_frame, 2))
    pos = [0.0, 0.0]
    for f, (p0, v0, p1, v1) in enumerate(regs):
        for ch, (per, vol) in enumerate(((p0, v0), (p1, v1))):
            step = PAULA_CLOCK / max(per, 124) / OUT_RATE        # probki zrodla na probke wyjscia
            idx = (pos[ch] + step * np.arange(per_frame)).astype(np.int64) % smp.size
            out[f * per_frame:(f + 1) * per_frame, ch] = smp[idx] * (vol / 64)
            pos[ch] = (pos[ch] + step * per_frame) % smp.size
    out *= 0.7
    with wave.open(sys.argv[2], "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes((np.clip(out, -1, 1) * 32767).astype("<i2").tobytes())
    print(f"{sys.argv[2]}: {regs.shape[0] / FPS:.1f} s silnikow")


if __name__ == "__main__":
    main()
