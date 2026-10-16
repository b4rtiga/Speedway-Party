"""genaudio.py - probki dzwiekowe dla Pauli (8-bit ze znakiem) + podglady WAV.

  data/engine.raw   8 cykli zaplonu jednocylindrowego silnika (48 probek/cykl), petla - syntetyczna;
                    gdy jest nagranie ENGINE_WAV, engine.raw robi z niego tools/import_engine.py
  data/crowd.raw    gwar kibicow, petla bez szwu, 11025 Hz
  data/buzzer.raw   start: brzeczyk + puszczenie tasmy (metaliczne "klak" i swist gumy), 0.7 s, 11025 Hz
  data/groan.raw    jek zawodu trybun ("uuu...") po tasmie i przerwaniu biegu, 1.4 s, 11025 Hz -
                    na zuzlu sedzia nie gwizdze (czerwone swiatla), slychac trybuny
  data/cheer.raw    okrzyk trybun, gdy prowadzacy wjezdza na ostatnia prosta, 2.6 s, 11025 Hz - syntetyczny;
                    gdy jest nagranie CHEER_WAV, cheer.raw powstaje z niego (mono, 11025 Hz, do 3 s)
Podglady: art/*.wav (engine_sweep.wav symuluje Paule: obroty od wolnych do maksymalnych).
Uruchomienie: python tools/genaudio.py"""
import pathlib
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAULA_CLOCK = 3546895          # PAL
ENGINE_CYCLE = 48              # probek na jeden zaplon - musi zgadzac sie z audio.c
ENGINE_WAV = ROOT / "src/speedway_500cc_10000rpm_loop.wav"   # nagranie silnika (import_engine.py)
CHEER_WAV = ROOT / "src/crowd_cheer.wav"                      # nagranie okrzyku trybun (opcjonalne)
CHEER_MAX = 3.0                                               # s - dluzsze nagranie jest przyciete
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


def groan(rate=11025, dur=1.4):
    """jek zawodu trybun: kilkadziesiat glosow "uuu" (samogloska u/o) z opadajacym tonem + szum"""
    r = np.random.default_rng(1414)                      # osobne ziarno - inne probki bez zmian
    n = int(rate * dur)
    t = np.arange(n) / rate
    src = np.zeros(n)
    for _ in range(56):
        f0 = r.uniform(110, 260)
        on = r.uniform(0.0, 0.18)
        rise = r.uniform(0.08, 0.2)
        fall = 1 - 0.22 * np.clip((t - on) / 0.9, 0, 1)   # "uuuu..." opada
        vib = 1 + 0.02 * np.sin(2 * np.pi * r.uniform(3, 6) * t + r.uniform(0, 6))
        phase = np.cumsum(f0 * fall * vib) / rate
        saw = 2 * (phase % 1) - 1
        env = np.clip((t - on) / rise, 0, 1) * np.exp(-np.maximum(0, t - on - 0.4) / r.uniform(0.35, 0.7))
        src += saw * env * r.uniform(0.5, 1.0)
    voices = resonator(src, 340, 90, rate) + 0.55 * resonator(src, 780, 130, rate) + 0.15 * resonator(src, 2400, 300, rate)
    noise = r.normal(0, 1, n)
    murmur = (lowpass(noise, 0.25) - lowpass(noise, 0.04)) * np.clip(t / 0.15, 0, 1) * np.exp(-t / 1.0)
    x = voices / (np.max(np.abs(voices)) + 1e-9) + 0.3 * murmur / (np.max(np.abs(murmur)) + 1e-9)
    x *= np.minimum(1, (dur - t) / 0.4)
    return to_s8(np.tanh(x * 1.3))


def resonator(x, f, bw, rate):
    """2-biegunowy filtr rezonansowy (formant samogloski)"""
    r = np.exp(-np.pi * bw / rate)
    a1, a2 = 2 * r * np.cos(2 * np.pi * f / rate), -r * r
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    for i, v in enumerate(x):
        y0 = v + a1 * y1 + a2 * y2
        y[i] = y0
        y2, y1 = y1, y0
    return y * (1 - r)


def cheer(rate=11025, dur=2.6):
    """okrzyk trybun: kilkadziesiat glosow "aaa!" (rozne wysokosci i wejscia) + oddech i oklaski"""
    r = np.random.default_rng(2026)                      # osobne ziarno - inne probki bez zmian
    n = int(rate * dur)
    t = np.arange(n) / rate
    src = np.zeros(n)
    for _ in range(64):
        f0 = r.uniform(140, 330)                         # meskie i kobiece glosy
        on = r.uniform(0.0, 0.3)
        rise = r.uniform(0.06, 0.18)
        tail = r.uniform(0.9, 1.8)
        glide = 1 + 0.12 * np.clip((t - on) / 0.25, 0, 1)   # krzyk idzie lekko w gore
        vib = 1 + 0.015 * np.sin(2 * np.pi * r.uniform(4, 7) * t + r.uniform(0, 6))
        phase = np.cumsum(f0 * glide * vib) / rate
        saw = 2 * (phase % 1) - 1
        env = np.clip((t - on) / rise, 0, 1) * np.exp(-np.maximum(0, t - on - 0.35) / tail)
        src += saw * env * r.uniform(0.5, 1.0)
    voices = resonator(src, 760, 140, rate) + 0.7 * resonator(src, 1250, 180, rate) + 0.25 * resonator(src, 2600, 300, rate)
    noise = r.normal(0, 1, n)
    breath = (lowpass(noise, 0.45) - lowpass(noise, 0.08)) * np.clip(t / 0.1, 0, 1) * np.exp(-t / 1.4)
    claps = np.zeros(n)
    for _ in range(140):                                 # oklaski od ok. 0.4 s
        c0 = int(rate * r.uniform(0.4, dur - 0.1))
        k = np.arange(min(220, n - c0))
        claps[c0:c0 + k.size] += r.normal(0, 1, k.size) * np.exp(-k / 35) * r.uniform(0.3, 1.0)
    claps = claps - lowpass(claps, 0.15)
    x = voices / (np.max(np.abs(voices)) + 1e-9) + 0.35 * breath / (np.max(np.abs(breath)) + 1e-9)
    x += 0.3 * claps / (np.max(np.abs(claps)) + 1e-9) * np.clip((t - 0.4) / 0.3, 0, 1)
    x *= np.minimum(1, (dur - t) / 0.5)                  # wyciszenie na koncu
    return to_s8(np.tanh(x * 1.4))


def cheer_from_wav(path, rate=11025):
    """nagranie okrzyku -> mono 8 bit, 11025 Hz, najwyzej CHEER_MAX s z wyciszeniem na koncu"""
    with wave.open(str(path), "rb") as w:
        ch, sw, sr, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(nf)
    if sw == 1:
        x = np.frombuffer(raw, np.uint8).astype(float) - 128
    elif sw == 2:
        x = np.frombuffer(raw, "<i2").astype(float)
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) << 8 >> 8).astype(float)
    else:
        x = np.frombuffer(raw, "<i4").astype(float)
    x = x.reshape(-1, ch).mean(axis=1)
    x = x[: int(sr * CHEER_MAX)]
    alpha = min(1.0, 2 * np.pi * 5000 / sr)              # bez skladowych powyzej ~5 kHz przed zmiana czestotliwosci
    x = lowpass(lowpass(x, alpha), alpha)
    n = int(x.size * rate / sr)
    x = np.interp(np.arange(n) * sr / rate, np.arange(x.size), x)
    t = np.arange(n) / rate
    x *= np.minimum(1, (t[-1] - t) / 0.4 + 1e-9)
    return to_s8(x)


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
        "groan": (groan(), 11025),
        "cheer": (cheer_from_wav(CHEER_WAV) if CHEER_WAV.exists() else cheer(), 11025),
    }
    if CHEER_WAV.exists():
        print(f"data/cheer.raw   z nagrania {CHEER_WAV.name}")
    if ENGINE_WAV.exists():                                # silnik z nagrania - nie nadpisujemy
        del sounds["engine"]
        print(f"data/engine.raw  z nagrania {ENGINE_WAV.name} (tools/import_engine.py)")
    for name, (s8, rate) in sounds.items():
        if s8.size % 2:                                    # Paula: dlugosc w slowach
            s8 = np.append(s8, np.int8(0))
        (ROOT / f"data/{name}.raw").write_bytes(s8.tobytes())
        if rate:
            write_wav(f"{name}.wav", s8 if name != "crowd" else np.tile(s8, 3), rate)
        print(f"data/{name}.raw  {s8.size} B")
    if "engine" in sounds:
        sweep, r = engine_sweep(sounds["engine"][0])
        write_wav("engine_sweep.wav", sweep, r)


if __name__ == "__main__":
    main()
