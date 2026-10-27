/* The Peak patch converter in C. Every rule mirrors tools/peak/import_banks.py (Conv) and the byte layout
 * mirrors tools/peak/peak_syx.py; see plan/18 for where each comes from. Keep the three in step:
 * tests/peak/test_import.py compares this file's output with the Python converter's. */
#include "peak_import.h"

#include <math.h>
#include <string.h>

#include "dsp_util.h"

/* ------------------------------------------------------------------ layout (peak_syx.py) */

enum {
    B_VOICE_MODE = 48, B_UNISON = 49, B_UNI_DETUNE = 50, B_UNI_SPREAD = 51, B_OCTAVE = 52, B_GLIDE = 53,
    B_PREGLIDE = 54, B_GLIDE_ON = 55, B_DIVERGENCE = 57, B_DRIFT = 58, B_NOISE_LPF = 59, B_NOISE_HPF = 60,
    B_OSC_KEYSYNC = 61, B_PATCH_LEVEL = 135, B_VCA_GAIN = 136, B_FX_DRY = 137, B_FX_WET = 138,
    B_F_OVERDRIVE = 139, B_F_POSTDRIVE = 140, B_F_SLOPE = 141, B_F_SHAPE = 142, B_F_KEYTRACK = 143, B_F_RES = 144,
    B_F_OSC3 = 149, B_F_AMPENV = 151, B_F_MODENV1 = 152, B_F_DIVERGENCE = 153,
    B_FX_DIST_LVL = 213, B_FX_BYPASS = 215, B_FX_ROUTING = 216,
    B_DLY_LVL = 218, B_DLY_TIME = 219, B_DLY_RATIO = 220, B_DLY_WIDTH = 221, B_DLY_SYNC = 222, B_DLY_SYNCTIME = 223,
    B_DLY_FB = 224, B_DLY_LP = 225, B_DLY_HP = 226, B_DLY_SLEW = 227,
    B_REV_LVL = 231, B_REV_TYPE = 232, B_REV_TIME = 233, B_REV_LP = 234, B_REV_HP = 235, B_REV_SIZE = 236,
    B_REV_MOD = 237, B_REV_MODRATE = 238, B_REV_LOWPASS = 239, B_REV_HIGHPASS = 240, B_REV_PRE = 241,
    B_CHO_LVL = 243, B_CHO_TYPE = 244, B_CHO_RATE = 245, B_CHO_DEPTH = 246, B_CHO_FB = 247, B_CHO_LP = 248,
    B_CHO_HP = 249,
    B_ARP_SYNC = 253, B_ARP_TYPE = 254, B_ARP_RHYTHM = 255, B_ARP_OCT = 256, B_ARP_GATE = 257, B_ARP_SWING = 258,
    B_ARP_ON = 259, B_ARP_LATCH = 260, B_ARP_KEYSYNC = 261, B_ANIM1_HOLD = 263, B_ANIM2_HOLD = 264,
    W_MX_O1 = 125, W_F_FREQ = 145, W_F_LFO1 = 147, /* 0..255 pairs; the mixer is 5 pairs from 125 */
    MM_BASE = 266, FXMM_BASE = 330,
};
static const int OSC_BASE[3] = {62, 83, 104};
static const int LFO_BASE[2] = {185, 199};
static const int GLFO_BASE[2] = {362, 365};
static const int ENV_BASE[3] = {160, 169, 177};       /* a, d, s, r, vel, trig */
static const int ENV_EXTRA[3][2] = {{166, 167}, {175, 176}, {183, 184}}; /* hold, repeats */

static int pair(const uint8_t *p, int o) {
    return p[o] * 2 + ((p[o + 1] >> 6) & 1);
}

/* ------------------------------------------------------------------ setting values (Conv.set) */

static int imin(int a, int b) { return a < b ? a : b; }

/* Python's round(): halves go to the even neighbour. */
static double round_even(double x) {
    double r = floor(x + 0.5);
    if (r - x == 0.5 && fmod(r, 2.0) != 0.0) r -= 1.0;
    return r;
}

static void set_num(float *v, int id, double x) {
    const param_desc_t *p = &CREST_PARAMS[id];
    double mn = (double)p->mn, mx = (double)p->mx;
    if (x < mn) x = mn;
    if (x > mx) x = mx;
    if (p->kind == PK_INT) x = round_even(x);
    v[id] = (float)x;
}

/* An enum by option name (the names params.py gives); unknown names leave the value alone. */
static void set_opt(float *v, int id, const char *name) {
    const param_desc_t *p = &CREST_PARAMS[id];
    for (int i = 0; i < p->nopts; i++)
        if (strcmp(p->opts[i], name) == 0) {
            v[id] = (float)i;
            return;
        }
}

static double cbrt_signed(double x) { return x < 0.0 ? -cbrt(-x) : cbrt(x); }

/* ------------------------------------------------------------------ tables (import_banks.py) */

static const char *const VOICE_MODE[5] = {"Mono", "MonoLG", "Mono2", "Poly", "Poly2"};
static const int UNISON[5] = {1, 2, 3, 4, 8};
static const char *const LFO_WAVE[4] = {"Tri", "SawDn", "Square", "S&H"};
static const char *const FILTER[2][3] = {{"LP12", "BP6", "HP12"}, {"LP24", "BP12", "HP24"}}; /* [slope][shape] */
static const char *const WAVES[5] = {"Sine", "Tri", "Saw", "Pulse", "Wavetable"};
static const char *const SHAPE_SRC[3] = {"Manual", "ModEnv1", "LFO1"};
/* The Peak's 60 wavetables in WaveMore order (index = WaveMore - 4); Crest has a table of each name. */
static const char *const PEAK_TABLES[60] = {
    "BS sine", "Random", "Zing", "Tubey", "Octaves", "Wobbler", "Chords", "Didgery", "Harsh", "Organ",
    "E. Piano", "VoxOooEe", "VoxYahEe", "Winds", "SoftClav", "String", "BassOrgn", "Acid", "Buzzy", "Carousel",
    "Choral", "Climbing", "CoinFlip", "Deep", "Dub", "Eee", "Eris", "Flame", "Further", "GlassSaw",
    "Glassy", "Granular", "Grime", "Drow", "Heavy", "Hedge", "Hungry", "Ladders", "Lead", "Modeling",
    "Modem", "Monster", "Screech", "SeaBase", "Shmorgan", "Spirals", "Steel", "Sunrise", "Swell", "Thicker",
    "Thinner", "Tides", "Tokyo", "Tops", "V.Chord", "Variance", "Vocaloid", "Vowelled", "WeirdVox", "Yeah"};
/* Peak delay sync index (slowest first) -> Crest dly_sync name: the same 16 divisions. */
static const char *const DELAY_SYNC[16] = {"1 bar", "1/2D", "1/1T", "1/2", "1/4D", "1/2T", "1/4", "1/8D",
                                           "1/4T", "1/8", "1/16D", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const char *const FADE_MODES[4] = {"FadeIn", "FadeOut", "GateIn", "GateOut"};
static const char *const CHORUS_TYPES[3] = {"Chorus 1", "Chorus 2", "Ensemble"};
static const char *const REVERB_TYPES[3] = {"Room", "Hall", "Plate"};
static const char *const DELAY_RATIOS[11] = {"1/1", "4/3", "3/4", "3/2", "2/3", "2/1", "1/2", "3/1", "1/3", "4/1", "1/4"};
static const char *const FX_ROUTES[7] = {"Parallel", "Dly>Rev>Cho", "Dly>Cho>Rev", "Rev>Dly>Cho", "Rev>Cho>Dly",
                                         "Cho>Dly>Rev", "Cho>Rev>Dly"};
static const char *const ARP_MODES[7] = {"Up", "Down", "UpDown", "UpDown2", "Played", "Random", "Chord"};
static const char *const ARP_RATES[19] = {"2bar", "1.5b", "2brT", "1bar", "1/2D", "1brT", "1/2", "1/4D", "1/2T", "1/4",
                                          "1/8D", "1/4T", "1/8", "16D", "1/8T", "1/16", "16T", "1/32", "32T"};

/* Mod matrix sources and destinations; NULL = not representable (the slot is dropped). */
static const char *const SRC[23] = {"Direct", "Mod Wheel", "Chan AT", "Expression", "Breath", "Velocity", "Key",
                                    "LFO 1+", "LFO 1", "LFO 2+", "LFO 2", "Amp Env", "Mod Env 1", "Mod Env 2",
                                    "Animate 1", "Animate 2", NULL, "LFO 3+", "LFO 3", "LFO 4+", "LFO 4",
                                    "Pitch Bend", "Pitch Bend"};
#define SRC_NEG 22 /* BndWhl-: the bend source with the depth inverted */
static const char *const DEST[37] = {"Pitch (all)", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 3 Pitch", "Osc 1 VSync",
                                     "Osc 2 VSync", "Osc 3 VSync", "Osc 1 Shape", "Osc 2 Shape", "Osc 3 Shape",
                                     "Osc 1 Level", "Osc 2 Level", "Osc 3 Level", "Noise Level", "Ring Level",
                                     "Amp Level", "Pre-OD", "Post Dist", "Cutoff", "Resonance", "LFO 1 Rate",
                                     "LFO 2 Rate", "Amp Attack", "Amp Decay", "Amp Release", "Mod1 Attack",
                                     "Mod1 Decay", "Mod1 Release", "Mod2 Attack", "Mod2 Decay", "Mod2 Release",
                                     "FM Osc1>Osc2", "FM Osc2>Osc3", "FM Osc3>Osc1", "FM Noise>Osc1",
                                     "Osc3 > Cutoff", "Noise > Cutoff"};
static const char *const FXSRC[16] = {"Direct", "Mod Wheel", "Chan AT", "Expression", "Breath", "Velocity", "Key",
                                      "Animate 1", "Animate 2", NULL, "LFO 3+", "LFO 3", "LFO 4+", "LFO 4",
                                      "Pitch Bend", "Pitch Bend"};
#define FXSRC_NEG 15
static const char *const FXDEST[12] = {"Dist Level", "Chorus Level", "Chorus Rate", "Chorus Depth", "Chorus Feedback",
                                       "Delay Level", "Delay Time", "Delay Feedback", "Reverb Level", "Reverb Decay",
                                       "Reverb Damping", "Reverb Low Cut"};
#define FXDEST_NEG 10 /* the Peak's "Rev LPF" opens the filter; Crest's "Reverb Damping" closes it */

/* ------------------------------------------------------------------ scan */

int peak_scan(const uint8_t *buf, int len, int *offs, int max) {
    int n = 0;
    for (int i = 0; i + PEAK_MSG_LEN <= len && n < max;) {
        if (buf[i] == 0xF0 && buf[i + PEAK_MSG_LEN - 1] == 0xF7) {
            offs[n++] = i;
            i += PEAK_MSG_LEN;
        } else {
            i++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ convert */

void peak_convert(const uint8_t *p, float *v, char name[PEAK_NAME_MAX]) {
    for (int i = 0; i < P_COUNT; i++) v[i] = CREST_PARAMS[i].def;

    /* name: 16 ASCII characters, space padded */
    int nl = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t c = p[16 + i];
        name[i] = (char)(c >= 32 && c < 127 ? c : '?');
        if (name[i] != ' ') nl = i + 1;
    }
    name[nl] = 0;
    if (nl == 0) strcpy(name, "Unnamed");

    /* ---- voice */
    set_opt(v, P_V_MODE, VOICE_MODE[imin(p[B_VOICE_MODE], 4)]);
    set_num(v, P_V_UNISON, UNISON[imin(p[B_UNISON], 4)]);
    set_num(v, P_V_UDET, p[B_UNI_DETUNE] / 127.0);
    set_num(v, P_V_SPREAD, p[B_UNI_SPREAD] / 127.0);
    set_num(v, P_V_OCT, p[B_OCTAVE] - 64);
    set_num(v, P_V_GLIDE, p[B_GLIDE_ON] ? p[B_GLIDE] / 127.0 : 0.0);
    set_num(v, P_V_DRIFT, p[B_DRIFT] / 127.0);
    set_opt(v, P_OSC_RESET, p[B_OSC_KEYSYNC] ? "Reset" : "Free");
    set_num(v, P_V_DIV, p[B_DIVERGENCE] / 127.0);
    set_num(v, P_FLT_DIV, p[B_F_DIVERGENCE] / 127.0);
    double g = 0.5 * pow(4.0, p[B_PATCH_LEVEL] / 127.0); /* patch level: 0 -> x0.5, 64 -> x1, 127 -> x2 */
    set_num(v, P_PATCH_VOL, sqrt(g / 2.0));
    set_num(v, P_VCA_GAIN, p[B_VCA_GAIN] / 127.0);
    int bends[3];
    for (int o = 0; o < 3; o++) bends[o] = p[OSC_BASE[o] + 18] - 64;
    int bmax = bends[0] > bends[1] ? bends[0] : bends[1];
    if (bends[2] > bmax) bmax = bends[2];
    set_num(v, P_V_BEND, bends[0] > 0 ? bends[0] : bmax);

    /* ---- oscillators */
    for (int o = 0; o < 3; o++) {
        const uint8_t *b = p + OSC_BASE[o];
        int base = P_O1_WAVE + o * (P_O2_WAVE - P_O1_WAVE);
#define OP(f) (base + (P_O1_##f - P_O1_WAVE))
        set_opt(v, OP(WAVE), WAVES[imin(b[8], 4)]);
        if (b[8] == 4) set_opt(v, OP(TABLE), PEAK_TABLES[clampi(b[9] - 4, 0, 59)]);
        int shsrc = imin(b[10], 2);
        set_opt(v, OP(SHSRC), SHAPE_SRC[shsrc]);
        set_num(v, OP(SHAPE), fabs(b[11] - 64.0) / 64.0);
        if (shsrc == 1) set_num(v, OP(SHAMT), (b[12] - 64) / 64.0);
        else if (shsrc == 2) set_num(v, OP(SHAMT), (b[13] - 64) / 64.0);
        set_num(v, OP(VSYNC), b[14] / 127.0);
        set_num(v, OP(DENS), b[15] / 127.0);
        set_num(v, OP(DDET), b[16] / 127.0);
        /* pitch: range (-1..+2 oct) + coarse (+-12 st over 0..255) + fine (+-100 ct) -> semitones + cents */
        double semis = (b[0] - 64) * 12 + (pair(b, 1) - 128) * 12.0 / 128.0;
        double cents = (pair(b, 3) - 128) + (semis - round_even(semis)) * 100.0;
        semis = round_even(semis);
        set_num(v, OP(COARSE), semis);
        set_num(v, OP(FINE), cents < -100.0 ? -100.0 : cents > 100.0 ? 100.0 : cents);
        set_num(v, OP(PENV), cbrt_signed((b[5] - 64) * 1.5 / 96.0)); /* 8 steps = 1 octave on the Peak */
        set_num(v, OP(PLFO), (pair(b, 6) - 128) / 127.0);            /* +-5 octaves, 60 st * x^3 */
        set_num(v, OP(FIXED), b[17]);
#undef OP
    }
    for (int k = 0; k < 5; k++) { /* mixer: osc 1-3, ring, noise */
        static const int ID[5] = {P_MX_O1, P_MX_O2, P_MX_O3, P_MX_RING, P_MX_NOISE};
        set_num(v, ID[k], pair(p, W_MX_O1 + 2 * k) / 255.0);
    }
    set_num(v, P_NOISE_COL, p[B_NOISE_LPF] < 127 ? -(1.0 - p[B_NOISE_LPF] / 127.0) : p[B_NOISE_HPF] / 127.0);

    /* ---- filter */
    set_opt(v, P_FLT_TYPE, FILTER[clampi(p[B_F_SLOPE], 0, 1)][clampi(p[B_F_SHAPE], 0, 2)]);
    set_num(v, P_FLT_CUT, pair(p, W_F_FREQ) / 255.0);
    set_num(v, P_FLT_RES, p[B_F_RES] / 127.0);
    set_num(v, P_FLT_KEY, p[B_F_KEYTRACK] / 127.0);
    set_num(v, P_PRE_OD, p[B_F_OVERDRIVE] / 127.0);
    set_num(v, P_POST_DIST, p[B_F_POSTDRIVE] / 127.0);
    set_num(v, P_FLT_ENV, cbrt_signed((p[B_F_MODENV1] - 64) / 63.0));
    set_num(v, P_FLT_AE, cbrt_signed((p[B_F_AMPENV] - 64) / 63.0));
    set_num(v, P_FLT_LFO, (pair(p, W_F_LFO1) - 128) / 127.0); /* eight octaves, 96 st * x^3 */
    set_num(v, P_FLT_O3, p[B_F_OSC3] / 255.0);

    /* ---- envelopes */
    for (int e = 0; e < 3; e++) {
        const uint8_t *b = p + ENV_BASE[e];
        int base = P_AE_A + e * (P_M1_A - P_AE_A);
#define EP(f) (base + (P_AE_##f - P_AE_A))
        set_num(v, EP(A), b[0] / 127.0);
        set_num(v, EP(D), b[1] / 127.0);
        set_num(v, EP(S), b[2] / 127.0);
        set_num(v, EP(R), b[3] / 127.0);
        int vel = b[4] - 64;
        set_num(v, EP(VEL), vel / (vel > 0 ? 63.0 : 64.0));
        set_opt(v, EP(TRIG), b[5] ? "Multi" : "Single"); /* MonoTrig: Re-Trig / Legato */
        set_num(v, EP(HOLD), p[ENV_EXTRA[e][0]] / 127.0);
        set_num(v, EP(REP), imin(p[ENV_EXTRA[e][1]], 127));
#undef EP
    }

    /* ---- LFOs */
    int lstride = P_L2_WAVE - P_L1_WAVE;
    for (int l = 0; l < 2; l++) {
        const uint8_t *b = p + LFO_BASE[l];
        int base = P_L1_WAVE + l * lstride;
#define LP(f) (base + (P_L1_##f - P_L1_WAVE))
        set_opt(v, LP(WAVE), LFO_WAVE[imin(b[4], 3)]);
        if (b[0] == 2) {
            set_num(v, LP(SYNC), 1 + imin(b[3], 34));
        } else {
            set_opt(v, LP(RANGE), b[0] == 1 ? "High" : "Low");
            set_num(v, LP(RATE), pair(b, 1) / 255.0);
        }
        if (b[5] > 0) {
            set_opt(v, LP(TRIG), "Key");
            set_num(v, LP(PHASE), (b[5] - 1) / 120.0);
        }
        if (b[12]) set_opt(v, LP(TRIG), b[5] > 0 ? "CommonKey" : "Common");
        set_num(v, LP(REP), imin(b[11], 127));
        set_num(v, LP(SLEW), b[6] / 127.0);
        set_num(v, LP(FADE), b[7] / 127.0);
        set_opt(v, LP(FMODE), FADE_MODES[imin(b[8], 3)]);
        set_opt(v, LP(MTRIG), b[10] ? "Re-Trig" : "Legato");
        set_opt(v, LP(FSYNC), b[9] ? "On" : "Off");
#undef LP
    }
    for (int l = 0; l < 2; l++) { /* LFO 3/4: wave, rate (0..127), sync (0 = Off) */
        const uint8_t *b = p + GLFO_BASE[l];
        int base = P_L3_WAVE + l * lstride;
        set_opt(v, base, LFO_WAVE[imin(b[0], 3)]);
        set_num(v, base + (P_L1_RATE - P_L1_WAVE), b[1] / 127.0);
        if (b[2] > 0) set_num(v, base + (P_L1_SYNC - P_L1_WAVE), imin(b[2], 35));
    }

    /* ---- mod matrix (16 slots) and FX matrix (4 slots) into up to 20 Crest slots */
    int ns = 0;
    for (int i = 0; i < 16 + 4 && ns < 20; i++) {
        int fxs = i >= 16;
        const uint8_t *m = p + (fxs ? FXMM_BASE + 4 * (i - 16) : MM_BASE + 4 * i);
        int s1 = m[0], s2 = m[1], depth = m[2], dest = m[3];
        if (depth == 64) continue; /* zero depth */
        const char *n1, *n2, *nd;
        int neg;
        if (!fxs) {
            n1 = s1 < 23 ? SRC[s1] : NULL;
            n2 = s2 < 23 ? SRC[s2] : NULL;
            nd = dest < 37 ? DEST[dest] : NULL;
            neg = (s1 == SRC_NEG) ^ (s2 == SRC_NEG);
        } else {
            n1 = s1 < 16 ? FXSRC[s1] : NULL;
            n2 = s2 < 16 ? FXSRC[s2] : NULL;
            nd = dest < 12 ? FXDEST[dest] : NULL;
            neg = (s1 == FXSRC_NEG) ^ (s2 == FXSRC_NEG) ^ (dest == FXDEST_NEG);
        }
        if (!nd || !n1 || (!n2 && s2 != 0)) continue;
        double dep = (depth - 64) / 63.0;
        if (neg) dep = -dep;
        int sb = P_MM01_S1 + 4 * ns++;
        set_opt(v, sb, n1);
        set_opt(v, sb + 1, n2); /* an unused source B is "Direct" (x1), as the Peak has it */
        set_opt(v, sb + 2, nd);
        set_num(v, sb + 3, dep < -1.0 ? -1.0 : dep > 1.0 ? 1.0 : dep);
    }

    /* ---- effects */
    int off = p[B_FX_BYPASS] == 1;
#define LVL(x) (off ? 0.0 : (x) / 127.0)
    set_num(v, P_DST_LVL, p[B_FX_DIST_LVL] / 127.0); /* the bypass button does not bypass the distortion */
    set_num(v, P_CHO_LVL, LVL(p[B_CHO_LVL]));
    set_opt(v, P_CHO_TYPE, CHORUS_TYPES[imin(p[B_CHO_TYPE], 2)]);
    set_num(v, P_CHO_RATE, p[B_CHO_RATE] / 127.0);
    set_num(v, P_CHO_DEPTH, p[B_CHO_DEPTH] / 127.0);
    set_num(v, P_CHO_FB, (p[B_CHO_FB] - 64) / 64.0);
    set_num(v, P_DLY_LVL, LVL(p[B_DLY_LVL]));
    if (p[B_DLY_SYNC]) {
        set_opt(v, P_DLY_SYNC, DELAY_SYNC[imin(p[B_DLY_SYNCTIME], 15)]);
    } else {
        set_opt(v, P_DLY_SYNC, "Off");
        double secs = 0.010 * pow(140.0, p[B_DLY_TIME] / 127.0); /* 127 = 1.4 s on Crest's 10 ms..2 s law */
        set_num(v, P_DLY_TIME, log(secs / 0.010) / log(200.0));
    }
    set_num(v, P_DLY_FB, p[B_DLY_FB] / 127.0);
    set_num(v, P_DLY_LP, 1.0 - p[B_DLY_LP] / 127.0); /* LP Damp is an amount of damping */
    set_num(v, P_DLY_HP, p[B_DLY_HP] / 127.0);
    set_opt(v, P_DLY_RATIO, DELAY_RATIOS[imin(p[B_DLY_RATIO], 10)]);
    set_num(v, P_DLY_WIDTH, p[B_DLY_WIDTH] / 127.0);
    set_num(v, P_DLY_SLEW, p[B_DLY_SLEW] / 127.0);
    set_num(v, P_CHO_LP, p[B_CHO_LP] / 127.0);
    set_num(v, P_CHO_HP, p[B_CHO_HP] / 127.0);
    set_num(v, P_REV_LPF, p[B_REV_LOWPASS] / 127.0);
    set_num(v, P_REV_HPF, p[B_REV_HIGHPASS] / 127.0);
    set_num(v, P_REV_MDEP, p[B_REV_MOD] / 127.0);
    set_num(v, P_REV_MRATE, p[B_REV_MODRATE] / 127.0);
    set_num(v, P_REV_LVL, LVL(p[B_REV_LVL]));
    set_opt(v, P_REV_TYPE, REVERB_TYPES[imin(p[B_REV_TYPE], 2)]);
    set_num(v, P_REV_DECAY, p[B_REV_TIME] / 127.0);
    set_num(v, P_REV_DAMP, p[B_REV_LP] / 127.0);
    set_num(v, P_REV_HP, p[B_REV_HP] / 127.0);
    set_num(v, P_REV_SIZE, p[B_REV_SIZE] / 127.0);
    set_num(v, P_REV_PRE, p[B_REV_PRE] / 127.0);
#undef LVL
    /* An effect is in use if its level is up or the FX matrix moves a level. Peak's dry 0 with nothing in
     * use would be silence in Crest's blend, so dry and wet are only honoured when there is something to hear. */
    int fx_mod = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *m = p + FXMM_BASE + 4 * i;
        if (m[2] != 64 && (m[3] == 1 || m[3] == 5 || m[3] == 8)) fx_mod = 1;
    }
    int active = !off && (p[B_DLY_LVL] || p[B_REV_LVL] || p[B_CHO_LVL] || fx_mod);
    set_num(v, P_FX_WET, active ? p[B_FX_WET] / 127.0 : 1.0);
    set_num(v, P_FX_DRY, active ? p[B_FX_DRY] / 127.0 : 1.0);
    set_opt(v, P_FX_ROUTE, FX_ROUTES[imin(p[B_FX_ROUTING], 6)]);

    /* ---- arpeggiator and Animate */
    set_opt(v, P_ARP_ON, p[B_ARP_ON] ? "On" : "Off");
    set_opt(v, P_ARP_MODE, ARP_MODES[imin(p[B_ARP_TYPE], 6)]);
    set_num(v, P_ARP_OCT, p[B_ARP_OCT] + 1);
    set_num(v, P_ARP_RHY, p[B_ARP_RHYTHM]);
    set_opt(v, P_ARP_RATE, ARP_RATES[18 - clampi(p[B_ARP_SYNC], 0, 18)]);
    set_num(v, P_ARP_GATE, p[B_ARP_GATE] / 127.0);
    set_num(v, P_ARP_SWING, p[B_ARP_SWING] / 100.0);
    set_opt(v, P_ARP_LATCH, p[B_ARP_LATCH] ? "On" : "Off");
    set_opt(v, P_ARP_KSYNC, p[B_ARP_KEYSYNC] ? "On" : "Off");
    set_opt(v, P_ANIM1_MODE, p[B_ANIM1_HOLD] ? "Latch" : "Hold");
    set_opt(v, P_ANIM2_MODE, p[B_ANIM2_HOLD] ? "Latch" : "Hold");
}
