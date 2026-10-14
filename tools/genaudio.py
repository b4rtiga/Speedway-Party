"""genaudio.py - probki dzwiekowe dla Pauli (8-bit ze znakiem) + podglady WAV.

  data/engine.raw   8 cykli zaplonu jednocylindrowego silnika (48 probek/cykl), petla
  data/crowd.raw    gwar kibicow, petla bez szwu, 11025 Hz
  data/buzzer.raw   start: brzeczyk + puszczenie tasmy (metaliczne "klak" i swist gumy), 0.7 s, 11025 Hz
  data/whistle.raw  gwizdek sedziego z "groszkiem" 0.8 s, 22050 Hz
Podglady: art/*.wav (engine_sweep.wav symuluje Paule: obroty od wolnych do maksymalnych).
Uruchomienie: python tools/genaudio.py"""
import pathlib
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAULA_CLOCK = 3546895          # PAL
ENGINE_CYCLE = 48              # probek na jeden zaplon - musi zgadzac sie z audio.c
rng = np.random.default_rng(500)


def to_s8(x, gain=0.95):
    x = x / (np.max(np.abs(x)) + 1e-9) * gain
    return np.clip(np.round(x * 127), -128, 127).astype(np.int8)


def lowpass(x, alpha):
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += alpha * (v - acc)
        y[i] = acc
    return y


def write_wav(name, s8, rate):
    with wave.open(str(ROOT / "build/preview" / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(int(rate))
        w.writeframes((s8.astype(np.int16) + 128).astype(np.uint8).tobytes())


# --- silnik: impuls wydechu = ostry atak, gasnace drgania + szum spalania ---
def engine():
    cycles = []
    t = np.arange(ENGINE_CYCLE) / ENGINE_CYCLE
    for k in range(8):
        amp = 1.0 + rng.uniform(-0.15, 0.15)
        x = np.exp(-t * 5.5) * np.sin(2 * np.pi * 3 * t) * amp
        x += 0.45 * np.exp(-t * 9) * np.sin(2 * np.pi * 7 * t + rng.uniform(0, 0.6))
        x += 0.25 * np.exp(-t * 3) * rng.normal(0, 1, ENGINE_CYCLE)
        x += 0.15 * np.sin(2 * np.pi * t)                 # podstawa zaplonu
        cycles.append(x)
    return to_s8(np.concatenate(cycles))


def crowd(n=16384, rate=11025):
    noise = rng.normal(0, 1, n + 4000)
    band = lowpass(noise, 0.35) - lowpass(noise, 0.04)      # pasmo "glosow" ~200 Hz - 1.5 kHz
    t = np.arange(n + 4000) / rate
    murmur = 0.7 + 0.3 * np.sin(2 * np.pi * 0.7 * t + 1.0) * np.sin(2 * np.pi * 0.23 * t)
    x = band * murmur
    fade = 4000                                              # przenikanie konca w poczatek -> petla bez szwu
    head = x[:fade].copy()
    x = x[fade:]
    w = np.linspace(0, 1, fade)
    x[-fade:] = x[-fade:] * (1 - w) + head * w
    return to_s8(x, 0.9)


def buzzer(rate=11025, dur=0.7):
    """brzeczyk startowy z dzwiekiem puszczenia tasmy (jeden kanal efektow Pauli)"""
    r = np.random.default_rng(1977)                      # osobne ziarno - inne probki bez zmian
    t = np.arange(int(rate * dur)) / rate
    bz = np.sign(np.sin(2 * np.pi * 185 * t)) * 0.6 + ((t * 370) % 1 - 0.5) * 0.8
    bz = np.tanh(bz * 2.0) * np.minimum(1, t / 0.01) * np.minimum(1, np.maximum(0, 0.6 - t) / 0.03)
    # "klak": zwolniony zaczep maszyny - krotki trzask i metaliczny dzwiek
    klak = r.normal(0, 1, t.size) * np.exp(-t / 0.006) * 1.6
    klak += np.sin(2 * np.pi * 1350 * t) * np.exp(-t / 0.035) * 0.9
    klak += np.sin(2 * np.pi * 2210 * t) * np.exp(-t / 0.02) * 0.5
    # swist gumy lecacej w gore: szum, ktory narasta i cichnie, coraz wyzszy
    sw_env = np.clip((t - 0.02) / 0.05, 0, 1) * np.exp(-np.maximum(0, t - 0.07) / 0.12)
    noise = r.normal(0, 1, t.size)
    hp = noise - lowpass(noise, 0.25)                    # gorne pasmo
    whoosh = lowpass(hp, 0.35) * sw_env * 1.3
    x = np.tanh((bz * 0.85 + klak * 0.45 + whoosh * 0.7) * 1.6)   # miekkie obciecie: trzask nie zjada skali
    return to_s8(x)


def whistle(rate=22050, dur=0.8):
    t = np.arange(int(rate * dur)) / rate
    trill = 35 + 4 * np.sin(2 * np.pi * 1.3 * t)              # "groszek" w gwizdku
    phase_t = np.cumsum(trill) / rate
    f = 2850 + 70 * np.sin(2 * np.pi * phase_t)
    phase = np.cumsum(2 * np.pi * f / rate)
    am = 0.62 + 0.38 * np.sin(2 * np.pi * phase_t) ** 2
    x = (np.sin(phase) + 0.18 * np.sin(2 * phase)) * am + rng.normal(0, 0.06, t.size)
    env = np.minimum(1, t / 0.02) * np.minimum(1, (dur - t) / 0.08)
    return to_s8(x * env)


def engine_sweep(sample, seconds=4.0, out_rate=22050):
    """Symulacja Pauli: probka w petli, okres zmienia sie z obrotami (jak w audio.c)."""
    out = np.zeros(int(out_rate * seconds))
    pos = 0.0
    for i in range(out.size):
        tt = i / out.size
        rpm = 4000 + 10000 * min(1, tt * 1.4) + 600 * np.sin(tt * 40)
        period = 8867237 // int(rpm)                      # ten sam wzor co audio.c
        rate = PAULA_CLOCK / period
        out[i] = sample[int(pos) % sample.size]
        pos += rate / out_rate
    return to_s8(out, 0.9), out_rate


def main():
    (ROOT / "data").mkdir(exist_ok=True)
    (ROOT / "build/preview").mkdir(parents=True, exist_ok=True)   # odsluch WAV (poza repo)
    sounds = {
        "engine": (engine(), None),
        "crowd": (crowd(), 11025),
        "buzzer": (buzzer(), 11025),
        "whistle": (whistle(), 22050),
    }
    for name, (s8, rate) in sounds.items():
        if s8.size % 2:                                    # Paula: dlugosc w slowach
            s8 = np.append(s8, np.int8(0))
        (ROOT / f"data/{name}.raw").write_bytes(s8.tobytes())
        if rate:
            write_wav(f"{name}.wav", s8 if name != "crowd" else np.tile(s8, 3), rate)
        print(f"data/{name}.raw  {s8.size} B")
    sweep, r = engine_sweep(sounds["engine"][0])
    write_wav("engine_sweep.wav", sweep, r)


if __name__ == "__main__":
    main()
