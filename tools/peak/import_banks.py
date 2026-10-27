#!/usr/bin/env python3
"""Convert Novation Peak SysEx banks into Crest presets (presets/peak/*.json).

    python3 tools/peak/import_banks.py [reference_novation/*.syx]   # default: all .syx in reference_novation/
    python3 tools/peak/import_banks.py --report                     # only print what is dropped/approximated

Mapping rules and every approximation are documented in plan/18-peak-import.md. Values Crest has no
parameter for are counted in the report instead of being silently ignored. Run tools/gen/gen.py afterwards.
"""
import glob
import json
import math
import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "gen"))
import params as PM            # noqa: E402
import peak_syx                # noqa: E402

BY_KEY = {p["key"]: p for p in PM.PARAMS}

# --- tables ---------------------------------------------------------------------------------------
VOICE_MODE = ["Mono", "MonoLG", "Mono2", "Poly", "Poly2"]
UNISON = [1, 2, 3, 4, 8]
LFO_WAVE = ["Tri", "SawDn", "Square", "S&H"]
FILTER = {(0, 0): "LP12", (1, 0): "LP24", (0, 1): "BP6", (1, 1): "BP12", (0, 2): "HP12", (1, 2): "HP24"}  # (slope, shape)

# Peak LFO sync table (35 steps) and arp/delay tables, as MIDI ticks at 24 PPQN (manual p.37).
LFO_TICKS = [1536, 1152, 1008, 864, 768, 720, 672, 576, 512, 480, 448, 432, 384, 320, 288, 256, 192, 144, 128, 96,
             72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2]
ARP_TICKS_SLOW_FIRST = [192, 144, 128, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2]
DELAY_TICKS = [96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2]

# Crest's LFO sync list is the Peak's table, in the same order, after "Off".
assert len(PM.LFO_SYNC) == len(LFO_TICKS) + 1
CREST_DLY_TICKS = {"1/32T": 2, "1/32": 3, "1/16T": 4, "1/16": 6, "1/8T": 8, "1/16D": 9, "1/8": 12, "1/4T": 16,
                   "1/8D": 18, "1/4": 24, "1/2T": 32, "1/4D": 36, "1/2": 48, "1/1T": 64, "1/2D": 72, "1 bar": 96}

# Peak's 60 wavetables, column-major as printed on manual p.37; index = WaveMore - 4.
PEAK_TABLES = """BS sine|Random|Zing|Tubey|Octaves|Wobbler|Chords|Didgery|Harsh|Organ|E. Piano|VoxOooEe|VoxYahEe|Winds|SoftClav|
String|BassOrgn|Acid|Buzzy|Carousel|Choral|Climbing|CoinFlip|Deep|Dub|Eee|Eris|Flame|Further|GlassSaw|
Glassy|Granular|Grime|Drow|Heavy|Hedge|Hungry|Ladders|Lead|Modeling|Modem|Monster|Screech|SeaBase|Shmorgan|
Spirals|Steel|Sunrise|Swell|Thicker|Thinner|Tides|Tokyo|Tops|V.Chord|Variance|Vocaloid|Vowelled|WeirdVox|Yeah""".replace("\n", "").split("|")
assert len(PEAK_TABLES) == 60, len(PEAK_TABLES)
# Crest has a table for every Peak wavetable name (tools/wavegen/peak_tables.py; "Organ" and "Harsh" are
# Crest's own tables of those names). They are Crest's designs, not Novation's samples.
TABLE_MAP = {n: n for n in PEAK_TABLES}
assert set(TABLE_MAP) == set(PEAK_TABLES) and set(TABLE_MAP.values()) <= set(PM.TABLE_NAMES)

# mod matrix: Peak source index -> (Crest source name, sign applied to depth)
SRC = {0: "Direct", 1: "Mod Wheel", 2: "Chan AT", 3: "Expression", 4: "Breath", 5: "Velocity", 6: "Key",
       7: "LFO 1+", 8: "LFO 1", 9: "LFO 2+", 10: "LFO 2", 11: "Amp Env", 12: "Mod Env 1", 13: "Mod Env 2",
       14: "Animate 1", 15: "Animate 2", 16: None, 17: "LFO 3+", 18: "LFO 3", 19: "LFO 4+", 20: "LFO 4",
       21: "Pitch Bend", 22: "Pitch Bend"}
SRC_NEG = {22}
DEST = {0: "Pitch (all)", 1: "Osc 1 Pitch", 2: "Osc 2 Pitch", 3: "Osc 3 Pitch", 4: "Osc 1 VSync", 5: "Osc 2 VSync",
        6: "Osc 3 VSync", 7: "Osc 1 Shape", 8: "Osc 2 Shape", 9: "Osc 3 Shape", 10: "Osc 1 Level",
        11: "Osc 2 Level", 12: "Osc 3 Level", 13: "Noise Level", 14: "Ring Level", 15: "Amp Level",
        16: "Pre-OD", 17: "Post Dist", 18: "Cutoff", 19: "Resonance", 20: "LFO 1 Rate", 21: "LFO 2 Rate",
        22: "Amp Attack", 23: "Amp Decay", 24: "Amp Release", 25: "Mod1 Attack", 26: "Mod1 Decay",
        27: "Mod1 Release", 28: "Mod2 Attack", 29: "Mod2 Decay", 30: "Mod2 Release",
        31: "FM Osc1>Osc2", 32: "FM Osc2>Osc3", 33: "FM Osc3>Osc1", 34: "FM Noise>Osc1", 35: "Osc3 > Cutoff",
        36: "Noise > Cutoff"}
PEAK_DEST_NAMES = ["0123Ptch", "Osc1Ptch", "Osc2Ptch", "Osc3Ptch", "Osc1VSnc", "Osc2VSnc", "Osc3VSnc", "Osc1Shpe",
                   "Osc2Shpe", "Osc3Shpe", "Osc1 Lev", "Osc2 Lev", "Osc3 Lev", "NoiseLev", "Ring Lev", "VcaLevel",
                   "Filt Drv", "FiltDist", "FiltFreq", "Filt Res", "Lfo1Rate", "Lfo2Rate", "AmpEnv A", "AmpEnv D",
                   "AmpEnv R", "ModEnv1A", "ModEnv1D", "ModEnv1R", "ModEnv2A", "ModEnv2D", "ModEnv2R", "FM O1>O2",
                   "FM O2>O3", "FM O3>O1", "FM Ns>O1", "O3>FiltF", "Ns>FiltF"]
FXSRC = {0: "Direct", 1: "Mod Wheel", 2: "Chan AT", 3: "Expression", 4: "Breath", 5: "Velocity", 6: "Key",
         7: "Animate 1", 8: "Animate 2", 9: None, 10: "LFO 3+", 11: "LFO 3", 12: "LFO 4+", 13: "LFO 4",
         14: "Pitch Bend", 15: "Pitch Bend"}
FXSRC_NEG = {15}
FXDEST = {0: "Dist Level", 1: "Chorus Level", 2: "Chorus Rate", 3: "Chorus Depth", 4: "Chorus Feedback",
          5: "Delay Level", 6: "Delay Time", 7: "Delay Feedback", 8: "Reverb Level", 9: "Reverb Decay",
          10: "Reverb Damping", 11: "Reverb Low Cut"}
FXDEST_NEG = {10}   # Peak "Rev LPF" opens the filter; Crest "Reverb Damping" closes it
PEAK_FXDEST_NAMES = ["Dist Lev", "Chor Lev", "ChorRate", "Chor Dep", "Chor FB", "Del Lev", "Del Time", "Del FB",
                     "Rev Lev", "Rev Time", "Rev LPF", "Rev HPF"]
# Crest destinations whose full scale is a "per-mille" of the Peak range: depth is d/63 for all of them.


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def cbrt(x):
    return math.copysign(abs(x) ** (1.0 / 3.0), x)


def nearest_tick(table, ticks):
    return min(table, key=lambda k: abs(math.log(table[k]) - math.log(ticks)))


class Conv:
    """One patch: fills `v` (Crest key -> value) and counts what had no home in `lost`."""

    def __init__(self, d):
        self.d, self.v, self.lost = d, {}, Counter()

    def set(self, key, val):
        p = BY_KEY.get(key)
        if p is None:
            self.lost["(no Crest key) " + key] += 1
            return
        if p["kind"] == "enum":
            if isinstance(val, str):
                assert val in p["opts"], (key, val)
            else:
                val = p["opts"][int(clamp(val, 0, len(p["opts"]) - 1))]
        else:
            val = clamp(val, p["mn"], p["mx"])
            if p["kind"] == "int":
                val = int(round(val))
            else:
                val = round(float(val), 4)
        self.v[key] = val

    def lose(self, what, cond=True):
        if cond:
            self.lost[what] += 1

    # ---- sections --------------------------------------------------------------------------------
    def voice(self):
        d = self.d
        self.set("v_mode", VOICE_MODE[min(d["voice_mode"], 4)])
        self.set("v_unison", UNISON[min(d["unison"], 4)])
        self.set("v_udet", d["uni_detune"] / 127.0)
        self.set("v_spread", d["uni_spread"] / 127.0)
        self.set("v_oct", d["octave"] - 64)
        self.set("v_glide", d["glide"] / 127.0 if d["glide_on"] else 0.0)
        self.set("v_drift", d["drift"] / 127.0)
        self.set("osc_reset", "Reset" if d["osc_keysync"] else "Free")
        self.set("v_div", d["divergence"] / 127.0)
        self.set("flt_div", d["f_divergence"] / 127.0)
        self.lose("pre-glide", d["preglide"] != 64)
        # patch level: 0 -> x0.5, 64 -> x1, 127 -> x2 (manual); VCA gain is a further 0..1 trim
        g = 0.5 * 4.0 ** (d["patch_level"] / 127.0)
        self.set("patch_vol", math.sqrt(g / 2.0))
        self.set("vca_gain", d["vca_gain"] / 127.0)
        # the per-osc bend ranges collapse into one global range (osc 1, else the largest)
        bends = [o["bend"] - 64 for o in d["osc"]]
        self.set("v_bend", bends[0] if bends[0] > 0 else max(bends))
        self.lose("per-osc bend range differs", len({b for b in bends}) > 1)

    def osc(self):
        d = self.d
        for i, o in enumerate(d["osc"]):
            n = "o%d_" % (i + 1)
            self.set(n + "wave", PM.WAVES[min(o["wave"], 4)])
            if o["wave"] == 4:
                self.set(n + "table", TABLE_MAP[PEAK_TABLES[clamp(o["wavemore"] - 4, 0, 59)]])
            shsrc = min(o["shsrc"], 2)
            self.set(n + "shsrc", PM.SHAPE_SRC[shsrc])
            self.set(n + "shape", abs(o["shape"] - 64) / 64.0)
            if shsrc == 1:
                self.set(n + "shamt", (o["me1_shape"] - 64) / 64.0)
            elif shsrc == 2:
                self.set(n + "shamt", (o["lfo1_shape"] - 64) / 64.0)
            self.set(n + "vsync", o["vsync"] / 127.0)
            self.set(n + "dens", o["dens"] / 127.0)
            self.set(n + "ddet", o["ddet"] / 127.0)
            # pitch: range (-1..+2 oct) + coarse (+-12 st over 0..255) + fine (+-100 ct) -> semitones + cents
            semis = (o["range"] - 64) * 12 + (o["coarse"] - 128) * 12.0 / 128.0
            cents = (o["fine"] - 128) + (semis - round(semis)) * 100.0
            semis = round(semis)
            self.lose("osc pitch beyond +-36 st", abs(semis) > 36)
            self.set(n + "coarse", semis)
            self.set(n + "fine", clamp(cents, -100, 100))
            # Mod Env 2 depth is linear on the Peak: 8 steps = 1 octave, full (63) = 8 octaves (manual, Oscillators)
            self.set(n + "penv", cbrt((o["me2_pitch"] - 64) * 1.5 / 96.0))
            # LFO 2 Depth: up to +-5 octaves, finer at low values (manual p.18). Crest's knob law is
            # 60 st * x^3, taken to be the Peak's taper: the common vibrato settings (12..21 of 127)
            # then give 5..27 cents.
            self.set(n + "plfo", clamp((o["lfo2_pitch"] - 128) / 127.0, -1.0, 1.0))
            self.set(n + "fixed", o["fixed"])
        # mixer
        self.set("mx_o1", d["mx_o1"] / 255.0)
        self.set("mx_o2", d["mx_o2"] / 255.0)
        self.set("mx_o3", d["mx_o3"] / 255.0)
        self.set("mx_ring", d["mx_ring"] / 255.0)
        self.set("mx_noise", d["mx_noise"] / 255.0)
        # noise colour: Peak has an LP (127 = open) and HP (0 = off); Crest has one bipolar tilt control
        self.set("noise_col", -(1.0 - d["noise_lpf"] / 127.0) if d["noise_lpf"] < 127 else d["noise_hpf"] / 127.0)

    def filter(self):
        d = self.d
        self.set("flt_type", FILTER[(clamp(d["f_slope"], 0, 1), clamp(d["f_shape"], 0, 2))])
        self.set("flt_cut", d["f_freq"] / 255.0)
        self.set("flt_res", d["f_res"] / 127.0)
        self.set("flt_key", d["f_keytrack"] / 127.0)
        self.set("pre_od", d["f_overdrive"] / 127.0)
        self.set("post_dist", d["f_postdrive"] / 127.0)
        self.set("flt_env", cbrt((d["f_modenv1"] - 64) / 63.0))
        self.set("flt_ae", cbrt((d["f_ampenv"] - 64) / 63.0))
        # LFO 1 depth: up to eight octaves (manual p.24) on the same 96 st * x^3 law, fine near zero
        self.set("flt_lfo", clamp((d["f_lfo1"] - 128) / 127.0, -1.0, 1.0))
        self.set("flt_o3", d["f_osc3"] / 255.0)

    def envelopes(self):
        d = self.d
        for pre, src in (("ae", "ae"), ("m1", "me1"), ("m2", "me2")):
            for k in "adsr":
                self.set("%s_%s" % (pre, k), d["%s_%s" % (src, k)] / 127.0)
            vel = d[src + "_vel"] - 64                  # -64..+63
            self.set(pre + "_vel", vel / (63.0 if vel > 0 else 64.0))
            self.set(pre + "_trig", "Multi" if d[src + "_trig"] else "Single")  # MonoTrig: Re-Trig / Legato
            self.set(pre + "_hold", d[src + "_hold"] / 127.0)                   # 127 = 500 ms on both
            self.set(pre + "_rep", min(d[src + "_rep"], 127))                   # Off, 1..126, On (127)

    def lfos(self):
        for i, l in enumerate(self.d["lfo"]):
            n = "l%d_" % (i + 1)
            self.set(n + "wave", LFO_WAVE[min(l["wave"], 3)])
            if l["range"] == 2:                         # Sync
                self.set(n + "sync", PM.LFO_SYNC[1 + min(l["sync"], 34)])
            else:                                       # Low 0.02..200 Hz, High x8 (to 1.6 kHz): the same curves
                self.set(n + "range", "High" if l["range"] == 1 else "Low")
                self.set(n + "rate", l["rate"] / 255.0)
                self.lose("lfo rate above the 1.24 kHz cap", l["range"] == 1 and 0.16 * 10000 ** (l["rate"] / 255.0) > 1240)
            if l["phase"] > 0:                          # a fixed start phase implies a key-triggered LFO
                self.set(n + "trig", "Key")
                self.set(n + "phase", (l["phase"] - 1) / 120.0)
            if l["common"]:                             # shared by all voices; a fixed phase restarts it per note
                self.set(n + "trig", "CommonKey" if l["phase"] > 0 else "Common")
            self.set(n + "rep", min(l["repeats"], 127))
            self.set(n + "slew", l["slew"] / 127.0)
            self.set(n + "fade", l["fade"] / 127.0)
            self.set(n + "fmode", PM.FADE_MODES[min(l["fademode"], 3)])
            self.set(n + "mtrig", PM.MONO_TRIG[1 if l["montrig"] else 0])
            self.set(n + "fsync", "On" if l["fadesync"] else "Off")
        # LFO 3/4 (global): waveform, one wide rate range (0..127), sync 0 = Off else the Peak table
        for i, l in enumerate(self.d["glfo"]):
            n = "l%d_" % (i + 3)
            self.set(n + "wave", LFO_WAVE[min(l["wave"], 3)])
            self.set(n + "rate", l["rate"] / 127.0)
            if l["sync"] > 0:
                self.set(n + "sync", PM.LFO_SYNC[min(l["sync"], 35)])

    def matrix(self):
        d = self.d
        slots = []
        for i, m in enumerate(d["mm"]):
            if m["dest"] == 0 and m["s1"] == 0 and m["s2"] == 0 and m["depth"] == 64:
                continue
            if m["depth"] == 64:
                continue                                 # zero depth
            s1, s2 = SRC.get(m["s1"]), SRC.get(m["s2"])
            dst = DEST.get(m["dest"])
            if dst is None or (s1 is None) or (s2 is None and m["s2"] != 0):
                self.lose("matrix dest/src %s <- %s" % (PEAK_DEST_NAMES[min(m["dest"], 36)], m["s1"]
                          if s1 is None else "FM/other"), True)
                continue
            dep = (m["depth"] - 64) / 63.0
            if m["s1"] in SRC_NEG:
                dep = -dep
            if m["s2"] in SRC_NEG:
                dep = -dep
            slots.append((s1, s2 or "Off", dst, dep))
        for m in d["fxmm"]:
            if m["depth"] == 64:
                continue
            s1, s2, dst = FXSRC.get(m["s1"]), FXSRC.get(m["s2"]), FXDEST.get(m["dest"])
            if dst is None or s1 is None or (s2 is None and m["s2"] != 0):
                self.lose("fx matrix dest %s" % PEAK_FXDEST_NAMES[min(m["dest"], 11)])
                continue
            dep = (m["depth"] - 64) / 63.0
            if m["s1"] in FXSRC_NEG:
                dep = -dep
            if m["s2"] in FXSRC_NEG:
                dep = -dep
            if m["dest"] in FXDEST_NEG:
                dep = -dep
            slots.append((s1, s2 or "Off", dst, dep))
        if len(slots) > 20:
            self.lose("matrix slots beyond 20", True)
            slots = slots[:20]
        for i, (s1, s2, dst, dep) in enumerate(slots, 1):
            self.set("mm%02d_s1" % i, s1)
            self.set("mm%02d_s2" % i, s2)
            self.set("mm%02d_dst" % i, dst)
            self.set("mm%02d_dep" % i, clamp(dep, -1, 1))

    def fx(self):
        d = self.d
        off = d["fx_bypass"] == 1
        lvl = lambda v: 0.0 if off else v / 127.0
        self.set("dst_lvl", d["fx_dist_lvl"] / 127.0)   # the bypass button does not bypass the distortion
        self.set("cho_lvl", lvl(d["cho_lvl"]))
        self.set("cho_type", PM.CHORUS_TYPES[min(d["cho_type"], 2)])
        self.set("cho_rate", d["cho_rate"] / 127.0)
        self.set("cho_depth", d["cho_depth"] / 127.0)
        self.set("cho_fb", (d["cho_fb"] - 64) / 64.0)
        self.set("dly_lvl", lvl(d["dly_lvl"]))
        if d["dly_sync"]:
            self.set("dly_sync", nearest_tick(CREST_DLY_TICKS, DELAY_TICKS[min(d["dly_synctime"], 15)]))
        else:
            self.set("dly_sync", "Off")
            # the Peak's longest delay is 1.4 s (manual p.31), Crest's 2 s: keep 127 = 1.4 s on the same
            # exponential law from 10 ms (the Peak's own curve is not published)
            secs = 0.010 * 140.0 ** (d["dly_time"] / 127.0)
            self.set("dly_time", math.log(secs / 0.010) / math.log(200.0))
        self.set("dly_fb", d["dly_fb"] / 127.0)
        # LP Damp is an amount of damping like HP Damp (0 = none, the HP Damp default): more damping is a
        # lower loop low-pass. The Peak's default 85 gives about 2.7 kHz.
        self.set("dly_lp", 1.0 - d["dly_lp"] / 127.0)
        self.set("dly_hp", d["dly_hp"] / 127.0)
        # FX-menu extras (web UI only in Crest). Byte 220 (L/R Ratio) is inferred: 0..9 with 0 (1/1) the
        # most common, between Time and Width as in the manual's list.
        self.set("dly_ratio", PM.DELAY_RATIOS[min(d["dly_ratio"], 10)])
        self.set("dly_width", d["dly_width"] / 127.0)
        self.set("dly_slew", d["dly_slew"] / 127.0)
        self.set("cho_lp", d["cho_lp"] / 127.0)
        self.set("cho_hp", d["cho_hp"] / 127.0)
        self.set("rev_lpf", d["rev_lowpass"] / 127.0)
        self.set("rev_hpf", d["rev_highpass"] / 127.0)
        self.set("rev_mdep", d["rev_mod"] / 127.0)
        self.set("rev_mrate", d["rev_modrate"] / 127.0)
        self.set("rev_lvl", lvl(d["rev_lvl"]))
        self.set("rev_type", PM.REVERB_TYPES[min(d["rev_type"], 2)])
        self.set("rev_decay", d["rev_time"] / 127.0)
        self.set("rev_damp", d["rev_lp"] / 127.0)
        self.set("rev_hp", d["rev_hp"] / 127.0)
        self.set("rev_size", d["rev_size"] / 127.0)
        self.set("rev_pre", d["rev_pre"] / 127.0)
        # an effect counts as in use if its level is up or the FX matrix moves its level
        fx_mod = any(m["depth"] != 64 and m["dest"] in (1, 5, 8) for m in d["fxmm"])
        active = not off and (d["dly_lvl"] or d["rev_lvl"] or d["cho_lvl"] or fx_mod)
        # Peak's dry level 0 with no effect in use would be silence in Crest's blend: only honour it when
        # there is something to hear
        self.set("fx_wet", d["fx_wet"] / 127.0 if active else 1.0)
        self.set("fx_dry", d["fx_dry"] / 127.0 if active else 1.0)
        self.set("fx_route", PM.FX_ROUTES[min(d["fx_routing"], 6)])

    def arp(self):
        d = self.d
        self.set("arp_on", "On" if d["arp_on"] else "Off")
        self.set("arp_mode", PM.ARP_MODES[min(d["arp_type"], 6)])
        self.set("arp_oct", d["arp_oct"] + 1)
        self.set("arp_rhy", d["arp_rhythm"])
        self.set("arp_rate", PM.ARP_RATES[18 - clamp(d["arp_sync"], 0, 18)])
        self.set("arp_gate", d["arp_gate"] / 127.0)
        self.set("arp_swing", d["arp_swing"] / 100.0)
        self.set("arp_latch", "On" if d["arp_latch"] else "Off")
        self.set("arp_ksync", "On" if d["arp_keysync"] else "Off")
        self.set("anim1_mode", "Latch" if d["anim1_hold"] else "Hold")
        self.set("anim2_mode", "Latch" if d["anim2_hold"] else "Hold")

    def run(self):
        self.voice(); self.osc(); self.filter(); self.envelopes(); self.lfos(); self.matrix(); self.fx(); self.arp()
        return self.v


def slug(path):
    s = re.sub(r"[^a-z0-9]+", "_", os.path.splitext(os.path.basename(path))[0].lower()).strip("_")
    return s


def write_bank(path, source, presets):
    """One preset per line so the files diff and load fast."""
    with open(path, "w", encoding="utf-8") as f:
        f.write('{"source": %s, "presets": [\n' % json.dumps(source, ensure_ascii=False))
        f.write(",\n".join(json.dumps(p, separators=(",", ":"), ensure_ascii=False) for p in presets))
        f.write("\n]}\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    paths = args or sorted(glob.glob(os.path.join(ROOT, "reference_novation", "*.syx")))
    seen, lost, out_total = set(), Counter(), 0
    outdir = os.path.join(ROOT, "presets", "peak")
    os.makedirs(outdir, exist_ok=True)
    for path in paths:
        presets = []
        for d in peak_syx.read_bank(path):
            key = json.dumps({k: v for k, v in d.items() if k != "mm_sel"}, sort_keys=True)
            if key in seen or d["name"].lower().startswith("init patch"):
                continue
            seen.add(key)
            c = Conv(d)
            vals = c.run()
            lost.update(c.lost)
            presets.append({"name": d["name"][:20] or "Unnamed", "values": vals})
        out_total += len(presets)
        if "--report" not in sys.argv:
            target = os.path.join(outdir, slug(path) + ".json")
            if os.path.exists(target):  # keep the loudness calibration (tools/peak/level.py) across re-imports
                old = {p["name"]: p["values"].get("patch_vol") for p in json.load(open(target, encoding="utf-8"))["presets"]}
                for p in presets:
                    if old.get(p["name"]) is not None:
                        p["values"]["patch_vol"] = old[p["name"]]
            write_bank(target, os.path.basename(path), presets)
        print("%-34s %3d patches" % (os.path.basename(path), len(presets)))
    print("total unique patches: %d" % out_total)
    print("\nnot representable in Crest (patch counts):")
    for k, n in lost.most_common():
        print("  %5d  %s" % (n, k))


if __name__ == "__main__":
    main()
