"""Crest parameter definitions: the single source of truth (plan/11-parameter-reference.md).

gen.py turns this file into the C descriptor table, the chain_params and
ui_hierarchy contract strings, and the compiled factory bank.

Rules (plan/11):
  * Enum option order is append-only and keys are never renamed: indices are stored in presets.
  * Short names are at most 5 characters; short enum options at most 4.
  * This is the M1 "core" subset. Later milestones append keys and enum options; they never reorder.
"""

# --- enum option lists (append only) -----------------------------------------------------------
WAVES = ["Sine", "Tri", "Saw", "Pulse", "Wavetable"]
SHAPE_SRC = ["Manual", "ModEnv1", "LFO1"]
FILTER_TYPES = ["LP24", "LP12", "BP12", "BP6", "HP24", "HP12"]
ENV_TRIG = ["Multi", "Single", "Reset"]
VOICE_MODES = ["Poly", "Poly2", "Mono", "MonoLG", "Mono2"]
GLIDE_MODES = ["Always", "Legato"]
OSC_RESET = ["Free", "Reset"]

TABLE_NAMES = ["Glass", "Reed", "Vowel", "Digital", "Bell", "Organ", "PWM Sweep", "Sync Sweep", "Formant",
               "Metal", "Additive", "Sub Bass", "Harsh", "Pluck", "Choir", "Grit", "Morph"]
# Crest's own tables for the Novation Peak's wavetable names, in the Peak's order (tools/wavegen/peak_tables.py);
# the Peak's "Organ" and "Harsh" use the tables above.
PEAK_TABLE_NAMES = ["BS sine", "Random", "Zing", "Tubey", "Octaves", "Wobbler", "Chords", "Didgery", "E. Piano",
                    "VoxOooEe", "VoxYahEe", "Winds", "SoftClav", "String", "BassOrgn", "Acid", "Buzzy", "Carousel",
                    "Choral", "Climbing", "CoinFlip", "Deep", "Dub", "Eee", "Eris", "Flame", "Further", "GlassSaw",
                    "Glassy", "Granular", "Grime", "Drow", "Heavy", "Hedge", "Hungry", "Ladders", "Lead", "Modeling",
                    "Modem", "Monster", "Screech", "SeaBase", "Shmorgan", "Spirals", "Steel", "Sunrise", "Swell",
                    "Thicker", "Thinner", "Tides", "Tokyo", "Tops", "V.Chord", "Variance", "Vocaloid", "Vowelled",
                    "WeirdVox", "Yeah"]
TABLE_NAMES = TABLE_NAMES + PEAK_TABLE_NAMES
LFO_WAVES = ["Sine", "Tri", "SawUp", "SawDn", "Square", "S&H", "Rnd", "Exp"]
# The Peak's LFO sync table (manual p.38), slowest first: beats per cycle ("21.3" = 21 1/3 beats), then
# note values. Every name fits the 4-character display, so no short options are sent.
LFO_SYNC = ["Off", "64b", "48b", "42b", "36b", "32b", "30b", "28b", "24b", "21.3", "20b", "18.7", "18b", "16b",
            "13.3", "12b", "10.7", "8b", "6b", "5.3b", "4b", "3b", "2.7b", "1/2", "1/4D", "1/2T", "1/4", "1/8D",
            "1/4T", "1/8", "16D", "1/8T", "1/16", "16T", "1/32", "32T"]
LFO_RANGES = ["Low", "High"]
FADE_MODES = ["FadeIn", "FadeOut", "GateIn", "GateOut"]
MONO_TRIG = ["Legato", "Re-Trig"]
LFO_TRIG = ["Free", "Key", "Common", "CommonKey"]  # a one-shot is Key with Repeats 1
ONOFF = ["Off", "On"]
HOLD_LATCH = ["Hold", "Latch"]
MOD_SOURCES = ["Off", "Direct", "Mod Wheel", "Chan AT", "Poly AT", "Expression", "Breath", "Sustain", "Pitch Bend",
               "Velocity", "Key", "Random", "Amp Env", "Mod Env 1", "Mod Env 2", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
               "Animate 1", "Animate 2"] + ["Macro %d" % i for i in range(1, 9)] + ["LFO %d+" % i for i in (1, 2, 3, 4)]
MOD_SOURCE_SHORT = ["--", "Dir", "Whl", "CAT", "PAT", "Exp", "Brth", "Sus", "PB", "Vel", "Key", "Rnd", "AEnv",
                    "ME1", "ME2", "LFO1", "LFO2", "LFO3", "LFO4", "An1", "An2"] + ["M%d" % i for i in range(1, 9)] + ["L%d+" % i for i in (1, 2, 3, 4)]
MOD_DESTS = (["Off", "Pitch (all)"] + ["Osc %d Pitch" % i for i in (1, 2, 3)] +
             ["Osc %d Shape" % i for i in (1, 2, 3)] + ["Osc %d VSync" % i for i in (1, 2, 3)] +
             ["Osc %d Density" % i for i in (1, 2, 3)] + ["Osc %d Level" % i for i in (1, 2, 3)] +
             ["Noise Level", "Ring Level", "Pre-OD", "Cutoff", "Resonance", "Filter Drive", "Post Dist",
              "Amp Level", "Pan", "Amp Attack", "Amp Decay", "Amp Release", "Mod1 Attack", "Mod1 Decay",
              "Mod1 Release", "Mod2 Attack", "Mod2 Decay", "Mod2 Release", "LFO 1 Rate", "LFO 2 Rate",
              "LFO 3 Rate", "LFO 4 Rate", "Glide Time", "Unison Detune", "Dist Level", "Chorus Level",
              "Delay Level", "Reverb Level", "Delay Feedback", "FM Osc1>Osc2", "FM Osc2>Osc3", "FM Osc3>Osc1",
              "FM Noise>Osc1", "Osc3 > Cutoff", "Noise > Cutoff", "Delay Time", "Reverb Decay", "Chorus Rate",
              "Chorus Depth", "Chorus Feedback", "Reverb Damping", "Reverb Low Cut"])
MOD_DEST_SHORT = (["--", "Pitc", "O1Pi", "O2Pi", "O3Pi", "O1Sh", "O2Sh", "O3Sh", "O1Sy", "O2Sy", "O3Sy", "O1De",
                   "O2De", "O3De", "O1Lv", "O2Lv", "O3Lv", "Nois", "Ring", "PrOD", "Cut", "Res", "FDrv", "PstD",
                   "Amp", "Pan", "AeA", "AeD", "AeR", "M1A", "M1D", "M1R", "M2A", "M2D", "M2R", "L1Rt", "L2Rt",
                   "L3Rt", "L4Rt", "Glid", "UDet", "DsLv", "ChLv", "DlLv", "RvLv", "DlFb", "FM12", "FM23", "FM31", "FMN1", "O3Fl", "NsFl", "DlTm", "RvDc", "ChRt",
                    "ChDp", "ChFb", "RvDm", "RvHP"])
assert len(MOD_SOURCES) == len(MOD_SOURCE_SHORT) == 33 and len(MOD_DESTS) == len(MOD_DEST_SHORT) == 59
FX_ROUTES = ["Parallel", "Dly>Rev>Cho", "Dly>Cho>Rev", "Rev>Dly>Cho", "Rev>Cho>Dly", "Cho>Dly>Rev", "Cho>Rev>Dly"]
ARP_MODES = ["Up", "Down", "UpDown", "UpDown2", "Played", "Random", "Chord"]
# Order matches SCALE_MASK in src/dsp/arp.c. Set follows the Move set; the others are in C.
# Four letters at most, so they need no short_options: chain_params has no room for them.
ARP_SCALES = ["Off", "Set", "Maj", "Min", "Dor", "Phr", "Lyd", "Mix", "Pent", "MPen", "Blue"]
ARP_RATES = ["2bar", "1.5b", "2brT", "1bar", "1/2D", "1brT", "1/2", "1/4D", "1/2T", "1/4", "1/8D", "1/4T", "1/8",
             "16D", "1/8T", "1/16", "16T", "1/32", "32T"]
DIST_TYPES = ["Soft", "Hard", "Fold", "Crush"]
CHORUS_TYPES = ["Chorus 1", "Chorus 2", "Ensemble"]
DELAY_SYNC = ["Off", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
              "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/1T", "1/2D", "1 bar"]
DELAY_MODES = ["Stereo", "PingPong", "Mono"]
DELAY_RATIOS = ["1/1", "4/3", "3/4", "3/2", "2/3", "2/1", "1/2", "3/1", "1/3", "4/1", "1/4"]  # left : right time
REVERB_TYPES = ["Room", "Hall", "Plate"]

SHORT_OPT = {
    "Mono2": "Mn2",
    "Chorus 1": "Cho1", "Chorus 2": "Cho2", "Ensemble": "Ens", "PingPong": "PngP", "Stereo": "Ster",
    "UpDown": "UpDn", "UpDown2": "UpD2", "Poly2": "Ply2", "MonoLG": "MnLG", "Single": "Sngl", "Multi": "Mult", "Reset": "Rset",
    "Always": "Alws", "Wavetable": "WT", "PWM Sweep": "PWM", "Sync Sweep": "Sync", "Sub Bass": "Sub", "Legato": "Lgto", "Manual": "Man", "ModEnv1": "ME1", "Pulse": "Puls",
}


SHORT_OPT.update({"Parallel": "Par", "Dly>Rev>Cho": "DRC", "Dly>Cho>Rev": "DCR", "Rev>Dly>Cho": "RDC",
                  "Rev>Cho>Dly": "RCD", "Cho>Dly>Rev": "CDR", "Cho>Rev>Dly": "CRD"})
for _o in DELAY_SYNC[1:]:
    SHORT_OPT[_o] = "Bar" if _o == "1 bar" else _o[2:]       # "1/16T" -> "16T"
SHORT_OPT.update({"FadeIn": "FdIn", "FadeOut": "FdOt", "GateIn": "GtIn", "GateOut": "GtOt", "Re-Trig": "ReTr"})
SHORT_OPT.update({"Common": "Comm", "CommonKey": "CmKy", "Square": "Sqr", "SawUp": "SawU", "SawDn": "SawD"})


def short_opts(opts):
    if opts is MOD_SOURCES:
        return list(MOD_SOURCE_SHORT)
    if opts is MOD_DESTS:
        return list(MOD_DEST_SHORT)
    if opts is LFO_SYNC:
        return list(LFO_SYNC)
    return [SHORT_OPT.get(o, o[:4]).strip() for o in opts]


PARAMS = []


def P(key, name, short, kind, mn, mx, default, opts=None, unit=None, step=None, viz=None,
      cp=True, state=True, access=None):
    assert len(short) <= 5, (key, short)
    if kind == "enum":
        mn, mx = 0, len(opts) - 1
        if isinstance(default, str):
            default = opts.index(default)
    if step is None:
        step = 1 if kind in ("int", "enum") else 0.01
    PARAMS.append(dict(key=key, name=name, short=short, kind=kind, mn=mn, mx=mx, default=default,
                       opts=opts, unit=unit, step=step, viz=viz, cp=cp, state=state, access=access))


# --- oscillators ------------------------------------------------------------------------------
for n in (1, 2, 3):
    p = "o%d_" % n
    P(p + "wave", "Osc %d Wave" % n, "Wave", "enum", 0, 0, "Saw", opts=WAVES, viz={"kind": "waveform"})
    P(p + "table", "Osc %d Table" % n, "Table", "enum", 0, 0, "Glass", opts=TABLE_NAMES)
    P(p + "shape", "Osc %d Shape" % n, "Shape", "float", 0, 1, 0.0)
    P(p + "shsrc", "Osc %d Shape Src" % n, "ShSrc", "enum", 0, 0, "Manual", opts=SHAPE_SRC)
    P(p + "shamt", "Osc %d Shape Mod" % n, "ShMod", "float", -1, 1, 0.0)
    P(p + "dens", "Osc %d Density" % n, "Dens", "float", 0, 1, 0.0)
    P(p + "ddet", "Osc %d Dens Detune" % n, "DnDet", "float", 0, 1, 0.3)
    P(p + "vsync", "Osc %d VSync" % n, "VSync", "float", 0, 1, 0.0)
    P(p + "coarse", "Osc %d Coarse" % n, "Crse", "int", -36, 36, {1: 0, 2: 0, 3: -12}[n], unit="st")
    P(p + "fine", "Osc %d Fine" % n, "Fine", "int", -100, 100, {1: 0, 2: 7, 3: 0}[n], unit="ct")
    P(p + "penv", "Osc %d ModEnv2>Pitch" % n, "PEnv", "float", -1, 1, 0.0)
    P(p + "plfo", "Osc %d LFO2>Pitch" % n, "PLfo", "float", -1, 1, 0.0)
    P(p + "fixed", "Osc %d Fixed Note" % n, "Fixed", "int", 0, 127, 0)

# --- mixer ------------------------------------------------------------------------------------
P("mx_o1", "Osc 1 Level", "Osc1", "float", 0, 1, 1.0)
P("mx_o2", "Osc 2 Level", "Osc2", "float", 0, 1, 0.0)
P("mx_o3", "Osc 3 Level", "Osc3", "float", 0, 1, 0.0)
P("mx_noise", "Noise Level", "Noise", "float", 0, 1, 0.0)
P("mx_ring", "Ring 1x2 Level", "Ring", "float", 0, 1, 0.0)
P("noise_col", "Noise Colour", "NCol", "float", -1, 1, 0.0)
P("fm_12", "FM Osc1>Osc2", "FM12", "float", 0, 1, 0.0)
P("fm_23", "FM Osc2>Osc3", "FM23", "float", 0, 1, 0.0)
P("fm_31", "FM Osc3>Osc1", "FM31", "float", 0, 1, 0.0)
P("fm_n1", "FM Noise>Osc1", "FMN1", "float", 0, 1, 0.0)
P("pre_od", "Pre-Filter Overdrive", "PreOD", "float", 0, 1, 0.0)
P("post_dist", "Post-Filter Distortion", "PstDs", "float", 0, 1, 0.0)

# --- filter -----------------------------------------------------------------------------------
P("flt_type", "Filter Type", "Type", "enum", 0, 0, "LP24", opts=FILTER_TYPES,
  viz={"group": "flt", "role": "mode", "span": False})
P("flt_cut", "Cutoff", "Cut", "float", 0, 1, 0.75, viz={"group": "flt", "role": "cutoff", "kind": "filter"})
P("flt_res", "Resonance", "Res", "float", 0, 1, 0.1, viz={"group": "flt", "role": "resonance", "kind": "filter"})
P("flt_drive", "Filter Drive", "Drive", "float", 0, 1, 0.0)
P("flt_key", "Key Tracking", "KeyTk", "float", 0, 1, 0.5)
P("flt_env", "ModEnv1 > Cutoff", "Env1", "float", -1, 1, 0.0)
P("flt_lfo", "LFO1 > Cutoff", "LFO1", "float", -1, 1, 0.0)
P("flt_vel", "Velocity > Cutoff", "Vel", "float", 0, 1, 0.0)
P("flt_ae", "AmpEnv > Cutoff", "AEnv", "float", -1, 1, 0.0)
P("flt_o3", "Osc3 > Cutoff", "O3Fl", "float", -1, 1, 0.0)
P("flt_ns", "Noise > Cutoff", "NsFl", "float", -1, 1, 0.0)
P("flt_div", "Filter Divergence", "FDiv", "float", 0, 1, 0.0)

# --- envelopes --------------------------------------------------------------------------------
# Trigger defaults follow the Peak: the Amp Env is legato in the mono modes, the Mod Envs retrigger.
for pre, nm, sus, vel, trig in (("ae", "Amp Env", 0.8, 0.5, "Single"), ("m1", "Mod Env 1", 0.0, 0.0, "Multi"),
                                ("m2", "Mod Env 2", 0.0, 0.0, "Multi")):
    vz = lambda role: {"group": pre, "role": role, "kind": "envelope"}
    P(pre + "_a", nm + " Attack", "Atk", "float", 0, 1, 0.02, viz=vz("attack"))
    P(pre + "_d", nm + " Decay", "Dec", "float", 0, 1, 0.45, viz=vz("decay"))
    P(pre + "_s", nm + " Sustain", "Sus", "float", 0, 1, sus, viz=vz("sustain"))
    P(pre + "_r", nm + " Release", "Rel", "float", 0, 1, 0.3, viz=vz("release"))
    P(pre + "_dly", nm + " Delay", "Dly", "float", 0, 1, 0.0)
    P(pre + "_hold", nm + " Hold", "Hold", "float", 0, 1, 0.0)
    P(pre + "_vel", nm + " Velocity", "Vel", "float", -1, 1, vel)
    P(pre + "_rep", nm + " Repeats", "Rpt", "int", 0, 127, 0)  # 127 = loop until note-off
    P(pre + "_trig", nm + " Trigger", "Trig", "enum", 0, 0, trig, opts=ENV_TRIG)

# --- voice ------------------------------------------------------------------------------------
P("v_mode", "Voice Mode", "Mode", "enum", 0, 0, "Poly", opts=VOICE_MODES)
P("v_unison", "Unison Voices", "Unis", "int", 1, 8, 1)
P("v_udet", "Unison Detune", "UDet", "float", 0, 1, 0.25)
P("v_spread", "Stereo Spread", "Sprd", "float", 0, 1, 0.5)
P("v_drift", "Analog Drift", "Drift", "float", 0, 1, 0.15)
P("v_glide", "Glide Time", "Glide", "float", 0, 1, 0.0)
P("v_gmode", "Glide Mode", "GMode", "enum", 0, 0, "Always", opts=GLIDE_MODES)
P("v_bend", "Bend Range", "Bend", "int", 0, 24, 2, unit="st")
P("v_oct", "Octave", "Oct", "int", -3, 3, 0)
P("v_div", "Osc Divergence", "Dvrg", "float", 0, 1, 0.0)
P("osc_reset", "Osc Phase", "OPhs", "enum", 0, 0, "Free", opts=OSC_RESET)
P("patch_vol", "Patch Volume", "Vol", "float", 0, 1, 0.7, viz={"kind": "fader"})
P("vca_gain", "VCA Gain", "VCA", "float", 0, 1, 1.0)

# --- LFOs (1-2 per voice, 3-4 global) ----------------------------------------------------------
for n in (1, 2, 3, 4):
    p = "l%d_" % n
    P(p + "wave", "LFO %d Wave" % n, "Wave", "enum", 0, 0, "Tri", opts=LFO_WAVES)
    P(p + "rate", "LFO %d Rate" % n, "Rate", "float", 0, 1, 0.45)
    P(p + "sync", "LFO %d Sync" % n, "Sync", "enum", 0, 0, "Off", opts=LFO_SYNC)
    P(p + "phase", "LFO %d Phase" % n, "Phase", "float", 0, 1, 0.0)
    P(p + "dly", "LFO %d Delay" % n, "Dly", "float", 0, 1, 0.0)
    P(p + "fade", "LFO %d Fade Time" % n, "Fade", "float", 0, 1, 0.0)
    P(p + "slew", "LFO %d Slew" % n, "Slew", "float", 0, 1, 0.0)
    P(p + "trig", "LFO %d Mode" % n, "Mode", "enum", 0, 0, "Free", opts=LFO_TRIG)
    # LFO 3/4 are global with one wide rate range (as on the Peak): Range, MonoTrig and FadeSync are
    # per-voice settings, kept in the table so all four LFOs share a layout but not used for them.
    # chain_params has no room for everything: the cp=False keys are saved in patches and set from the
    # web UI but have no knob on the Move (the Peak has no Repeats or Fade Mode for LFO 3/4 either,
    # and FadeSync is On in all but one of the imported patches).
    per_voice = n <= 2
    P(p + "range", "LFO %d Range" % n, "Range", "enum", 0, 0, "Low", opts=LFO_RANGES, cp=per_voice)
    P(p + "rep", "LFO %d Repeats" % n, "Rpt", "int", 0, 127, 0, cp=per_voice)
    P(p + "fmode", "LFO %d Fade Mode" % n, "FMode", "enum", 0, 0, "FadeIn", opts=FADE_MODES, cp=per_voice)
    P(p + "mtrig", "LFO %d MonoTrig" % n, "MTrig", "enum", 0, 0, "Legato", opts=MONO_TRIG, cp=per_voice)
    P(p + "fsync", "LFO %d FadeSync" % n, "FSync", "enum", 0, 0, "On", opts=ONOFF, cp=False)

# --- Animate and macros ----------------------------------------------------------------------------
for n in (1, 2):
    P("anim%d" % n, "Animate %d" % n, "An%d" % n, "enum", 0, 0, "Off", opts=ONOFF, viz={"kind": "switch"})
    P("anim%d_mode" % n, "Animate %d Mode" % n, "An%dMd" % n, "enum", 0, 0, "Hold", opts=HOLD_LATCH)
for n in range(1, 9):
    P("mac%d" % n, "Macro %d" % n, "M%d" % n, "float", 0, 1, 0.0)

MM_SLOTS = 20
# --- modulation matrix: 20 raw slots are saved in patches but not published; the edit pages use the
#     proxy keys below, which read and write the selected slot (plan/07 section 6) --------------------
for n in range(1, MM_SLOTS + 1):
    P("mm%02d_s1" % n, "Slot %d Source 1" % n, "Src1", "enum", 0, 0, "Off", opts=MOD_SOURCES, cp=False)
    P("mm%02d_s2" % n, "Slot %d Source 2" % n, "Src2", "enum", 0, 0, "Off", opts=MOD_SOURCES, cp=False)
    P("mm%02d_dst" % n, "Slot %d Destination" % n, "Dest", "enum", 0, 0, "Off", opts=MOD_DESTS, cp=False)
    P("mm%02d_dep" % n, "Slot %d Depth" % n, "Depth", "float", -1, 1, 0.0, cp=False)
P("mm_sel", "Matrix Slot", "Slot", "int", 0, MM_SLOTS - 1, 0, state=False)
P("mm_slot", "Slot Number", "Slot", "int", 1, MM_SLOTS, 1, state=False)
P("mm_s1", "Source 1", "Src1", "enum", 0, 0, "Off", opts=MOD_SOURCES, state=False)
P("mm_s2", "Source 2", "Src2", "enum", 0, 0, "Off", opts=MOD_SOURCES, state=False)
P("mm_dst", "Destination", "Dest", "enum", 0, 0, "Off", opts=MOD_DESTS, state=False)
P("mm_dep", "Depth", "Depth", "float", -1, 1, 0.0, state=False)
P("mm_clear", "Clear Slot", "Clear", "enum", 0, 0, "-", opts=["-", "Clear"], state=False, access="write")

for n in range(1, 9):
    for k in range(1, 5):
        P("mac%d_d%d" % (n, k), "Macro %d Dest %d" % (n, k), "Dest", "enum", 0, 0, "Off", opts=MOD_DESTS, cp=False)
        P("mac%d_a%d" % (n, k), "Macro %d Amount %d" % (n, k), "Amt", "float", -1, 1, 0.0, cp=False)
P("mac_sel", "Macro", "Macro", "int", 0, 7, 0, state=False)
P("mac_k", "Assignment", "Assn", "int", 1, 4, 1, state=False)
P("mac_dst", "Destination", "Dest", "enum", 0, 0, "Off", opts=MOD_DESTS, state=False)
P("mac_amt", "Amount", "Amt", "float", -1, 1, 0.0, state=False)

# --- effects (algorithms adapted from Amino's fx.c, see src/dsp/fx.c) --------------------------
P("fx_route", "FX Routing", "Route", "enum", 0, 0, "Cho>Dly>Rev", opts=FX_ROUTES)
P("fx_dry", "FX Dry Level", "Dry", "float", 0, 1, 1.0)
P("fx_wet", "FX Wet Level", "Wet", "float", 0, 1, 1.0)
P("dst_lvl", "Distortion Drive", "Level", "float", 0, 1, 0.0)
P("dst_type", "Distortion Type", "Type", "enum", 0, 0, "Soft", opts=DIST_TYPES)
P("dst_tone", "Distortion Tone", "Tone", "float", 0, 1, 0.7)
P("dst_mix", "Distortion Mix", "Mix", "float", 0, 1, 1.0)

P("cho_lvl", "Chorus Level", "Level", "float", 0, 1, 0.0)
P("cho_type", "Chorus Type", "Type", "enum", 0, 0, "Chorus 1", opts=CHORUS_TYPES)
P("cho_rate", "Chorus Rate", "Rate", "float", 0, 1, 0.3)
P("cho_depth", "Chorus Depth", "Depth", "float", 0, 1, 0.5)
P("cho_dly", "Chorus Delay", "Dly", "float", 0, 1, 0.3)
P("cho_fb", "Chorus Feedback", "Fb", "float", -1, 1, 0.0)
# The Peak's FX-menu extras (manual pp.31-33). chain_params has no room left, so these have no knob on the
# Move (cp=False): they are saved in patches and set from the web UI. Defaults are the Peak's Init values.
P("cho_lp", "Chorus LoPass", "LoPas", "float", 0, 1, 0.709, cp=False)
P("cho_hp", "Chorus HiPass", "HiPas", "float", 0, 1, 0.016, cp=False)

P("dly_lvl", "Delay Level", "Level", "float", 0, 1, 0.0)
P("dly_time", "Delay Time", "Time", "float", 0, 1, 0.5)
P("dly_sync", "Delay Sync", "Sync", "enum", 0, 0, "1/8D", opts=DELAY_SYNC)
P("dly_fb", "Delay Feedback", "Fb", "float", 0, 1, 0.35)
P("dly_mode", "Delay Mode", "Mode", "enum", 0, 0, "Stereo", opts=DELAY_MODES)
P("dly_lp", "Delay Low-pass", "LP", "float", 0, 1, 0.7)
P("dly_hp", "Delay High-pass", "HP", "float", 0, 1, 0.1)
P("dly_ratio", "Delay L/R Ratio", "Ratio", "enum", 0, 0, "1/1", opts=DELAY_RATIOS, cp=False)
P("dly_width", "Delay Width", "Width", "float", 0, 1, 1.0, cp=False)
P("dly_slew", "Delay Slew Rate", "Slew", "float", 0, 1, 0.252, cp=False)

P("rev_lvl", "Reverb Level", "Level", "float", 0, 1, 0.0)
P("rev_type", "Reverb Type", "Type", "enum", 0, 0, "Hall", opts=REVERB_TYPES)
P("rev_size", "Reverb Size", "Size", "float", 0, 1, 0.6)
P("rev_decay", "Reverb Decay", "Decay", "float", 0, 1, 0.5)
P("rev_damp", "Reverb Damping", "Damp", "float", 0, 1, 0.4)
P("rev_hp", "Reverb Low Cut", "HP", "float", 0, 1, 0.15)
P("rev_pre", "Reverb Pre-delay", "Pre", "float", 0, 1, 0.04)
P("rev_lpf", "Reverb LoPass", "LoPas", "float", 0, 1, 0.583, cp=False)
P("rev_hpf", "Reverb HiPass", "HiPas", "float", 0, 1, 0.0, cp=False)
P("rev_mdep", "Reverb Mod Depth", "MDep", "float", 0, 1, 0.504, cp=False)
P("rev_mrate", "Reverb Mod Rate", "MRate", "float", 0, 1, 0.031, cp=False)

# --- arpeggiator (plan/09) ----------------------------------------------------------------------
P("arp_on", "Arp On", "Arp", "enum", 0, 0, "Off", opts=ONOFF, viz={"kind": "switch"})
P("arp_mode", "Arp Mode", "Mode", "enum", 0, 0, "Up", opts=ARP_MODES)
P("arp_rate", "Arp Rate", "Rate", "enum", 0, 0, "1/16", opts=ARP_RATES)
P("arp_rhy", "Arp Rhythm", "Rhy", "int", 0, 32, 0)
P("arp_oct", "Arp Octaves", "Oct", "int", 1, 6, 1)
P("arp_gate", "Arp Gate", "Gate", "float", 0.05, 1, 0.5)
P("arp_swing", "Arp Swing", "Swing", "float", 0.2, 0.8, 0.5)
P("arp_latch", "Arp Latch", "Latch", "enum", 0, 0, "Off", opts=ONOFF)
P("arp_ksync", "Arp Key Sync", "KSync", "enum", 0, 0, "On", opts=ONOFF)
P("arp_scale", "Arp Scale", "Scale", "enum", 0, 0, "Off", opts=ARP_SCALES)

# --- Peak banks on the Move (src/dsp/peak_bank.c): the syx_file browser is a special chain_params entry
#     (gen.py), this knob picks a patch of the loaded bank. Not saved: the patch itself is. -----------
P("syx_patch", "Peak Patch", "Patch", "int", 0, 127, 0, state=False)

# --- Move pages (ui_hierarchy) ----------------------------------------------------------------
# knobs: list of key or (key, short_override). The root level doubles as the Main page.
# (level id, label, knobs, extra) - extra holds the keys of a list level (knobs = []).
LEVELS = [
    ("perform", "Perform", ["anim1", "anim2", "anim1_mode", "anim2_mode", "patch_vol", "v_drift", "arp_on", "arp_scale"], None),
    ("macros", "Macros", ["mac1", "mac2", "mac3", "mac4", "mac5", "mac6", "mac7", "mac8"], None),
    ("osc1", "Oscillator 1", ["o1_wave", "o1_table", "o1_shape", "o1_dens", "o1_coarse", "o1_fine", "o1_vsync", "mx_o1"], None),
    ("osc1m", "Osc 1 Modulation", ["o1_shsrc", "o1_shamt", "o1_ddet", "o1_penv", "o1_plfo", "o1_fixed"], None),
    ("osc2", "Oscillator 2", ["o2_wave", "o2_table", "o2_shape", "o2_dens", "o2_coarse", "o2_fine", "o2_vsync", "mx_o2"], None),
    ("osc2m", "Osc 2 Modulation", ["o2_shsrc", "o2_shamt", "o2_ddet", "o2_penv", "o2_plfo", "o2_fixed"], None),
    ("osc3", "Oscillator 3", ["o3_wave", "o3_table", "o3_shape", "o3_dens", "o3_coarse", "o3_fine", "o3_vsync", "mx_o3"], None),
    ("osc3m", "Osc 3 Modulation", ["o3_shsrc", "o3_shamt", "o3_ddet", "o3_penv", "o3_plfo", "o3_fixed"], None),
    ("mixer", "Mixer", ["mx_o1", "mx_o2", "mx_o3", "mx_ring", "mx_noise", "noise_col", "pre_od", "post_dist"], None),
    ("filter", "Filter", ["flt_type", "flt_cut", "flt_res", "flt_drive", "flt_key", "flt_env", "flt_lfo", "flt_vel"], None),
    ("ae", "Amp Envelope", ["ae_a", "ae_d", "ae_s", "ae_r", "ae_hold", "ae_vel", "ae_rep", "ae_trig"], None),
    ("m1", "Mod Envelope 1", ["m1_a", "m1_d", "m1_s", "m1_r", "m1_hold", "m1_vel", "m1_rep", "m1_trig"], None),
    ("m2", "Mod Envelope 2", ["m2_a", "m2_d", "m2_s", "m2_r", "m2_hold", "m2_vel", "m2_rep", "m2_trig"], None),
    ("env_dly", "Envelope Delays", [("ae_dly", "AmpD"), ("m1_dly", "M1Dly"), ("m2_dly", "M2Dly")], None),
    ("lfo1", "LFO 1", ["l1_wave", "l1_range", "l1_rate", "l1_sync", "l1_phase", "l1_slew", "l1_trig", "l1_rep"], None),
    ("lfo1f", "LFO 1 Fade", ["l1_fade", "l1_fmode", "l1_dly", "l1_mtrig"], None),
    ("lfo2", "LFO 2", ["l2_wave", "l2_range", "l2_rate", "l2_sync", "l2_phase", "l2_slew", "l2_trig", "l2_rep"], None),
    ("lfo2f", "LFO 2 Fade", ["l2_fade", "l2_fmode", "l2_dly", "l2_mtrig"], None),
    ("lfo3", "LFO 3", ["l3_wave", "l3_rate", "l3_sync", "l3_phase", "l3_slew", "l3_trig", "l3_fade", "l3_dly"], None),
    ("lfo4", "LFO 4", ["l4_wave", "l4_rate", "l4_sync", "l4_phase", "l4_slew", "l4_trig", "l4_fade", "l4_dly"], None),
    ("matrix", "Mod Matrix", [], {"items_param": "mm_items", "select_param": "mm_sel", "navigate_to": "matrix_edit"}),
    ("matrix_edit", "Matrix Slot", ["mm_s1", "mm_s2", "mm_dst", "mm_dep", "mm_slot", "mm_clear"], None),
    ("macro_routing", "Macro Routing", [], {"items_param": "mac_items", "select_param": "mac_sel", "navigate_to": "macro_edit"}),
    ("macro_edit", "Macro Edit", ["mac_dst", "mac_amt", "mac_k"], None),
    ("fm", "FM and Filter FM", ["fm_12", "fm_23", "fm_31", "fm_n1", "flt_o3", "flt_ns", "flt_div", "flt_ae"], None),
    ("voice2", "Voice Character", ["osc_reset", "v_div", "v_drift", "vca_gain"], None),
    ("arp", "Arpeggiator", ["arp_mode", "arp_rate", "arp_rhy", "arp_oct", "arp_gate", "arp_swing", "arp_latch", "arp_ksync"], None),
    ("voice", "Voice", ["v_mode", "v_unison", "v_udet", "v_spread", "v_glide", "v_gmode", "v_bend", "v_oct"], None),
    ("fx_main", "FX Routing", ["fx_route", "fx_dry", "fx_wet"], None),
    ("fx_dist", "Distortion", ["dst_lvl", "dst_type", "dst_tone", "dst_mix"], None),
    ("fx_cho", "Chorus", ["cho_lvl", "cho_type", "cho_rate", "cho_depth", "cho_dly", "cho_fb"], None),
    ("fx_dly", "Delay", ["dly_lvl", "dly_time", "dly_sync", "dly_fb", "dly_mode", "dly_lp", "dly_hp"], None),
    ("fx_rev", "Reverb", ["rev_lvl", "rev_type", "rev_size", "rev_decay", "rev_damp", "rev_hp", "rev_pre"], None),
    ("peak_banks", "Peak Banks", ["syx_file", "syx_patch"], None),
]
ROOT_LABEL = "Crest"
ROOT_KNOBS = ["flt_cut", "flt_res", ("flt_env", "Env1"), "pre_od", "ae_a", "ae_d", "ae_s", "ae_r"]

# --- display curves (shared by the web UI and the on-device cards) --------------------------------
# How a stored value reads in real units. Each mirrors the DSP formula named in the comment; a key
# with no entry reads as a percentage (0..1) or a signed percentage (-1..1).
#   ["env", tmax, bend]  env.c env_time: 0.5 ms * (tmax / 0.5 ms)^(x^bend); bend 1 unless given (release 0.6)
#   ["rep", loop]    a repeat count: 0 = Off, loop (if given) = Loop
#   ["edly"]         env.c delay_time: 0 or 1 ms * 10000^x
#   ["lfo", rangekey] lfo.c: Low 0.02 Hz * 10000^x, High 8x that (the value of rangekey picks)
#   ["ldly"]         lfo.c: 0 or 5 ms * 2000^x          ["lfade"] lfo.c: 0 (off) .. 10 s
#   ["cut"]          voice.c: note 20 + 115 x, as Hz
#   ["glide"]        voice.c: 5.6 x^5 s                   ["udet"]  voice.c: +-50 x^3 cents
#   ["fm"]           voice.c: index 1.5 x^2 cycles, shown as radians
#   ["cube", s, u]   s * x^3 in unit u                  ["exp", lo, hi, u] lo * (hi/lo)^x
#   ["lin", lo, hi, u]                                  ["note"]  MIDI note, 0 = off
DISP = {}
for _n in (1, 2, 3):
    _p = "o%d_" % _n
    DISP[_p + "penv"] = ["cube", 96, "st"]
    DISP[_p + "plfo"] = ["cube", 60, "st"]
    DISP[_p + "ddet"] = ["cube", 35, "ct"]
    DISP[_p + "vsync"] = ["exp", 1, 16, "x"]
    DISP[_p + "fixed"] = ["note"]
for _k in ("fm_12", "fm_23", "fm_31", "fm_n1"):
    DISP[_k] = ["fm"]
DISP.update({"flt_cut": ["cut"], "flt_env": ["cube", 96, "st"], "flt_ae": ["cube", 96, "st"],
             "flt_lfo": ["cube", 96, "st"], "flt_vel": ["lin", 0, 48, "st"], "flt_div": ["lin", 0, 12, "st"],
             "v_udet": ["udet"], "v_glide": ["glide"], "v_div": ["lin", 0, 40, "ct"],
             "dst_tone": ["exp", 1500, 20000, "Hz"], "cho_rate": ["exp", 0.05, 8, "Hz"], "cho_dly": ["lin", 3, 30, "ms"],
             "dly_time": ["exp", 10, 2000, "ms"], "dly_lp": ["exp", 1000, 20000, "Hz"], "dly_hp": ["exp", 20, 2000, "Hz"],
             "rev_decay": ["exp", 0.2, 20, "s"],
             "cho_lp": ["exp", 500, 20000, "Hz"], "cho_hp": ["exp", 20, 1000, "Hz"], "rev_lpf": ["exp", 500, 20000, "Hz"],
             "rev_hpf": ["exp", 20, 1000, "Hz"], "rev_mrate": ["exp", 1, 32, "x"], "dly_slew": ["exp", 10, 3000, "ms"], "rev_hp": ["exp", 20, 1000, "Hz"], "rev_pre": ["lin", 0, 500, "ms"]})
for _e in ("ae", "m1", "m2"):
    for _s, _t in (("a", 20), ("d", 22), ("r", 25)):  # env.h ENV_*_MAX
        DISP["%s_%s" % (_e, _s)] = ["env", _t, 0.6] if _s == "r" else ["env", _t]  # release: x^0.6 (env.h)
    DISP[_e + "_dly"] = ["edly"]
    DISP[_e + "_hold"] = ["lin", 0, 500, "ms"]
    DISP[_e + "_rep"] = ["rep", 127]
    DISP[_e + "_vel"] = ["bip"]
for _n in (1, 2, 3, 4):
    DISP["l%d_rate" % _n] = ["lfo", "l%d_range" % _n] if _n <= 2 else ["exp", 0.02, 1600, "Hz"]
    DISP["l%d_rep" % _n] = ["rep"]
    DISP["l%d_dly" % _n] = ["ldly"]
    DISP["l%d_fade" % _n] = ["lfade"]
    DISP["l%d_phase" % _n] = ["lin", 0, 360, "deg"]

# Knobs that raise an on-device card (src/cards.js) while held or turned: the values whose meaning
# needs more than the header's number. Each costs ~29 bytes of chain_params.
CARD_KEYS = (["flt_cut", "flt_env", "flt_ae", "v_glide", "v_udet", "mm_dep", "mac_amt", "arp_rhy", "fx_route",
              "dly_time", "rev_decay", "cho_rate"] +
             ["%s_%s" % (e, s) for e in ("ae", "m1", "m2") for s in "adr"] +
             ["l%d_rate" % n for n in (1, 2, 3, 4)] +
             ["o%d_%s" % (n, k) for n in (1, 2, 3) for k in ("shape", "penv", "fixed", "table")] +
             ["fm_12", "fm_23", "fm_31", "fm_n1"])
