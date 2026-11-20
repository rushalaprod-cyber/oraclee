# Harder, more realistic test material with ground truth: python gen_real.py outdir
#  reverb tails, slow pad attacks, legato + vibrato + pitch scoops, sustain pedal, detune/chorus, glides
import numpy as np, wave, sys, json
out = sys.argv[1]
sr = 44100

def write(name, x, truth, bpm):
    x = x / np.max(np.abs(x)) * 0.9
    w = wave.open(f'{out}/{name}.wav', 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes((x * 32767).astype('<i2').tobytes()); w.close()
    json.dump(dict(bpm=bpm, truth=truth), open(f'{out}/{name}.json', 'w'))

def reverb(x, amount=0.25, t60=1.6, seed=0):
    rng = np.random.default_rng(seed)
    n = int(sr * t60)
    ir = rng.standard_normal(n) * np.exp(-6.9 * np.arange(n) / n)
    ir[:int(0.012 * sr)] = 0
    wet = np.fft.irfft(np.fft.rfft(x, len(x) + n) * np.fft.rfft(ir, len(x) + n))[:len(x)]
    wet *= np.std(x) / (np.std(wet) + 1e-9)
    return x + amount * wet

def osc(freq_curve, harm, decay_h=0.0):
    ph = 2 * np.pi * np.cumsum(freq_curve) / sr
    t = np.arange(len(freq_curve)) / sr
    s = np.zeros_like(ph)
    for h, a in enumerate(harm, 1):
        if np.max(freq_curve) * h > 10000: break
        s += a * np.sin(h * ph) * np.exp(-decay_h * (h - 1) * t)
    return s

def mf(m, cents=0.0): return 440 * 2 ** ((m - 69 + cents / 100) / 12)

def pad(m, dur, v, rng):
    n = int(sr * (dur + 0.6)); t = np.arange(n) / sr
    saw = [1 / h for h in range(1, 14)]
    s = 0.5 * osc(np.full(n, mf(m, -7)), saw) + 0.5 * osc(np.full(n, mf(m, 7)), saw)
    env = np.minimum(1, t / 0.15) * np.where(t < dur, 1.0, np.exp(-(t - dur) * 6))
    return s * env * v * 0.18

def lead(m, dur, v, rng, prev=None):
    n = int(sr * (dur + 0.25)); t = np.arange(n) / sr
    cents = np.where(t < 0.04, -60 * (1 - t / 0.04), 0.0)                       # scoop into the note
    cents += np.where(t > 0.18, 22 * np.sin(2 * np.pi * 5.5 * (t - 0.18)) * np.minimum(1, (t - 0.18) / 0.2), 0)  # vibrato
    f = mf(m) * 2 ** (cents / 1200)
    s = osc(f, [1, 0.5, 0.35, 0.2, 0.12, 0.08])
    env = np.minimum(1, t / 0.008) * np.where(t < dur, 1.0, np.exp(-(t - dur) * 25))
    return s * env * v * 0.35

def keys(m, dur, v, rng):
    n = int(sr * (dur + 1.5)); t = np.arange(n) / sr
    f0 = mf(m); s = np.zeros(n)
    for h in range(1, 12):
        fh = f0 * h * np.sqrt(1 + 0.0004 * h * h)
        if fh > 10000: break
        s += (0.7 ** (h - 1)) * np.sin(2 * np.pi * fh * t + rng.uniform(0, 6)) * np.exp(-t * (1.2 + 0.6 * h))
    env = np.minimum(1, t / 0.003) * np.where(t < dur, 1.0, np.exp(-(t - dur) * 8))
    return s * env * v * 0.3

def bass(m, dur, v, rng, glide_from=None):
    n = int(sr * (dur + 0.15)); t = np.arange(n) / sr
    f = np.full(n, mf(m))
    if glide_from is not None:
        g = np.minimum(1, t / 0.06); f = mf(glide_from) * (1 - g) + mf(m) * g
    s = osc(f, [1, 0.45, 0.2, 0.1])
    env = np.minimum(1, t / 0.006) * np.where(t < dur, np.exp(-t * 0.8), np.exp(-dur * 0.8) * np.exp(-(t - dur) * 30))
    return s * env * v * 0.5

def drums(x, bpm, bars, rng, truth):
    beat = 60 / bpm
    def add(sig, t):
        i = int(t * sr); k = min(len(sig), len(x) - i); x[i:i + k] += sig[:k]
    for b in range(4 * bars):
        t0 = b * beat
        tt = np.arange(int(sr * 0.3)) / sr
        if b % 2 == 0:
            add(np.sin(2 * np.pi * np.cumsum(50 + 100 * np.exp(-tt * 30)) / sr) * np.exp(-tt * 9) * 1.1, t0)
            truth.append(dict(layer='drums', pitch=36, start=t0, end=t0 + 0.1, vel=1))
        else:
            add((rng.standard_normal(len(tt)) * np.exp(-tt * 20) * 0.7), t0)
            truth.append(dict(layer='drums', pitch=38, start=t0, end=t0 + 0.1, vel=1))
        for e in range(2):
            th = np.arange(int(sr * 0.05)) / sr
            add(np.diff(rng.standard_normal(len(th)), prepend=0) * np.exp(-th * 90) * (0.35 if e == 0 else 0.2), t0 + e * beat / 2)
            truth.append(dict(layer='drums', pitch=42, start=t0 + e * beat / 2, end=t0 + e * beat / 2 + 0.05, vel=1))

def song(name, bpm, bars, parts, seed, rev=0.25):
    rng = np.random.default_rng(seed)
    beat = 60 / bpm; L = int(sr * (beat * 4 * bars + 1.5)); x = np.zeros(L); truth = []
    def add(sig, t):
        i = int(t * sr); k = min(len(sig), L - i); x[i:i + k] += sig[:k]
    prog = [[60, 64, 67, 71], [57, 60, 64, 67], [53, 57, 60, 64], [55, 59, 62, 65]]
    roots = [48, 45, 41, 43]
    if 'pad' in parts:
        for b in range(bars):
            for m in prog[b % 4]:
                d = beat * 4 - 0.05
                add(pad(m, d, 0.8, rng), b * beat * 4)
                truth.append(dict(layer='chords', pitch=m, start=b * beat * 4, end=b * beat * 4 + d, vel=0.8))
    if 'keys' in parts:  # sustain pedal: arpeggio notes held to the end of the bar
        for b in range(bars):
            ch = prog[b % 4]
            for i, m in enumerate(ch + [ch[0] + 12, ch[2] + 12]):
                st = b * beat * 4 + i * beat / 2; d = b * beat * 4 + beat * 4 - st - 0.03
                v = rng.uniform(0.5, 1.0)
                add(keys(m, d, v, rng), st)
                truth.append(dict(layer='chords', pitch=m, start=st, end=st + d, vel=v))
    if 'lead' in parts:  # legato line, mixed note lengths
        t = 0.0; total = beat * 4 * bars
        scale = [72, 74, 76, 77, 79, 81, 83, 84]
        lens = [0.5, 0.5, 1, 1, 1.5, 2, 3]
        while t < total - beat * 0.5:
            dur_b = lens[rng.integers(len(lens))]; d = min(dur_b * beat, total - t)
            m = int(scale[rng.integers(len(scale))]); v = rng.uniform(0.6, 1.0)
            if rng.random() < 0.15:  # rest
                t += beat * 0.5; continue
            add(lead(m, d - 0.01, v, rng), t)
            truth.append(dict(layer='melody', pitch=m, start=t, end=t + d - 0.01, vel=v))
            t += d
    if 'bass' in parts:
        prev = None
        for b in range(bars):
            for half in range(2):
                m = roots[b % 4] - 12 + (7 if half == 1 else 0) - 12 * (half == 1 and False)
                m = roots[b % 4] - 12 if half == 0 else roots[b % 4] - 5
                st = b * beat * 4 + half * beat * 2; d = beat * 2 - 0.04
                add(bass(m, d, 0.8, rng, glide_from=prev if rng.random() < 0.4 else None), st)
                truth.append(dict(layer='bass', pitch=m, start=st, end=st + d, vel=0.8)); prev = m
    x = reverb(x, rev, seed=seed) if rev > 0 else x
    if 'drums' in parts:
        drums(x, bpm, bars, rng, truth)
    write(name, x, truth, bpm)

song('pad_lead_rev', 92, 4, ['pad', 'lead'], 11)
song('keys_pedal_rev', 100, 4, ['keys'], 12, rev=0.3)
song('lead_solo', 120, 4, ['lead'], 13, rev=0.15)
song('full_mix', 96, 4, ['pad', 'lead', 'bass', 'drums'], 14)
