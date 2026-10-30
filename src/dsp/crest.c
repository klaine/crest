/* Crest: Schwung sound-generator module. Plugin entry points, instance pool, MIDI, render loop. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef CREST_PROFILE
#include <time.h>
#endif

#include "crest.h"
#include "peak_bank.h"
#include "dsp_util.h"

#if defined(__linux__)
#include "move_info.h"
#else
/* The native tests and the browser have no Move: Set stays on its C minor fallback there. */
typedef struct {
    uint8_t valid;
    int8_t root_note;
    char scale[24];
} move_info_t;
static int move_info_read(move_info_t *out) {
    (void)out;
    return 0;
}
#endif

static const host_api_v1_t *g_host;
/* static: no malloc anywhere. The extra last slot is the loudness solver's (crest_scratch_instance). */
static crest_inst_t g_pool[CREST_MAX_INSTANCES + 1];
static plugin_api_v2_t g_api; /* defined at the end */
static char g_err[64];

/* ------------------------------------------------------------------ lifecycle */

static crest_inst_t *claim(crest_inst_t *p) {
    int expect = 0;
    if (!atomic_compare_exchange_strong(&p->in_use, &expect, 1)) return NULL;
    int was_used = p->ever_used;
    /* Reset everything after the pool bookkeeping except the big FX arena, which is cleared
     * lazily (64 KB per block) when an effect is first switched on again. */
    memset((char *)p + offsetof(crest_inst_t, v), 0, offsetof(crest_inst_t, fx) - offsetof(crest_inst_t, v));
    memset(&p->fx, 0, offsetof(fx_t, mem));
    p->ever_used = 1;
    p->slot = (int)(p - g_pool);
    p->syx_cur = -1;
    peak_bank_start(&g_api); /* once per process, here on the loader thread (never the callback) */
    peak_bank_request(p->slot, ""); /* a previous instance's bank does not carry over */
    p->err[0] = 0;
    fx_init(&p->fx, was_used);
    params_defaults(p);
    params_prepare(p);
    p->last_vmode = p->bp.vmode;
    p->last_unison = p->bp.unison;
    p->expression = 1.0f;
    p->s_expr = 1.0f;
    p->beat = -1.0;
    p->bpm = 120.0f;
    for (int n = 0; n < 2; n++) {
        p->glfo[n].rng = 0x9E3779B1u * (uint32_t)(n + 1) | 1u;
        lfo_start(&p->glfo[n], &p->bp.lfo_p[2 + n], p->bp.lfo_p[2 + n].trig == LT_FREE);
    }
    p->crng = 0x6C8E9CF5u;
    for (int n = 0; n < 2; n++) {
        p->cshare[n].sh_prev = rng_bi(&p->crng);
        p->cshare[n].sh_next = rng_bi(&p->crng);
    }
    return p;
}

/* The Move set's root and scale, for the arp's Scale = Set. A lock-free seqlock copy, safe on the audio thread
 * once create_instance has made the first read, which resolves the export (dlsym, shm_open, mmap). */
static void update_set_scale(crest_inst_t *I) {
    arp_t *a = &I->arp;
    move_info_t mi;
    memset(&mi, 0, sizeof mi);
    if (!move_info_read(&mi) || !mi.valid) return;
    if (mi.root_note == a->set_root_raw && memcmp(mi.scale, a->set_name, sizeof a->set_name) == 0) return;
    a->set_root_raw = mi.root_note;
    memcpy(a->set_name, mi.scale, sizeof a->set_name);
    a->set_name[sizeof a->set_name - 1] = 0;
    arp_set_scale(I, mi.root_note, a->set_name);
}

static void *crest_create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults;
    for (int i = 0; i < CREST_MAX_INSTANCES; i++) {
        crest_inst_t *p = claim(&g_pool[i]);
        if (p) {
            update_set_scale(p);
            return p;
        }
    }
    snprintf(g_err, sizeof g_err, "Crest: all %d instances are in use", CREST_MAX_INSTANCES);
    return NULL;
}

void *crest_scratch_instance(void) { return claim(&g_pool[CREST_MAX_INSTANCES]); }

static crest_inst_t *as_inst(void *p) {
    crest_inst_t *c = (crest_inst_t *)p;
    if (c >= g_pool && c < g_pool + CREST_MAX_INSTANCES + 1) return c;
    return NULL;
}

static void crest_destroy_instance(void *instance) {
    crest_inst_t *I = as_inst(instance);
    if (I) atomic_store(&I->in_use, 0);
}

static int crest_get_error(void *instance, char *buf, int buf_len) {
    crest_inst_t *I = as_inst(instance);
    const char *e = I ? I->err : g_err;
    int n = (int)strlen(e);
    if (n == 0 || !buf || buf_len < n + 1) return 0;
    memcpy(buf, e, (size_t)n + 1);
    return n;
}

/* ------------------------------------------------------------------ MIDI */

static void cc_animate(crest_inst_t *I, int n, int value) {
    int on = value >= 64;
    if (on && !I->anim_cc[n] && I->v[P_ANIM1 + 2 * n + 1] >= 0.5f)
        I->anim_latch[n] = !I->anim_latch[n]; /* rising edge toggles, in Latch mode only */
    I->anim_cc[n] = on;
}

/* Refresh the derived parameters; a voice-mode change retires the old voices before any new note. So does
 * a unison change in a mono mode: those place their voices by unison count, and a held group that no
 * longer matches would never be released. */
static void prepare(crest_inst_t *I) {
    params_prepare(I);
    int relayout = I->bp.vmode >= VM_MONO && I->bp.unison != I->last_unison;
    if (I->bp.vmode != I->last_vmode || relayout) { /* poly <-> mono: retire the old voices cleanly */
        voice_all_off(I, 1);
        I->last_vmode = I->bp.vmode;
    }
    I->last_unison = I->bp.unison;
}

static void crest_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    crest_inst_t *I = as_inst(instance);
    if (!I || !msg || len < 1 || source == MOVE_MIDI_SOURCE_TOUCH) return;
    if (I->pdirty) prepare(I); /* a note in the same block as a knob move must see the new value */
    int st = msg[0] & 0xF0;
    int d1 = len > 1 ? msg[1] & 0x7F : 0;
    int d2 = len > 2 ? msg[2] & 0x7F : 0;
    switch (st) {
    case 0x90:
        if (d2 > 0) {
            if (arp_enabled(I)) arp_note_on(I, d1, d2);
            else voice_note_on(I, d1, d2);
            break;
        }
        /* fall through: note-on with velocity 0 is a note-off */
    case 0x80:
        if (arp_enabled(I)) arp_note_off(I, d1);
        else voice_note_off(I, d1);
        break;
    case 0xA0:
        for (int i = 0; i < CREST_VOICES; i++)
            if (I->voice[i].active && I->voice[i].note == d1) I->voice[i].polyat = (float)d2 * (1.0f / 127.0f);
        break;
    case 0xB0:
        switch (d1) {
        case 1: I->modwheel = (float)d2 * (1.0f / 127.0f); break;
        case 2: I->breath = (float)d2 * (1.0f / 127.0f); break;
        case 11: I->expression = (float)d2 * (1.0f / 127.0f); break;
        case 64: voice_sustain(I, d2 >= 64); break;
        case 80: cc_animate(I, 0, d2); break;
        case 81: cc_animate(I, 1, d2); break;
        case 120: arp_all_off(I); voice_all_off(I, 1); break;
        case 123: arp_all_off(I); voice_all_off(I, 0); break;
        case 121:
            I->modwheel = I->chan_at = I->bend = I->breath = 0.0f;
            I->expression = 1.0f;
            voice_sustain(I, 0);
            break;
        default: break;
        }
        break;
    case 0xD0: I->chan_at = (float)d1 * (1.0f / 127.0f); break;
    case 0xE0: I->bend = (float)(((d2 << 7) | d1) - 8192) * (1.0f / 8192.0f); break;
    default: break;
    }
}

static void crest_set_param(void *instance, const char *key, const char *val) {
    crest_inst_t *I = as_inst(instance);
    if (I) params_set(I, key, val);
}

static int crest_get_param(void *instance, const char *key, char *buf, int buf_len) {
    crest_inst_t *I = as_inst(instance);
    if (!I) return -1;
    return params_get(I, key, buf, buf_len);
}

/* ------------------------------------------------------------------ global modulation */

/* Once per sub-block: smooth the controllers, run LFO 3/4, Animate and the macros, and work out the
 * matrix for the destinations that are not per voice (effects, LFO 3/4 rates). */
static void global_tick(crest_inst_t *I) {
    const bparams_t *bp = &I->bp;
    const float c5 = 1.0f - expf(-CREST_CTL_DT / 0.005f);
    const float c10 = 1.0f - expf(-CREST_CTL_DT / 0.010f);
    I->s_wheel += c5 * (I->modwheel - I->s_wheel);
    I->s_chanat += c5 * (I->chan_at - I->s_chanat);
    I->s_expr += c5 * (I->expression - I->s_expr);
    I->s_breath += c5 * (I->breath - I->s_breath);
    I->s_sustain += c5 * ((I->sustain_down ? 1.0f : 0.0f) - I->s_sustain);
    I->s_bend += c5 * (I->bend - I->s_bend); /* also drives the pitch: no zipper on fast bends */
    for (int n = 0; n < 2; n++) {
        int mode = (int)I->v[P_ANIM1_MODE + 2 * n];
        float tgt = I->v[P_ANIM1 + 2 * n] >= 0.5f ? 1.0f : (mode == 1 ? (float)I->anim_latch[n] : (float)I->anim_cc[n]);
        I->s_anim[n] += c10 * (tgt - I->s_anim[n]);
    }
    for (int m = 0; m < 8; m++) I->s_mac[m] += c5 * (I->v[P_MAC1 + m] - I->s_mac[m]);

    for (int n = 0; n < 2; n++) { /* shared phase and random values for voice LFOs in Common mode */
        const lfo_par_t *p = &bp->lfo_p[n];
        lfo_shared_t *c = &I->cshare[n];
        float hz = lfo_rate_hz(p, 0.0f, I->bpm), prev = c->phase;
        if (p->sync > 0 && I->beat >= 0.0 && p->trig != LT_COMMONKEY) {
            double cyc = I->beat * (double)hz * 60.0 / (double)I->bpm + (double)p->phase;
            c->phase = (float)(cyc - floor(cyc));
        } else {
            c->phase += hz * CREST_CTL_DT;
            c->phase -= floorf(c->phase);
        }
        if (c->phase < prev) {
            c->sh_prev = c->sh_next;
            c->sh_next = rng_bi(&I->crng);
        }
    }
    for (int n = 0; n < 2; n++) { /* LFO 3/4: the first key of a phrase restarts their timing (and a Key phase) */
        const lfo_par_t *p = &bp->lfo_p[2 + n];
        if (I->lfo_restart) {
            if (p->trig == LT_KEY) lfo_rephase(&I->glfo[n], p);
            lfo_retime(&I->glfo[n]);
        }
        float lv = lfo_tick(&I->glfo[n], p, I->gacc[MD_LFO3_RATE + n], I->beat, I->bpm, NULL);
        I->gsrc[MS_LFO3 + n] = lv;
        I->gsrc[MS_LFO3P + n] = 0.5f + 0.5f * lv;
    }
    I->lfo_restart = 0;

    float *g = I->gsrc;
    g[MS_DIRECT] = 1.0f;
    g[MS_WHEEL] = I->s_wheel;
    g[MS_CHANAT] = I->s_chanat;
    g[MS_EXPR] = I->s_expr;
    g[MS_BREATH] = I->s_breath;
    g[MS_SUSTAIN] = I->s_sustain;
    g[MS_BEND] = I->s_bend;
    g[MS_ANIM1] = I->s_anim[0];
    g[MS_ANIM2] = I->s_anim[1];
    for (int m = 0; m < 8; m++) g[MS_MAC1 + m] = I->s_mac[m];

    /* Destinations that are not per voice use the most recently started voice for the per-voice sources. */
    float src[MS_COUNT];
    memcpy(src, g, sizeof src);
    const voice_t *last = NULL;
    for (int i = 0; i < CREST_VOICES; i++)
        if (I->voice[i].active && (!last || I->voice[i].age > last->age)) last = &I->voice[i];
    if (last) {
        static const int PER_VOICE[] = {MS_POLYAT, MS_VEL, MS_KEY, MS_RANDOM, MS_AENV, MS_ME1, MS_ME2, MS_LFO1, MS_LFO2, MS_LFO1P, MS_LFO2P};
        for (size_t k = 0; k < sizeof PER_VOICE / sizeof *PER_VOICE; k++) src[PER_VOICE[k]] = last->src[PER_VOICE[k]];
    }
    mod_eval(bp, src, I->gacc);
}

/* ------------------------------------------------------------------ render */

/* Linear below 0.8, then a smooth knee that approaches 1.0 with the same slope. */
static inline float soft_clip(float x) {
    float ax = fabsf(x);
    if (ax <= 0.8f) return x;
    float y = 0.8f + 0.2f * fast_tanh((ax - 0.8f) * 5.0f);
    return x < 0.0f ? -y : y;
}

static void crest_render_block(void *instance, int16_t *out, int frames) {
    crest_inst_t *I = as_inst(instance);
    if (!out || frames <= 0) return;
    if (!I) {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
#ifdef CREST_PROFILE
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
#endif
    uint64_t fp = ftz_enter();
    if (I->syx_pend && syx_apply(I)) I->syx_pend = 0; /* the picked bank has finished loading */
    if (I->syx_cur >= 0 && !I->syx_vol_set) { /* a Peak bank patch: its loudness arrives from the worker */
        float vol = peak_bank_vol(I->slot, I->syx_cur);
        if (vol >= 0.0f) {
            I->v[P_PATCH_VOL] = vol;
            I->pdirty = 1;
            I->syx_vol_set = 1;
        }
    }
    prepare(I);
    float bpm = 120.0f;
    double beat0 = -1.0;
    if (g_host && !I->offline) {
        if (g_host->get_bpm) {
            float b = g_host->get_bpm();
            if (b > 20.0f && b < 400.0f) bpm = b;
        }
        if (g_host->get_clock_status && g_host->get_beat_position &&
            g_host->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING) {
            double bt = g_host->get_beat_position();
            if (bt >= 0.0) beat0 = bt;
        }
        if (arp_enabled(I) && (int)I->v[P_ARP_SCALE] == 1) update_set_scale(I); /* the set can change under us */
    }
    I->bpm = bpm;

    int done = 0;
    while (done < frames) {
        int cnt = frames - done;
        if (cnt > CREST_MAX_FRAMES) cnt = CREST_MAX_FRAMES;
        memset(I->busl, 0, (size_t)cnt * sizeof(float));
        memset(I->busr, 0, (size_t)cnt * sizeof(float));
        for (int sb = 0; sb < cnt; sb += CREST_SUB) {
            int n = cnt - sb;
            if (n > CREST_SUB) n = CREST_SUB;
            I->beat = beat0 >= 0.0 ? beat0 + (double)(done + sb) * (double)bpm / (60.0 * (double)CREST_FS) : -1.0;
            global_tick(I);
            arp_tick(I, n);
            for (int vi = 0; vi < CREST_VOICES; vi++) {
                voice_t *v = &I->voice[vi];
                if (v->active) voice_render(I, v, I->busl + sb, I->busr + sb, n);
            }
        }
        float chk = 0.0f;
        for (int i = 0; i < cnt; i++) chk += I->busl[i] * I->busl[i] + I->busr[i] * I->busr[i];
        if (!isfinite(chk) || chk > 1e12f) { /* runaway or NaN: wipe everything and carry on */
            memset(I->busl, 0, (size_t)cnt * sizeof(float));
            memset(I->busr, 0, (size_t)cnt * sizeof(float));
            for (int vi = 0; vi < CREST_VOICES; vi++) {
                I->voice[vi].active = 0;
                filt_reset(&I->voice[vi].flt);
            }
            fx_reset(&I->fx);
            I->nan_resets++;
        }
        /* Effect levels (and delay feedback) can be modulated from the matrix. */
        float fxv[P_COUNT];
        memcpy(fxv, I->v, sizeof fxv);
        fxv[P_DST_LVL] = clampf(fxv[P_DST_LVL] + I->gacc[MD_FX_DIST], 0.0f, 1.0f);
        fxv[P_CHO_LVL] = clampf(fxv[P_CHO_LVL] + I->gacc[MD_FX_CHO], 0.0f, 1.0f);
        fxv[P_DLY_LVL] = clampf(fxv[P_DLY_LVL] + I->gacc[MD_FX_DLY], 0.0f, 1.0f);
        fxv[P_REV_LVL] = clampf(fxv[P_REV_LVL] + I->gacc[MD_FX_REV], 0.0f, 1.0f);
        fxv[P_DLY_FB] = clampf(fxv[P_DLY_FB] + I->gacc[MD_DLY_FB], 0.0f, 1.0f);
        fxv[P_DLY_TIME] = clampf(fxv[P_DLY_TIME] + I->gacc[MD_DLY_TIME], 0.0f, 1.0f);
        fxv[P_REV_DECAY] = clampf(fxv[P_REV_DECAY] + I->gacc[MD_REV_DECAY], 0.0f, 1.0f);
        fxv[P_CHO_RATE] = clampf(fxv[P_CHO_RATE] + I->gacc[MD_CHO_RATE], 0.0f, 1.0f);
        fxv[P_CHO_DEPTH] = clampf(fxv[P_CHO_DEPTH] + I->gacc[MD_CHO_DEPTH], 0.0f, 1.0f);
        fxv[P_CHO_FB] = clampf(fxv[P_CHO_FB] + I->gacc[MD_CHO_FB], -1.0f, 1.0f);
        fxv[P_REV_DAMP] = clampf(fxv[P_REV_DAMP] + I->gacc[MD_REV_DAMP], 0.0f, 1.0f);
        fxv[P_REV_HP] = clampf(fxv[P_REV_HP] + I->gacc[MD_REV_HP], 0.0f, 1.0f);
        fx_process(&I->fx, fxv, bpm, I->busl, I->busr, cnt);
        float gain = I->bp.master;
        int16_t *o = out + 2 * done;
        for (int i = 0; i < cnt; i++) {
            float l = soft_clip(I->busl[i] * gain) * 32767.0f;
            float r = soft_clip(I->busr[i] * gain) * 32767.0f;
            o[2 * i] = (int16_t)lrintf(clampf(l, -32768.0f, 32767.0f));
            o[2 * i + 1] = (int16_t)lrintf(clampf(r, -32768.0f, 32767.0f));
        }
        done += cnt;
    }
    ftz_exit(fp);
#ifdef CREST_PROFILE
    clock_gettime(CLOCK_MONOTONIC, &t1);
    I->cpu_us += 0.05f * (((float)(t1.tv_sec - t0.tv_sec)) * 1e6f + ((float)(t1.tv_nsec - t0.tv_nsec)) * 1e-3f - I->cpu_us);
#endif
}

/* ------------------------------------------------------------------ entry */

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = crest_create_instance,
    .destroy_instance = crest_destroy_instance,
    .on_midi = crest_on_midi,
    .set_param = crest_set_param,
    .get_param = crest_get_param,
    .get_error = crest_get_error,
    .render_block = crest_render_block,
};

CREST_EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
