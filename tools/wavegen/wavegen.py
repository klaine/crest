#!/usr/bin/env python3
"""Generate Crest's factory wavetables (plan/04 section 5).

  python3 tools/wavegen/wavegen.py        writes src/dsp/wavetables.c and wavetables.h

17 original tables plus 58 for the Peak wavetable names (peak_tables.py), x 16 frames, built from harmonic spectra (a few from a time-domain cycle that is
analysed with an FFT). Every frame is stored at 9 mip levels: level k keeps H_k = 256 >> k harmonics in
L_k = max(4 H_k, 64) samples, so a table read at the right level is band-limited to Nyquist. All levels
of a frame share one gain, chosen so the frame is loud but never clips. Output is 16-bit.

Pure Python (no numpy). The result is committed, so a normal build never runs this.
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "gen"))
sys.path.insert(0, HERE)
import params as PM  # noqa: E402

FRAMES = 16
LEVELS = 9
H0 = 256
HL = [max(H0 >> k, 1) for k in range(LEVELS)]
LL = [max(4 * h, 64) for h in HL]
STRIDE = sum(LL)
SINE_PHASE = -math.pi / 2  # cos(x - pi/2) = sin(x): a natural "sine series" waveform


# --- FFT ---------------------------------------------------------------------------------------
def fft(re, im):
    n = len(re)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    length = 2
    while length <= n:
        ang = -2.0 * math.pi / length
        wr, wi = math.cos(ang), math.sin(ang)
        half = length // 2
        for i in range(0, n, length):
            cr, ci = 1.0, 0.0
            for k in range(half):
                a, b = i + k, i + k + half
                xr = re[b] * cr - im[b] * ci
                xi = re[b] * ci + im[b] * cr
                re[b], im[b] = re[a] - xr, im[a] - xi
                re[a] += xr
                im[a] += xi
                cr, ci = cr * wr - ci * wi, cr * wi + ci * wr
        length <<= 1


def cycle_from_spectrum(amps, phases, H, N):
    """x[n] = sum_{h=1..H} a_h cos(2 pi h n / N + phi_h), via one forward FFT of the conjugate."""
    re = [0.0] * N
    im = [0.0] * N
    for h in range(1, H + 1):
        re[h] = amps[h] * math.cos(phases[h])
        im[h] = -amps[h] * math.sin(phases[h])
    fft(re, im)
    return re


def spectrum_from_cycle(x):
    N = len(x)
    re, im = list(x), [0.0] * N
    fft(re, im)
    amps = [0.0] * (H0 + 1)
    phases = [0.0] * (H0 + 1)
    for h in range(1, H0 + 1):
        amps[h] = 2.0 * math.hypot(re[h], im[h]) / N
        phases[h] = math.atan2(im[h], re[h])
    return amps, phases


# --- table definitions: each returns (amps[0..H0], phases[0..H0]) for frame position u in 0..1 ---
def sines():
    return [SINE_PHASE] * (H0 + 1)


def odd(h):
    return h % 2 == 1


def bump(h, c, w):
    return math.exp(-(((h - c) / w) ** 2))


def t_glass(u, f):
    return [0.0] + [h ** -0.8 * (1 + 3 * bump(h, 3 + 20 * u, 2.5)) * (1.0 if odd(h) else 0.35)
                    for h in range(1, H0 + 1)], sines()


def t_reed(u, f):
    p, ev = 1.0 - 0.5 * u, 0.3 + 0.7 * u
    return [0.0] + [h ** -p * (1.0 if odd(h) else ev) for h in range(1, H0 + 1)], sines()


VOWELS = [(6, 11), (4, 16), (2, 22), (4, 7), (2, 5)]


def t_vowel(u, f):
    pos = u * (len(VOWELS) - 1)
    i = min(int(pos), len(VOWELS) - 2)
    fr = pos - i
    f1 = VOWELS[i][0] * (1 - fr) + VOWELS[i + 1][0] * fr
    f2 = VOWELS[i][1] * (1 - fr) + VOWELS[i + 1][1] * fr
    return [0.0] + [(1.0 / h) * (0.08 + bump(h, f1, 1.6) + 0.7 * bump(h, f2, 2.8))
                    for h in range(1, H0 + 1)], sines()


def t_digital(u, f):
    return [0.0] + [max(0.02, h ** -0.7 * (0.55 + 0.45 * math.sin(h * (0.3 + 1.7 * u))))
                    for h in range(1, H0 + 1)], sines()


def t_bell(u, f):
    parts = [1, 2, 3, 4, 5, 6, 8, 10, 13]
    a = [0.0] + [0.004 / h for h in range(1, H0 + 1)]
    for i, p in enumerate(parts):
        a[p] += math.exp(-i * (0.7 - 0.45 * u))
    return a, sines()


def t_organ(u, f):
    harm = [1, 2, 3, 4, 5, 6, 8]
    A = [1, .8, 0, .3, 0, 0, 0]
    B = [.5, .6, .8, .9, .4, .5, .3]
    a = [0.0] * (H0 + 1)
    for h, x, y in zip(harm, A, B):
        a[h] = x * (1 - u) + y * u
    return a, sines()


def t_pwm(u, f):
    w = 0.5 - 0.44 * u
    a = [0.0] * (H0 + 1)
    ph = [0.0] * (H0 + 1)
    for h in range(1, H0 + 1):
        s = math.sin(math.pi * h * w)
        a[h] = 2.0 / (math.pi * h) * abs(s)
        ph[h] = -math.pi * h * w + (math.pi if s < 0 else 0.0)
    return a, ph


def t_sync(u, f):
    N = 4096
    r = 1.0 + 6.0 * u
    x = [2.0 * ((r * n / N) % 1.0) - 1.0 for n in range(N)]
    return spectrum_from_cycle(x)


def t_formant(u, f):
    c1 = 3 + 10 * u
    c2 = 2.3 * c1
    return [0.0] + [(1.0 / h) * (0.05 + bump(h, c1, 2.0) + 0.6 * bump(h, c2, 3.0))
                    for h in range(1, H0 + 1)], sines()


def _random_spectrum(seed):
    r = random.Random(seed)
    a = [0.0] + [(r.random() ** 2) * h ** -0.4 for h in range(1, H0 + 1)]
    p = [0.0] + [r.random() * 2 * math.pi for _ in range(H0)]
    return a, p


_METAL = (_random_spectrum(11), _random_spectrum(12))


def t_metal(u, f):
    (a0, p0), (a1, _) = _METAL
    return [(1 - u) * x + u * y for x, y in zip(a0, a1)], p0


def t_additive(u, f):
    p, ev = 1.0 + u, 1.0 - u
    return [0.0] + [h ** -p * (1.0 if odd(h) else ev) for h in range(1, H0 + 1)], sines()


def t_sub(u, f):
    a = [0.0] + [0.01 / h ** 2 for h in range(1, H0 + 1)]
    a[1] += 1.0
    a[2] += 0.6 * u
    a[3] += 0.35 * u * u
    a[4] += 0.15 * u
    return a, sines()


def t_harsh(u, f):
    return [0.0] + [h ** -0.3 * (0.25 + 0.75 * abs(math.cos(math.pi * h * (0.5 + 0.5 * u))))
                    for h in range(1, H0 + 1)], sines()


def t_pluck(u, f):
    k = 0.01 + 0.2 * u
    return [0.0] + [(1.0 / h) * math.exp(-h * k) for h in range(1, H0 + 1)], sines()


_CHOIR_PH = [0.0] + [random.Random(21).random() * 2 * math.pi for _ in range(H0)]
_r = random.Random(21)
_CHOIR_PH = [0.0] + [_r.random() * 2 * math.pi for _ in range(H0)]


def t_choir(u, f):
    return [0.0] + [h ** -0.5 * (0.05 + bump(h, 3 + 2 * u, 2) + 0.8 * bump(h, 8 + 4 * u, 2.5) +
                                 0.5 * bump(h, 15 + 6 * u, 3)) for h in range(1, H0 + 1)], _CHOIR_PH


def t_grit(u, f):
    ra, rp = random.Random(100 + f), random.Random(200 + f)
    return [0.0] + [(0.4 + 0.6 * ra.random()) * h ** -0.5 for h in range(1, H0 + 1)], \
           [0.0] + [rp.random() * 2 * math.pi for _ in range(H0)]


def t_morph(u, f):
    def sine():
        a = [0.0] * (H0 + 1)
        a[1] = 1.0
        return a

    def saw():
        return [0.0] + [1.0 / h for h in range(1, H0 + 1)]

    def square():
        return [0.0] + [(1.0 / h if odd(h) else 0.0) for h in range(1, H0 + 1)]

    def pulse():
        return [0.0] + [abs(math.sin(math.pi * h * 0.125)) * 2.0 / (math.pi * h) * math.pi
                        for h in range(1, H0 + 1)]

    def bright():
        return [0.0] + [h ** -0.35 for h in range(1, H0 + 1)]

    shapes = [sine(), saw(), square(), pulse(), bright()]
    pos = u * (len(shapes) - 1)
    i = min(int(pos), len(shapes) - 2)
    fr = pos - i
    return [x * (1 - fr) + y * fr for x, y in zip(shapes[i], shapes[i + 1])], sines()


BUILDERS = {
    "Glass": t_glass, "Reed": t_reed, "Vowel": t_vowel, "Digital": t_digital, "Bell": t_bell,
    "Organ": t_organ, "PWM Sweep": t_pwm, "Sync Sweep": t_sync, "Formant": t_formant,
    "Metal": t_metal, "Additive": t_additive, "Sub Bass": t_sub, "Harsh": t_harsh,
    "Pluck": t_pluck, "Choir": t_choir, "Grit": t_grit, "Morph": t_morph,
}


import peak_tables  # noqa: E402

BUILDERS.update(peak_tables.make(H0, spectrum_from_cycle, sines, bump, odd))


# --- build ------------------------------------------------------------------------------------
def build_frame(builder, f):
    u = f / (FRAMES - 1.0)
    amps, phases = builder(u, f)
    amps = amps[:H0 + 1]
    # Gain from the full-resolution cycle: loud by RMS, but never past 0.9 of full scale.
    c0 = cycle_from_spectrum(amps, phases, HL[0], LL[0])
    peak = max(abs(v) for v in c0) or 1.0
    rms = math.sqrt(sum(v * v for v in c0) / len(c0)) or 1.0
    gain = min(0.9 / peak, 0.35 / rms)
    cycles = [c0] + [cycle_from_spectrum(amps, phases, HL[k], LL[k]) for k in range(1, LEVELS)]
    # a band-limited level can peak higher than the full one (Gibbs): never let any level clip
    top = max(max(abs(v) for v in c) for c in cycles)
    gain = min(gain, 0.97 / top)
    out = []
    for cyc in cycles:
        out.extend(max(-32767, min(32767, int(round(v * gain * 32767.0)))) for v in cyc)
    return out


def main():
    names = PM.TABLE_NAMES
    assert sorted(names) == sorted(BUILDERS), "TABLE_NAMES and BUILDERS differ"
    data = []
    for ti, name in enumerate(names):
        for f in range(FRAMES):
            data.extend(build_frame(BUILDERS[name], f))
        print("  %2d %-11s done" % (ti, name), file=sys.stderr)
    assert len(data) == len(names) * FRAMES * STRIDE

    offs, o = [], 0
    for L in LL:
        offs.append(o)
        o += L
    h = ["/* GENERATED by tools/wavegen/wavegen.py. Do not edit. */",
         "#ifndef CREST_WAVETABLES_H", "#define CREST_WAVETABLES_H", "#include <stdint.h>", "",
         "#define WT_TABLES %d" % len(names), "#define WT_FRAMES %d" % FRAMES,
         "#define WT_LEVELS %d" % LEVELS, "#define WT_FRAME_STRIDE %d" % STRIDE, "",
         "/* Frame (table t, frame f) starts at WT_DATA + (t * WT_FRAMES + f) * WT_FRAME_STRIDE; mip level k",
         " * starts WT_LEVEL_OFF[k] samples in and is WT_LEN[k] samples long (a power of two). */",
         "extern const int16_t WT_DATA[WT_TABLES * WT_FRAMES * WT_FRAME_STRIDE];",
         "extern const uint16_t WT_LEN[WT_LEVELS];", "extern const uint16_t WT_LEVEL_OFF[WT_LEVELS];", "",
         "#endif", ""]
    c = ['/* GENERATED by tools/wavegen/wavegen.py. Do not edit. */', '#include "wavetables.h"', "",
         "const uint16_t WT_LEN[WT_LEVELS] = {%s};" % ", ".join(map(str, LL)),
         "const uint16_t WT_LEVEL_OFF[WT_LEVELS] = {%s};" % ", ".join(map(str, offs)), "",
         "const int16_t WT_DATA[WT_TABLES * WT_FRAMES * WT_FRAME_STRIDE] = {"]
    for i in range(0, len(data), 24):
        c.append(",".join(map(str, data[i:i + 24])) + ",")
    c += ["};", ""]
    with open(os.path.join(ROOT, "src", "dsp", "wavetables.h"), "w") as fh:
        fh.write("\n".join(h))
    with open(os.path.join(ROOT, "src", "dsp", "wavetables.c"), "w") as fh:
        fh.write("\n".join(c))
    print("wrote %d samples (%.2f MB as int16)" % (len(data), len(data) * 2 / 1e6))


if __name__ == "__main__":
    main()
