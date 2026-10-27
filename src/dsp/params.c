/* Parameter store: get/set, derived values, proxies, state blob, factory presets (plan/11, 13, 07). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crest.h"
#include "dsp_util.h"
#include "peak_bank.h"

/* Parameters are addressed as base + n * stride, so the groups in params.py must stay uniform. */
#define OSC_STRIDE (P_O2_WAVE - P_O1_WAVE)
#define LFO_STRIDE (P_L2_WAVE - P_L1_WAVE)
#define ENV_STRIDE (P_M1_A - P_AE_A)
#define OSC_F(o, f) (P_O1_##f + (o) * OSC_STRIDE)
#define LFO_F(n, f) (P_L1_##f + (n) * LFO_STRIDE)
#define STRIDE_OK(f) (P_O2_##f - P_O1_##f == OSC_STRIDE && P_O3_##f - P_O2_##f == OSC_STRIDE)
_Static_assert(STRIDE_OK(WAVE) && STRIDE_OK(TABLE) && STRIDE_OK(SHAPE) && STRIDE_OK(SHSRC) &&
                   STRIDE_OK(SHAMT) && STRIDE_OK(DENS) && STRIDE_OK(DDET) && STRIDE_OK(VSYNC) &&
                   STRIDE_OK(COARSE) && STRIDE_OK(FINE) && STRIDE_OK(PENV) && STRIDE_OK(PLFO) && STRIDE_OK(FIXED),
               "oscillator block stride");
_Static_assert(P_M2_A - P_M1_A == ENV_STRIDE && P_M1_TRIG - P_M1_A == P_AE_TRIG - P_AE_A, "envelope block stride");
_Static_assert(P_L3_WAVE - P_L2_WAVE == LFO_STRIDE && P_L4_WAVE - P_L3_WAVE == LFO_STRIDE &&
                   P_L4_FSYNC - P_L4_WAVE == P_L1_FSYNC - P_L1_WAVE, "LFO block stride");
_Static_assert(P_MM02_S1 - P_MM01_S1 == 4 && P_MM01_DEP - P_MM01_S1 == 3, "matrix slot stride");
_Static_assert(P_MAC2_D1 - P_MAC1_D1 == 8 && P_MAC1_D2 - P_MAC1_D1 == 2 && P_MAC1_A1 - P_MAC1_D1 == 1,
               "macro assignment stride");
_Static_assert(P_ANIM2 - P_ANIM1 == 2, "animate stride");

static float sqr(float x) { return x * x; }

/* ------------------------------------------------------------------ proxy keys */

static int sel_slot(const crest_inst_t *I) { return clampi((int)I->v[P_MM_SEL], 0, CREST_MM_SLOTS - 1); }
static int mac_base(const crest_inst_t *I) {
    return P_MAC1_D1 + 8 * clampi((int)I->v[P_MAC_SEL], 0, 7) + 2 * (clampi((int)I->v[P_MAC_K], 1, 4) - 1);
}

/* The proxies show the selected matrix slot / macro assignment; refresh them after the selection or the
 * raw values changed. */
static void proxies_refresh(crest_inst_t *I) {
    int base = P_MM01_S1 + 4 * sel_slot(I);
    I->v[P_MM_SLOT] = (float)(sel_slot(I) + 1);
    I->v[P_MM_S1] = I->v[base];
    I->v[P_MM_S2] = I->v[base + 1];
    I->v[P_MM_DST] = I->v[base + 2];
    I->v[P_MM_DEP] = I->v[base + 3];
    I->v[P_MM_CLEAR] = 0.0f;
    int mb = mac_base(I);
    I->v[P_MAC_DST] = I->v[mb];
    I->v[P_MAC_AMT] = I->v[mb + 1];
}

/* A write to a proxy key lands in the raw slot it stands for. */
static void proxy_written(crest_inst_t *I, int id) {
    int base = P_MM01_S1 + 4 * sel_slot(I);
    switch (id) {
    case P_MM_SEL:
    case P_MAC_SEL:
    case P_MAC_K: break;
    case P_MM_SLOT: I->v[P_MM_SEL] = I->v[P_MM_SLOT] - 1.0f; break;
    case P_MM_S1: I->v[base] = I->v[P_MM_S1]; break;
    case P_MM_S2: I->v[base + 1] = I->v[P_MM_S2]; break;
    case P_MM_DST: I->v[base + 2] = I->v[P_MM_DST]; break;
    case P_MM_DEP: I->v[base + 3] = I->v[P_MM_DEP]; break;
    case P_MM_CLEAR:
        if (I->v[P_MM_CLEAR] >= 0.5f)
            for (int i = 0; i < 4; i++) I->v[base + i] = 0.0f;
        break;
    case P_MAC_DST: I->v[mac_base(I)] = I->v[P_MAC_DST]; break;
    case P_MAC_AMT: I->v[mac_base(I) + 1] = I->v[P_MAC_AMT]; break;
    default: return;
    }
    proxies_refresh(I);
}

/* ------------------------------------------------------------------ derived values */

void params_defaults(crest_inst_t *I) {
    for (int i = 0; i < P_COUNT; i++) I->v[i] = CREST_PARAMS[i].def;
    strcpy(I->name, "Init Saw");
    I->preset_idx = 0;
    proxies_refresh(I);
}

void params_prepare(crest_inst_t *I) {
    const float *v = I->v;
    I->pdirty = 0;
    bparams_t *b = &I->bp;
    for (int o = 0; o < 3; o++) {
        float pe = v[OSC_F(o, PENV)], pl = v[OSC_F(o, PLFO)];
        b->osc[o].wave = (int)v[OSC_F(o, WAVE)];
        b->osc[o].table = (int)v[OSC_F(o, TABLE)];
        b->osc[o].shsrc = (int)v[OSC_F(o, SHSRC)];
        b->osc[o].shape = v[OSC_F(o, SHAPE)];
        b->osc[o].shamt = v[OSC_F(o, SHAMT)];
        b->osc[o].dens = v[OSC_F(o, DENS)];
        b->osc[o].ddet = v[OSC_F(o, DDET)];
        b->osc[o].vsync = v[OSC_F(o, VSYNC)];
        b->osc[o].semis = v[OSC_F(o, COARSE)] + v[OSC_F(o, FINE)] * 0.01f;
        b->osc[o].penv = pe * pe * pe * 96.0f; /* 8 octaves at full depth, as on the Peak */
        b->osc[o].plfo = pl * pl * pl * 60.0f; /* +-5 octaves, fine near zero: the Peak's LFO 2 Depth */
        b->osc[o].level = v[P_MX_O1 + o];
        b->osc[o].fixed = (int)v[OSC_F(o, FIXED)];
    }
    b->lvl_noise = v[P_MX_NOISE];
    b->vca = v[P_VCA_GAIN];
    b->fm[0] = v[P_FM_12];
    b->fm[1] = v[P_FM_23];
    b->fm[2] = v[P_FM_31];
    b->fm[3] = v[P_FM_N1];
    b->flt_o3 = v[P_FLT_O3];
    b->flt_ns = v[P_FLT_NS];
    b->flt_div = v[P_FLT_DIV];
    b->v_div = v[P_V_DIV];
    b->lvl_ring = v[P_MX_RING];
    float col = v[P_NOISE_COL];
    if (col < -0.001f) {
        float fc = 20000.0f * powf(0.01f, -col); /* 20 kHz -> 200 Hz */
        b->nz_mode = 1;
        b->nz_a = 1.0f - expf(-CREST_TAU * fc / CREST_FS);
        b->nz_boost = fminf(4.0f, sqrtf((2.0f - b->nz_a) / b->nz_a));
    } else if (col > 0.001f) {
        float fc = 20.0f * powf(400.0f, col); /* 20 Hz -> 8 kHz */
        b->nz_mode = 2;
        b->nz_a = 1.0f - expf(-CREST_TAU * fc / CREST_FS);
        b->nz_boost = 1.0f;
    } else {
        b->nz_mode = 0;
    }
    b->pre_od = v[P_PRE_OD];
    b->post_dist = v[P_POST_DIST];

    float fe = v[P_FLT_ENV], fl = v[P_FLT_LFO];
    b->ftype = (int)v[P_FLT_TYPE];
    b->cut = v[P_FLT_CUT];
    b->res = v[P_FLT_RES];
    b->drive = v[P_FLT_DRIVE];
    b->key = v[P_FLT_KEY];
    b->env = fe * fe * fe * 96.0f;
    b->lfo = fl * fl * fl * 96.0f; /* eight octaves at full depth (manual p.24), fine near zero */
    b->vel = v[P_FLT_VEL] * 48.0f;
    b->ae = v[P_FLT_AE] * v[P_FLT_AE] * v[P_FLT_AE] * 96.0f;

    for (int k = 0; k < 3; k++) {
        int base = P_AE_A + ENV_STRIDE * k;
        env_par_t *e = &b->env_p[k];
        e->a = v[base + (P_AE_A - P_AE_A)];
        e->d = v[base + (P_AE_D - P_AE_A)];
        e->s = v[base + (P_AE_S - P_AE_A)];
        e->r = v[base + (P_AE_R - P_AE_A)];
        e->dly = v[base + (P_AE_DLY - P_AE_A)];
        e->hold = v[base + (P_AE_HOLD - P_AE_A)];
        b->env_vel[k] = v[base + (P_AE_VEL - P_AE_A)];
        e->rep = (int)v[base + (P_AE_REP - P_AE_A)];
        e->trig = (int)v[base + (P_AE_TRIG - P_AE_A)];
    }
    for (int n = 0; n < 4; n++) {
        lfo_par_t *l = &b->lfo_p[n];
        l->wave = (int)v[LFO_F(n, WAVE)];
        l->rate = v[LFO_F(n, RATE)];
        l->sync = (int)v[LFO_F(n, SYNC)];
        l->phase = v[LFO_F(n, PHASE)];
        l->dly = v[LFO_F(n, DLY)];
        l->fade = v[LFO_F(n, FADE)];
        l->slew = v[LFO_F(n, SLEW)];
        l->trig = (int)v[LFO_F(n, TRIG)];
        l->rep = (int)v[LFO_F(n, REP)];
        l->fmode = (int)v[LFO_F(n, FMODE)];
        l->range = n >= 2 ? LR_WIDE : (int)v[LFO_F(n, RANGE)];
        l->mtrig = (int)v[LFO_F(n, MTRIG)];
        l->fsync = (int)v[LFO_F(n, FSYNC)];
        if (n >= 2 && l->trig >= LT_COMMON) l->trig = l->trig == LT_COMMONKEY ? LT_KEY : LT_FREE; /* global LFOs */
    }

    b->vmode = (int)v[P_V_MODE];
    b->gmode = (int)v[P_V_GMODE];
    b->oct = (int)v[P_V_OCT];
    b->osc_reset = (int)v[P_OSC_RESET];
    b->unison = clampi((int)v[P_V_UNISON], 1, 8);
    b->glide_x = v[P_V_GLIDE];
    b->bend_range = v[P_V_BEND];
    b->udet = v[P_V_UDET];
    b->spread = v[P_V_SPREAD];
    b->drift = v[P_V_DRIFT];
    b->master = 2.0f * sqr(v[P_PATCH_VOL]);

    b->nslots = 0;
    for (int n = 0; n < CREST_MM_SLOTS; n++) {
        int base = P_MM01_S1 + 4 * n;
        int s1 = (int)v[base], s2 = (int)v[base + 1], dst = (int)v[base + 2];
        float dep = v[base + 3];
        if (s1 && dst && dep != 0.0f) {
            mm_slot_t *s = &b->slot[b->nslots++];
            s->s1 = (uint8_t)s1;
            s->s2 = (uint8_t)s2;
            s->dst = (uint8_t)dst;
            s->dep = dep;
        }
    }
    for (int m = 0; m < 8; m++)
        for (int k = 0; k < 4; k++) {
            int base = P_MAC1_D1 + 8 * m + 2 * k;
            float amt = v[base + 1];
            b->mac[m][k].dst = amt != 0.0f ? (uint8_t)v[base] : 0;
            b->mac[m][k].amt = amt;
        }
}

/* ------------------------------------------------------------------ lookup / values */

static int find_param(const char *key) {
    int lo = 0, hi = P_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int id = CREST_PARAMS_SORTED[mid];
        int c = strcmp(key, CREST_PARAMS[id].key);
        if (c == 0) return id;
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

static int ci_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return *a == *b;
}

/* Enums accept an index or an option name. Returns 0 if the text is not understood. */
static int parse_value(int id, const char *s, float *out) {
    const param_desc_t *d = &CREST_PARAMS[id];
    if (d->kind == PK_ENUM) {
        /* Names first: some options ("1/8", "1 bar") start with a digit. */
        for (int i = 0; i < d->nopts; i++)
            if (ci_eq(s, d->opts[i])) {
                *out = (float)i;
                return 1;
            }
        if (!((s[0] >= '0' && s[0] <= '9') || s[0] == '-' || s[0] == '+')) return 0;
    }
    char *end;
    float f = strtof(s, &end);
    if (end == s || !isfinite(f)) return 0; /* "nan" would pass the clamp and mute every note */
    if (d->kind != PK_FLOAT) f = roundf(f);
    *out = clampf(f, d->mn, d->mx);
    return 1;
}

static int format_value(int id, float val, char *buf, int cap) {
    const param_desc_t *d = &CREST_PARAMS[id];
    int n = d->kind == PK_FLOAT ? snprintf(buf, (size_t)cap, "%.4f", (double)val)
                                : snprintf(buf, (size_t)cap, "%d", (int)lrintf(val));
    return n >= cap ? -1 : n;
}

static int copy_out(const char *s, int len, char *buf, int cap) {
    if (len + 1 > cap) return -1;
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return len;
}

/* ------------------------------------------------------------------ presets */

void preset_load_values(crest_inst_t *I, const float *v, const char *name) {
    float sel = I->v[P_MM_SEL], msel = I->v[P_MAC_SEL], mk = I->v[P_MAC_K];
    memcpy(I->v, v, sizeof I->v);
    I->v[P_MM_SEL] = sel;
    I->v[P_MAC_SEL] = msel;
    I->v[P_MAC_K] = mk;
    proxies_refresh(I);
    strncpy(I->name, name, sizeof I->name - 1);
    I->name[sizeof I->name - 1] = 0;
    I->preset_idx = -1;
    I->syx_cur = -1;
    voice_all_off(I, 1);
    fx_reset(&I->fx);
    params_prepare(I);
    I->last_vmode = I->bp.vmode; /* the voice-mode change is handled here, not by the next render */
    I->last_unison = I->bp.unison;
}

void preset_load(crest_inst_t *I, int idx) {
    if (idx < 0 || idx >= CREST_FACTORY_COUNT) return;
    preset_load_values(I, CREST_FACTORY_VALUES[idx], CREST_FACTORY_NAMES[idx]);
    I->preset_idx = idx;
}

int syx_apply(crest_inst_t *I) {
    if (!peak_bank_ready(I->slot)) return 0;
    int n = clampi((int)I->v[P_SYX_PATCH], 0, PEAK_BANK_MAX - 1);
    float pv[P_COUNT];
    char nm[PEAK_NAME_MAX];
    if (peak_bank_patch(I->slot, n, pv, nm)) {
        float knob = I->v[P_SYX_PATCH];
        preset_load_values(I, pv, nm);
        I->v[P_SYX_PATCH] = knob;
        I->syx_cur = n;
        I->syx_vol_set = 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ state blob */

typedef struct {
    char *b;
    int cap, n, ok;
} wr_t;

static void w_str(wr_t *w, const char *s) {
    int l = (int)strlen(s);
    if (!w->ok || w->n + l + 1 > w->cap) {
        w->ok = 0;
        return;
    }
    memcpy(w->b + w->n, s, (size_t)l);
    w->n += l;
    w->b[w->n] = 0;
}

static int state_save(crest_inst_t *I, char *buf, int cap) {
    wr_t w = {buf, cap, 0, 1};
    if (cap > 0) buf[0] = 0;
    w_str(&w, "{\"fmt\":\"crest\",\"v\":\"1\",\"name\":\"");
    for (const char *p = I->name; *p; p++) {
        char c[3] = {0, 0, 0};
        if (*p == '"' || *p == '\\') {
            c[0] = '\\';
            c[1] = *p;
        } else if ((unsigned char)*p >= 32 && (unsigned char)*p < 127) {
            c[0] = *p;
        }
        w_str(&w, c);
    }
    w_str(&w, "\"");
    for (int i = 0; i < P_COUNT; i++) {
        if (!(CREST_PARAMS[i].flags & PF_STATE)) continue;
        char val[24];
        w_str(&w, ",\"");
        w_str(&w, CREST_PARAMS[i].key);
        w_str(&w, "\":\"");
        if (format_value(i, I->v[i], val, sizeof val) < 0) return -1;
        w_str(&w, val);
        w_str(&w, "\"");
    }
    w_str(&w, "}");
    return w.ok ? w.n : -1;
}

/* Tolerant single-pass parser: unknown keys are ignored, missing keys keep their INIT value, and
 * the blob must start with "fmt":"crest". Never reads past the terminating NUL. */
static int state_load(crest_inst_t *I, const char *s) {
    float nv[P_COUNT];
    char name[sizeof I->name];
    for (int i = 0; i < P_COUNT; i++) nv[i] = CREST_PARAMS[i].def;
    strcpy(name, "Init");
    int first = 1, fmt_ok = 0;
    const char *p = s;
    while (*p) {
        if (*p != '"') {
            p++;
            continue;
        }
        const char *k0 = ++p;
        while (*p && *p != '"') p++;
        if (!*p) break;
        char key[40];
        int klen = (int)(p - k0);
        if (klen > 39) klen = 39;
        memcpy(key, k0, (size_t)klen);
        key[klen] = 0;
        p++;
        while (*p == ' ') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ') p++;
        char val[64];
        int vl = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                if (vl < 63) val[vl++] = *p;
                p++;
            }
            if (*p) p++;
        } else {
            while (*p && *p != ',' && *p != '}') {
                if (vl < 63) val[vl++] = *p;
                p++;
            }
        }
        val[vl] = 0;
        if (first) {
            first = 0;
            if (strcmp(key, "fmt") != 0 || strcmp(val, "crest") != 0) return -1;
            fmt_ok = 1;
            continue;
        }
        if (strcmp(key, "name") == 0) {
            strncpy(name, val, sizeof name - 1);
            name[sizeof name - 1] = 0;
        } else {
            int id = find_param(key);
            float f;
            if (id >= 0 && (CREST_PARAMS[id].flags & PF_STATE) && parse_value(id, val, &f)) nv[id] = f;
        }
    }
    if (!fmt_ok) return -1;
    for (int i = 0; i < P_COUNT; i++)
        if (CREST_PARAMS[i].flags & PF_STATE) I->v[i] = nv[i];
    memcpy(I->name, name, sizeof name);
    I->preset_idx = -1;
    proxies_refresh(I);
    voice_all_off(I, 1);
    fx_reset(&I->fx);
    params_prepare(I);
    I->last_vmode = I->bp.vmode; /* the voice-mode change is handled here, not by the next render */
    I->last_unison = I->bp.unison;
    return 0;
}

/* ------------------------------------------------------------------ list pages */

/* [{"index":0,"label":"1: LFO1xWhl>Cut"},...] for the matrix page. */
static int items_matrix(const crest_inst_t *I, char *buf, int cap) {
    int n = 0;
    if (cap < 2) return -1;
    buf[n++] = '[';
    for (int m = 0; m < CREST_MM_SLOTS; m++) {
        int base = P_MM01_S1 + 4 * m;
        int s1 = (int)I->v[base], s2 = (int)I->v[base + 1], dst = (int)I->v[base + 2];
        char label[48], one[96];
        if (!s1 || !dst) {
            snprintf(label, sizeof label, "%d: --", m + 1);
        } else if (s2) {
            snprintf(label, sizeof label, "%d: %sx%s>%s", m + 1, CREST_MOD_SRC_SHORT[s1], CREST_MOD_SRC_SHORT[s2],
                     CREST_MOD_DST_SHORT[dst]);
        } else {
            snprintf(label, sizeof label, "%d: %s>%s", m + 1, CREST_MOD_SRC_SHORT[s1], CREST_MOD_DST_SHORT[dst]);
        }
        snprintf(one, sizeof one, "%s{\"index\":%d,\"label\":\"%s\"}", m ? "," : "", m, label);
        int r = (int)strlen(one);
        if (n + r + 2 >= cap) return -1;
        memcpy(buf + n, one, (size_t)r + 1);
        n += r;
    }
    buf[n++] = ']';
    buf[n] = 0;
    return n;
}

/* One row per macro: its first destination and how many more it moves. */
static int items_macros(const crest_inst_t *I, char *buf, int cap) {
    int n = 0;
    if (cap < 2) return -1;
    buf[n++] = '[';
    for (int m = 0; m < 8; m++) {
        int first = -1, extra = 0;
        for (int k = 0; k < 4; k++) {
            int base = P_MAC1_D1 + 8 * m + 2 * k;
            if ((int)I->v[base] && I->v[base + 1] != 0.0f) {
                if (first < 0) first = (int)I->v[base];
                else extra++;
            }
        }
        char label[40], one[96];
        if (first < 0) snprintf(label, sizeof label, "M%d: --", m + 1);
        else if (extra) snprintf(label, sizeof label, "M%d: %s +%d", m + 1, CREST_MOD_DST_SHORT[first], extra);
        else snprintf(label, sizeof label, "M%d: %s", m + 1, CREST_MOD_DST_SHORT[first]);
        snprintf(one, sizeof one, "%s{\"index\":%d,\"label\":\"%s\"}", m ? "," : "", m, label);
        int r = (int)strlen(one);
        if (n + r + 2 >= cap) return -1;
        memcpy(buf + n, one, (size_t)r + 1);
        n += r;
    }
    buf[n++] = ']';
    buf[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ set / get */

void params_set(crest_inst_t *I, const char *key, const char *val) {
    if (!key || !val) return;
    if (strcmp(key, "state") == 0) {
        state_load(I, val);
        return;
    }
    if (strcmp(key, "preset") == 0) {
        preset_load(I, atoi(val));
        return;
    }
    if (strcmp(key, "name") == 0) {
        strncpy(I->name, val, sizeof I->name - 1);
        I->name[sizeof I->name - 1] = 0;
        return;
    }
    if (strcmp(key, "syx_file") == 0) { /* the Peak bank to load; the worker does the reading */
        peak_bank_request(I->slot, val);
        I->syx_pend = val[0] != 0; /* then the knob's patch loads by itself (crest_render_block) */
        return;
    }
    if (strcmp(key, "syx_patch") == 0) {
        /* Load patch n of the loaded bank. While no bank is ready the value is only kept and the patch
         * loads when the bank arrives: a Move set restoring this key before its bank has loaded must
         * not replace the patch it saved. */
        I->v[P_SYX_PATCH] = (float)clampi(atoi(val), 0, PEAK_BANK_MAX - 1);
        I->syx_pend = !syx_apply(I);
        return;
    }
    int id = find_param(key);
    float f;
    if (id >= 0 && parse_value(id, val, &f)) {
        I->v[id] = f;
        proxy_written(I, id);
        I->pdirty = 1;
    }
}

int params_get(crest_inst_t *I, const char *key, char *buf, int buf_len) {
    if (!key || !buf || buf_len < 1) return -1;
    if (strcmp(key, "chain_params") == 0)
        return copy_out(CREST_CHAIN_PARAMS, CREST_CHAIN_PARAMS_LEN, buf, buf_len);
    if (strcmp(key, "ui_hierarchy") == 0)
        return copy_out(CREST_UI_HIERARCHY, CREST_UI_HIERARCHY_LEN, buf, buf_len);
    if (strcmp(key, "state") == 0) return state_save(I, buf, buf_len);
    if (strcmp(key, "mm_items") == 0) return items_matrix(I, buf, buf_len);
    if (strcmp(key, "mac_items") == 0) return items_macros(I, buf, buf_len);
    if (strcmp(key, "preset") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%d", I->preset_idx < 0 ? 0 : I->preset_idx);
        return n >= buf_len ? -1 : n;
    }
    if (strcmp(key, "preset_count") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%d", CREST_FACTORY_COUNT);
        return n >= buf_len ? -1 : n;
    }
    if (strcmp(key, "preset_name") == 0 || strcmp(key, "name") == 0)
        return copy_out(I->name, (int)strlen(I->name), buf, buf_len);
    if (strcmp(key, "split_voices") == 0) return copy_out("", 0, buf, buf_len);
    if (strcmp(key, "syx_file") == 0) return peak_bank_path(I->slot, buf, buf_len);
#ifdef CREST_PROFILE
    if (strcmp(key, "cpu_us") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.1f", (double)I->cpu_us);
        return n >= buf_len ? -1 : n;
    }
#endif
    int id = find_param(key);
    if (id < 0) return -1;
    return format_value(id, I->v[id], buf, buf_len);
}
