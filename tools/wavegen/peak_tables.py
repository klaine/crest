"""Crest's tables for the Novation Peak's wavetable names (plan/18).

The Peak's 60 factory wavetables are Novation's sampled audio and are not available to Crest. These
are Crest's own tables, one per Peak name, designed from the name and its character so that a
converted Peak patch lands on a table of the same kind: a vowel where the Peak had a vowel, octaves
where it had octaves, a bass sine where it had a bass sine. "Organ" and "Harsh" reuse Crest's tables
of those names. Each builder maps the frame position u (0..1) and frame index f to (amps, phases)
for harmonics 0..H0, like the builders in wavegen.py. None of them reproduces a Peak table.
"""
import math
import random


def make(H0, spectrum_from_cycle, sines, bump, odd):
    H = range(1, H0 + 1)

    def spec(fn):
        return [0.0] + [max(0.0, fn(h)) for h in H]

    def cycle(fn):
        return spectrum_from_cycle([fn(n / 4096.0) for n in range(4096)])

    def lerp(a, b, x):
        return [p * (1 - x) + q * x for p, q in zip(a, b)]

    def seq(frames, u):
        """Crossfade through a list of spectra (or (amps, phases) pairs) across the table."""
        pos = u * (len(frames) - 1)
        i = min(int(pos), len(frames) - 2)
        return lerp(frames[i], frames[i + 1], pos - i)

    def rnd_amps(seed, tilt=0.5, floor=0.0):
        r = random.Random(seed)
        return [0.0] + [(floor + (1 - floor) * r.random() ** 2) * h ** -tilt for h in H]

    def rnd_phases(seed):
        r = random.Random(seed)
        return [0.0] + [r.random() * 2 * math.pi for _ in H]

    def formants(centers, widths, gains, src=lambda h: 1.0 / h, base=0.04):
        return spec(lambda h: src(h) * (base + sum(g * bump(h, c, w) for c, w, g in zip(centers, widths, gains))))

    # vowels as formant centres in harmonics of a ~110-220 Hz voice (F1, F2, F3)
    V = {"a": (7, 11, 25), "e": (4, 18, 25), "i": (2.5, 21, 28), "o": (4.5, 8, 24), "u": (3, 7, 22),
         "y": (2.5, 18, 22), "m": (2, 9, 24)}

    def vowel(vs, u, src=lambda h: 1.0 / h, w=(1.6, 2.6, 3.5), g=(1.0, 0.7, 0.35)):
        pos = u * (len(vs) - 1)
        i = min(int(pos), len(vs) - 2)
        x = pos - i
        c = [V[vs[i]][k] * (1 - x) + V[vs[i + 1]][k] * x for k in range(3)]
        return formants(c, w, g, src)

    PH = sines()
    RP = {s: rnd_phases(s) for s in range(1000, 1010)}
    B = {}

    def t(name):
        def deco(fn):
            B[name] = fn
            return fn
        return deco

    @t("BS sine")
    def _(u, f):  # a bass sine growing low overtones
        a = [0.0] * (H0 + 1)
        a[1], a[2], a[3], a[4], a[5] = 1.0, 0.55 * u, 0.3 * u * u, 0.15 * u ** 3, 0.06 * u ** 3
        return a, PH

    @t("Random")
    def _(u, f):  # four unrelated random spectra, crossfaded
        return seq([rnd_amps(s, 0.6) for s in (31, 32, 33, 34)], u), RP[1000]

    @t("Zing")
    def _(u, f):  # a ringing peak sweeping up a bright spectrum
        return spec(lambda h: h ** -0.7 * (0.3 + 4 * bump(h, 6 + 40 * u, 2.5))), PH

    @t("Tubey")
    def _(u, f):  # a sine through a tube-like stage, driven harder across the table
        k = 0.6 + 7 * u
        return cycle(lambda x: math.tanh(k * math.sin(2 * math.pi * x) + 0.25 * k * 0.3) - math.tanh(0.25 * k * 0.3))

    @t("Octaves")
    def _(u, f):  # only octave partials, the weight moving upward
        a = [0.0] * (H0 + 1)
        for i, h in enumerate((1, 2, 4, 8, 16, 32, 64)):
            a[h] = math.exp(-((i - 5 * u) ** 2) / 2.2)
        return a, PH

    @t("Wobbler")
    def _(u, f):  # a sine phase-wobbled by its 2nd harmonic
        d = 3.2 * u
        return cycle(lambda x: math.sin(2 * math.pi * x + d * math.sin(4 * math.pi * x)))

    CHORDS = [(4, 5, 6), (10, 12, 15), (8, 9, 12), (4, 5, 6, 7), (8, 10, 12, 15)]

    @t("Chords")
    def _(u, f):  # triads and sevenths as harmonic sets, an octave stacked
        frames = []
        for ch in CHORDS:
            a = [0.0] * (H0 + 1)
            for k in ch:
                for o in (1, 2, 4):
                    if k * o <= H0:
                        a[k * o] += 1.0 / o
            a[1] = 0.25
            frames.append(a)
        return seq(frames, u), PH

    @t("Didgery")
    def _(u, f):  # a drone with a low formant pair walking up
        return formants((2 + 5 * u, 9 + 9 * u), (1.2, 2.5), (1.0, 0.6), lambda h: h ** -0.6), PH

    @t("E. Piano")
    def _(u, f):  # tine: a strong fundamental, a few soft harmonics, a bell partial above
        return spec(lambda h: (1.0 if h == 1 else 0.35 * h ** -1.4) + 0.5 * u * bump(h, 14, 1.2)), PH

    @t("VoxOooEe")
    def _(u, f):
        return vowel("oue", u), PH

    @t("VoxYahEe")
    def _(u, f):
        return vowel("yae", u), PH

    @t("Winds")
    def _(u, f):  # a flute-like fundamental with a breath bed rising
        r = random.Random(77)
        return spec(lambda h: (1.0 if h == 1 else 0.0) + 0.3 * bump(h, 2, 0.6) + (0.04 + 0.25 * u) * r.random() * h ** -0.35), RP[1001]

    @t("SoftClav")
    def _(u, f):  # a plucked string seen from a pickup moving along it
        p = 0.08 + 0.35 * u
        return spec(lambda h: abs(math.sin(math.pi * h * p)) / h * math.exp(-h * 0.02)), PH

    @t("String")
    def _(u, f):  # a bowed saw with body resonances
        return spec(lambda h: (1.0 / h) * (0.6 + 0.5 * bump(h, 3, 1) + 0.4 * bump(h, 7 + 3 * u, 2) + 0.3 * bump(h, 13 + 6 * u, 3)) * math.exp(-h * (0.03 - 0.02 * u))), PH

    @t("BassOrgn")
    def _(u, f):  # bass drawbars: 16' 8' 5 1/3' 4', more pulled out across the table
        a = [0.0] * (H0 + 1)
        for h, s in ((1, 1.0), (2, 0.8), (3, 0.55 * u + 0.1), (4, 0.4 * u), (6, 0.25 * u * u), (8, 0.15 * u * u)):
            a[h] = s
        return a, PH

    @t("Acid")
    def _(u, f):  # a resonant low-passed saw, cutoff rising
        c = 2 + 34 * u
        return spec(lambda h: (1.0 / h) * (1.0 / (1 + (h / c) ** 4)) * (1 + 5 * bump(h, c, 1.4))), PH

    @t("Buzzy")
    def _(u, f):  # a pulse narrowing to a buzz
        w = 0.22 - 0.19 * u
        return spec(lambda h: abs(math.sin(math.pi * h * w)) / h), [0.0] + [-math.pi * h * w for h in H]

    @t("Carousel")
    def _(u, f):  # a calliope: a flute tone whose bright partial goes round
        c = 2 + 10 * (0.5 - 0.5 * math.cos(2 * math.pi * u))
        return spec(lambda h: (1.0 if h == 1 else 0.0) + (0.4 / h if odd(h) else 0.05 / h) + 0.5 * bump(h, c, 0.8)), PH

    @t("Choral")
    def _(u, f):  # a choir: ah to oh with soft random phases
        return vowel("aoa", u, lambda h: h ** -0.7, w=(2.2, 3, 4)), RP[1002]

    @t("Climbing")
    def _(u, f):  # one bright harmonic climbing the series over a soft base
        c = 1 + 47 * u
        return spec(lambda h: 0.25 / h + bump(h, c, 0.9)), PH

    @t("CoinFlip")
    def _(u, f):  # two random spectra, swapped frame by frame (heads, tails)
        return (rnd_amps(41, 0.5) if f % 2 == 0 else rnd_amps(42, 0.9)), RP[1003]

    @t("Deep")
    def _(u, f):  # dark and hollow: a triangle-like odd series with a low resonance coming up
        return spec(lambda h: (h ** -1.9 if odd(h) else 0.08 * h ** -2) * (1 + 1.5 * u * bump(h, 3, 0.8))), PH

    @t("Dub")
    def _(u, f):  # a sub sine filling out into a soft square
        return spec(lambda h: (1.0 if h == 1 else 0.0) + (u / h ** 1.3 if odd(h) and h > 1 else 0.0)), PH

    @t("Eee")
    def _(u, f):  # an "ee" getting brighter
        return formants((2.5, 20 + 4 * u, 27), (1.2, 2.5 + 2 * u, 3), (1.0, 0.5 + 0.6 * u, 0.3), lambda h: h ** -0.8), PH

    PRIMES = (1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47)

    @t("Eris")
    def _(u, f):  # sparse prime partials: hollow and metallic
        a = [0.0] * (H0 + 1)
        for i, p in enumerate(PRIMES):
            a[p] = math.exp(-i * (0.55 - 0.4 * u))
        return a, RP[1004]

    @t("Flame")
    def _(u, f):  # bright noise-like spectrum that flickers frame to frame
        r = random.Random(300 + f)
        return spec(lambda h: h ** -(1.3 - 0.8 * u) * (0.5 + r.random())), RP[1005]

    @t("Further")
    def _(u, f):  # hollow odd harmonics gaining the even ones and air
        return spec(lambda h: h ** -1.1 * (1.0 if odd(h) else u) * (1 + 0.6 * u * bump(h, 24, 8))), PH

    @t("GlassSaw")
    def _(u, f):
        return spec(lambda h: 1.0 / h + 0.7 * bump(h, 18 + 30 * u, 5) * h ** -0.3), PH

    @t("Glassy")
    def _(u, f):  # glass-harmonica: a pure tone with a soft cluster high up
        return spec(lambda h: (1.0 if h == 1 else 0.0) + 0.6 * bump(h, 5 + 8 * u, 1.5) * (1.0 if odd(h) else 0.4)), PH

    @t("Granular")
    def _(u, f):  # unrelated short-lived grains: a new random spectrum every frame, smoothed
        return lerp(rnd_amps(500 + f, 0.6), rnd_amps(501 + f, 0.6), 0.5), rnd_phases(600 + f)

    @t("Grime")
    def _(u, f):  # a saw crushed to fewer and fewer levels
        steps = 64 * (1 - u) + 3
        return cycle(lambda x: math.floor((2 * x - 1) * steps) / steps)

    @t("Drow")
    def _(u, f):  # a dark drone, odd and even balanced, a slow low formant
        return formants((1.5 + 3 * u, 6 + 4 * u), (1.0, 2.0), (1.0, 0.4), lambda h: h ** -1.2), PH

    @t("Heavy")
    def _(u, f):  # saw and square stacked, the low harmonics pushed
        return spec(lambda h: (1.0 / h) * (1 + (0.8 if odd(h) else 0.2) * u) * (1 + 1.2 * bump(h, 2, 1.5))), PH

    @t("Hedge")
    def _(u, f):  # a jagged random step shape, finer across the table
        n = int(6 + 58 * u)
        r = random.Random(900)
        steps = [r.uniform(-1, 1) for _ in range(n)]
        return cycle(lambda x: steps[min(n - 1, int(x * n))])

    @t("Hungry")
    def _(u, f):  # a "wow": closed mouth opening
        return vowel("muoa", u, lambda h: h ** -0.6), PH

    @t("Ladders")
    def _(u, f):  # a staircase with more and more rungs
        n = 2 + int(14 * u)
        return cycle(lambda x: (math.floor(x * n) / (n - 1)) * 2 - 1 if n > 1 else 0)

    @t("Lead")
    def _(u, f):  # a hard-synced pulse, the slave ratio rising
        r = 1 + 3.5 * u
        return cycle(lambda x: 1.0 if (r * x) % 1.0 < 0.45 else -1.0)

    @t("Modeling")
    def _(u, f):  # a struck string through a small body
        p = 0.13
        return spec(lambda h: abs(math.sin(math.pi * h * p)) * h ** -0.8 * (0.5 + bump(h, 4 + 6 * u, 2) + 0.6 * bump(h, 11 + 9 * u, 3)) * math.exp(-h * 0.03)), PH

    @t("Modem")
    def _(u, f):  # two-tone digital chirps hopping between pairs
        pairs = [(6, 10), (12, 20), (8, 14), (16, 28)]
        a1, a2 = pairs[f % 4]
        a = [0.0] * (H0 + 1)
        a[1], a[a1], a[a2] = 0.3, 1.0, 0.7
        return a, PH

    @t("Monster")
    def _(u, f):  # a growl: strong low formant on a saw, even harmonics up
        return formants((3 + 3 * u, 9), (1.5, 2.5), (2.0, 0.6), lambda h: (1.0 / h) * (1.0 if odd(h) else 1.3)), PH

    @t("Screech")
    def _(u, f):  # a thin, piercing band high up
        return spec(lambda h: 0.2 / h + bump(h, 12 + 20 * u, 4) * h ** -0.1), PH

    @t("SeaBase")
    def _(u, f):  # a triangle bass taking on a saw edge
        return spec(lambda h: (1.0 / h ** 2 if odd(h) else 0.0) * (1 - u) + (1.0 / h) * u * math.exp(-h * 0.05)), PH

    @t("Shmorgan")
    def _(u, f):  # a full organ with percussion and key click coming in
        a = [0.0] * (H0 + 1)
        for h, s in ((1, 1.0), (2, 0.9), (3, 0.7), (4, 0.6), (6, 0.4 * u), (8, 0.35 * u), (10, 0.25 * u), (12, 0.2 * u), (16, 0.15 * u)):
            a[h] = s
        for h in range(20, 60):
            a[h] += 0.02 * u * u
        return a, PH

    @t("Spirals")
    def _(u, f):  # one saw spectrum whose phases turn: the shape spirals, the tone barely moves
        return spec(lambda h: 1.0 / h ** 0.9), [0.0] + [math.pi * h * h * u * 0.07 for h in H]

    @t("Steel")
    def _(u, f):  # steel string / steel drum: bright partial clusters
        return spec(lambda h: h ** -0.9 * (0.3 + bump(h, 5, 1) + 0.8 * bump(h, 9 + 4 * u, 1.2) + 0.6 * bump(h, 16 + 8 * u, 1.5))), PH

    @t("Sunrise")
    def _(u, f):  # dark to bright
        return spec(lambda h: h ** -(3.0 - 2.4 * u)), PH

    @t("Swell")
    def _(u, f):  # a pad whose harmonic cluster widens
        return spec(lambda h: h ** -0.7 * (0.05 + bump(h, 4, 1 + 10 * u))), RP[1006]

    @t("Thicker")
    def _(u, f):  # dense: everything in, random phases, the top rising
        return spec(lambda h: h ** -(0.9 - 0.5 * u)), RP[1007]

    @t("Thinner")
    def _(u, f):  # full to almost a sine
        return spec(lambda h: h ** -(0.6 + 3.4 * u)), PH

    @t("Tides")
    def _(u, f):  # a spectral ripple flowing up the series
        return spec(lambda h: h ** -0.8 * (0.55 + 0.45 * math.sin(0.35 * h - 2 * math.pi * u))), PH

    @t("Tokyo")
    def _(u, f):  # two-operator FM bell, index rising
        i = 4.0 * u
        return cycle(lambda x: math.sin(2 * math.pi * x + i * math.sin(2 * math.pi * 3 * x)))

    @t("Tops")
    def _(u, f):  # the top of the spectrum without the body
        c = 5 + 15 * u
        return spec(lambda h: h ** -0.4 * (1 - math.exp(-((h / c) ** 4))) * math.exp(-h / 70) + (0.15 if h == 1 else 0)), RP[1009]

    @t("V.Chord")
    def _(u, f):  # a sung chord: chord harmonics through moving vowels
        base = [0.0] * (H0 + 1)
        for k in (4, 5, 6, 8, 10, 12, 16, 20, 24):
            base[k] = 1.0
        base[1], base[2] = 0.3, 0.5
        v = vowel("aeo", u, lambda h: 1.0)
        return [x * y for x, y in zip(base, v)], PH

    @t("Variance")
    def _(u, f):  # saw and pulse mixtures with small changes every frame
        r = random.Random(700 + f)
        w = 0.5 - 0.4 * u
        return spec(lambda h: ((1 - u) / h + u * abs(math.sin(math.pi * h * w)) / h) * (0.8 + 0.4 * r.random())), PH

    @t("Vocaloid")
    def _(u, f):  # a - e - i - o - u with sharp formants
        return vowel("aeiou", u, lambda h: h ** -0.5, w=(1.1, 1.8, 2.5), g=(1.0, 0.8, 0.5)), PH

    @t("Vowelled")
    def _(u, f):  # a bright source through narrow vowels
        return vowel("oaei", u, lambda h: h ** -0.3, w=(1.0, 1.6, 2.2)), PH

    @t("WeirdVox")
    def _(u, f):  # formants where no mouth puts them, moving opposite ways
        return formants((12 - 9 * u, 4 + 18 * u, 30 - 10 * u), (1.5, 2, 3), (1.0, 0.8, 0.5), lambda h: h ** -0.6), RP[1008]

    @t("Yeah")
    def _(u, f):  # "ye-ah"
        return vowel("iea", u, lambda h: h ** -0.7), PH

    return B
