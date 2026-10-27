/* Voices: allocation, modes, unison, glide, modulation and the per-sub-block render (plan/08, 07, 03). */
#include <math.h>
#include <string.h>

#include "crest.h"
#include "dsp_util.h"

#define VOICE_GAIN 0.5f
#define FADE_STEP 0.25f /* a stolen voice fades out over 4 sub-blocks = 64 samples */
#define SQRT2 1.41421356f
#define DRIFT_A 6.84e-4f   /* one-pole at 0.3 Hz per control tick */
#define DRIFT_SCALE 46.8f  /* brings the filtered noise to roughly +-0.5 */

/* Mixer level law x^1.5: the middle of a level knob is -9 dB (an audio taper, like the Peak's). */
static float mix_law(float x) { return x * sqrtf(x); }

/* Velocity scaling of an envelope: depth > 0 makes harder notes louder (0 = no effect), depth < 0
 * makes them softer (manual p.23). */
static float vel_gain(float depth, float vel01) {
    return depth >= 0.0f ? 1.0f - depth * (1.0f - vel01) : 1.0f + depth * vel01;
}

/* ------------------------------------------------------------------ held-key stack */

static void stack_remove(crest_inst_t *I, int note) {
    int j = 0;
    for (int i = 0; i < I->nstack; i++)
        if (I->stack[i] != note) I->stack[j++] = I->stack[i];
    I->nstack = j;
}

static void stack_push(crest_inst_t *I, int note) {
    stack_remove(I, note);
    if (I->nstack >= CREST_STACK) {
        memmove(I->stack, I->stack + 1, CREST_STACK - 1);
        I->nstack = CREST_STACK - 1;
    }
    I->stack[I->nstack++] = (uint8_t)note;
}

/* ------------------------------------------------------------------ start / release */

static void voice_release(crest_inst_t *I, voice_t *v) {
    v->gate = 0;
    v->sustained = 0;
    for (int k = 0; k < 3; k++) env_release(&v->env[k], &I->bp.env_p[k]);
}

/* fresh: a clean voice. legato: an overlapping note in a mono mode (envelopes may keep running).
 * glide_from >= 0 starts the pitch there. */
static void voice_start(crest_inst_t *I, voice_t *v, int note, int vel, int fresh, int legato, float glide_from,
                        int uidx, int usize, uint32_t group) {
    const bparams_t *bp = &I->bp;
    float tgt = (float)note + 12.0f * (float)bp->oct;
    if (fresh) {
        uint32_t seed = v->rng ? v->rng : 0x9E3779B9u + (uint32_t)(v - I->voice) * 7919u;
        memset(v, 0, sizeof *v);
        v->rng = seed;
        rng_u32(&v->rng);
        for (int o = 0; o < 3; o++) {
            v->osc[o].ph[0] = bp->osc_reset ? 0u : rng_u32(&v->rng);
            for (int u = 1; u < 4; u++) v->osc[o].ph[u] = rng_u32(&v->rng); /* density copies: random */
        }
        for (int o = 0; o < 3; o++) v->dvg[o] = rng_bi(&v->rng);
        v->fdvg = rng_bi(&v->rng);
        v->pitch = glide_from >= 0.0f ? glide_from : tgt;
        v->fresh = 1;
        for (int k = 0; k < 2; k++) {
            v->lfo[k].rng = rng_u32(&v->rng) | 1u;
            lfo_start(&v->lfo[k], &bp->lfo_p[k], bp->lfo_p[k].trig == LT_FREE);
        }
    } else {
        /* A new note on a sounding voice. Its LFO delay, fade and repeats start over and a Key LFO
         * restarts its wave; a legato note in a mono mode does so only as MonoTrig / FadeSync say. */
        for (int k = 0; k < 2; k++) {
            const lfo_par_t *lp = &bp->lfo_p[k];
            if (lp->trig == LT_KEY && (!legato || lp->mtrig)) lfo_rephase(&v->lfo[k], lp);
            if (!legato || lp->fsync) lfo_retime(&v->lfo[k]);
        }
    }
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->uidx = (uint8_t)uidx;
    v->usize = (uint8_t)usize;
    v->group = group;
    v->upos = usize > 1 ? 2.0f * (float)uidx / (float)(usize - 1) - 1.0f : 0.0f;
    v->gate = 1;
    v->sustained = 0;
    v->fading = 0;
    v->pend = 0;
    v->fade = 1.0f;
    v->active = 1;
    v->age = ++I->age_ctr;
    v->polyat = 0.0f;
    v->rnd = (float)(rng_u32(&v->rng) & 0xFFFFFF) * (1.0f / 16777216.0f);
    for (int k = 0; k < 3; k++) {
        if (legato && bp->env_p[k].trig == ENV_SINGLE &&
            v->env[k].stage != ES_IDLE && v->env[k].stage != ES_RELEASE)
            continue; /* keep running */
        env_trigger(&v->env[k], &bp->env_p[k], bp->env_p[k].trig == ENV_RESET);
    }
    if (fresh) { /* prime the destinations so the first control tick already sees envelope-time mods */
        memcpy(v->src, I->gsrc, sizeof(float) * MS_COUNT);
        v->src[MS_VEL] = (float)vel * (1.0f / 127.0f);
        v->src[MS_KEY] = ((float)note - 60.0f) * (1.0f / 60.0f);
        v->src[MS_RANDOM] = v->rnd;
        mod_eval(bp, v->src, v->acc);
    }
}

/* ------------------------------------------------------------------ groups (unison) */

typedef struct {
    int idx[CREST_VOICES];
    int steal[CREST_VOICES];
    int n;
} alloc_t;

static int in_alloc(const alloc_t *A, int i) {
    for (int k = 0; k < A->n; k++)
        if (A->idx[k] == i) return 1;
    return 0;
}

/* Best voice to take: the quietest releasing one, else the oldest held one, else the oldest of
 * those already fading. */
static int pick_victim(crest_inst_t *I, const alloc_t *A) {
    int best = -1;
    for (int i = 0; i < CREST_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (!v->active || in_alloc(A, i) || v->fading || v->gate || v->sustained) continue;
        if (best < 0 || v->amp_out < I->voice[best].amp_out) best = i;
    }
    if (best >= 0) return best;
    for (int i = 0; i < CREST_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (!v->active || in_alloc(A, i) || v->fading) continue;
        if (best < 0 || v->age < I->voice[best].age) best = i;
    }
    if (best >= 0) return best;
    for (int i = 0; i < CREST_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (!v->active || in_alloc(A, i)) continue;
        if (best < 0 || v->age < I->voice[best].age) best = i;
    }
    return best;
}

/* Find U voices: idle ones first, then whole groups taken from the voices worth stealing. */
static void alloc_group(crest_inst_t *I, int U, alloc_t *A) {
    A->n = 0;
    for (int i = 0; i < CREST_VOICES && A->n < U; i++)
        if (!I->voice[i].active) {
            A->idx[A->n] = i;
            A->steal[A->n++] = 0;
        }
    while (A->n < U) {
        int vi = pick_victim(I, A);
        if (vi < 0) break;
        uint32_t gid = I->voice[vi].group;
        for (int i = 0; i < CREST_VOICES; i++) {
            voice_t *w = &I->voice[i];
            if (!w->active || in_alloc(A, i) || (i != vi && w->group != gid)) continue;
            if (A->n < U) {
                A->idx[A->n] = i;
                A->steal[A->n++] = 1;
            } else if (!w->fading) { /* the rest of a bigger group just goes away */
                w->fading = 1;
                w->pend = 0;
            }
        }
    }
}

static void start_group(crest_inst_t *I, int note, int vel, int U, float gf) {
    alloc_t A;
    alloc_group(I, U, &A);
    int total = A.n < U ? A.n : U;
    uint32_t gid = ++I->group_ctr;
    for (int k = 0; k < A.n; k++) {
        voice_t *v = &I->voice[A.idx[k]];
        if (A.steal[k]) { /* fade out, then the pending note starts in this voice */
            v->fading = 1;
            v->pend = 1;
            v->pend_note = (uint8_t)note;
            v->pend_vel = (uint8_t)vel;
            v->pend_uidx = (uint8_t)k;
            v->pend_usize = (uint8_t)total;
            v->pend_group = gid;
            v->pend_glide = gf;
        } else {
            voice_start(I, v, note, vel, 1, 0, gf, k, total, gid);
        }
    }
}

/* ------------------------------------------------------------------ polyphonic modes */

static void poly_note_on(crest_inst_t *I, int note, int vel) {
    const bparams_t *bp = &I->bp;
    int others = I->nstack > 1; /* the stack already holds this note */
    float gf = -1.0f;
    if (bp->glide_x > 0.0f && I->last_valid && (bp->gmode == 0 || others)) gf = I->last_pitch;
    I->last_pitch = (float)note + 12.0f * (float)bp->oct;
    I->last_valid = 1;
    if (bp->vmode == VM_POLY) {
        int found = 0;
        for (int i = 0; i < CREST_VOICES; i++) {
            voice_t *v = &I->voice[i];
            if (v->active && !v->fading && v->note == note) { /* same key again: retrigger in place */
                voice_start(I, v, note, vel, 0, 0, -1.0f, v->uidx, v->usize, v->group);
                found = 1;
            }
        }
        if (found) return;
    }
    start_group(I, note, vel, bp->unison, gf);
}

static void poly_note_off(crest_inst_t *I, int note) {
    for (int i = 0; i < CREST_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (!v->active) continue;
        if (v->pend && v->pend_note == note) v->pend = 0;
        if (!v->fading && v->gate && !v->sustained && v->note == note) {
            if (I->sustain_down) v->sustained = 1;
            else voice_release(I, v);
        }
    }
}

/* ------------------------------------------------------------------ monophonic modes */

static int mono_U(const crest_inst_t *I) {
    int U = I->bp.unison;
    return I->bp.vmode == VM_MONO2 && U > 4 ? 4 : U; /* Mono2 needs two groups */
}

static voice_t *mono_first(crest_inst_t *I) {
    return &I->voice[I->bp.vmode == VM_MONO2 ? I->m2cur * mono_U(I) : 0];
}

static void mono2_play(crest_inst_t *I, int note, int vel) {
    const bparams_t *bp = &I->bp;
    int U = mono_U(I), old = I->m2cur, g = old ^ 1;
    int legato = 0;
    for (int k = 0; k < U; k++) {
        voice_t *v = &I->voice[old * U + k];
        if (v->active && v->gate && !v->fading) {
            legato = 1;
            voice_release(I, v);
        }
    }
    float gf = -1.0f;
    if (bp->glide_x > 0.0f && I->last_valid && (bp->gmode == 0 || legato)) gf = I->last_pitch;
    uint32_t gid = ++I->group_ctr;
    for (int k = 0; k < U; k++) {
        voice_t *v = &I->voice[g * U + k];
        if (v->active) {
            v->fading = 1;
            v->pend = 1;
            v->pend_note = (uint8_t)note;
            v->pend_vel = (uint8_t)vel;
            v->pend_uidx = (uint8_t)k;
            v->pend_usize = (uint8_t)U;
            v->pend_group = gid;
            v->pend_glide = gf;
        } else {
            voice_start(I, v, note, vel, 1, 0, gf, k, U, gid);
        }
    }
    I->m2cur = g;
    I->last_pitch = (float)note + 12.0f * (float)bp->oct;
    I->last_valid = 1;
}

static void mono_play(crest_inst_t *I, int note, int vel) {
    const bparams_t *bp = &I->bp;
    if (bp->vmode == VM_MONO2) {
        mono2_play(I, note, vel);
        return;
    }
    int U = mono_U(I);
    uint32_t gid = ++I->group_ctr;
    for (int k = 0; k < U; k++) {
        voice_t *v = &I->voice[k];
        if (v->active && v->fading) { /* a steal or mode change is in progress: start afterwards */
            v->pend = 1;
            v->pend_note = (uint8_t)note;
            v->pend_vel = (uint8_t)vel;
            v->pend_uidx = (uint8_t)k;
            v->pend_usize = (uint8_t)U;
            v->pend_group = gid;
            v->pend_glide = -1.0f;
            continue;
        }
        int legato = v->active && v->gate;
        int want_glide = bp->glide_x > 0.0f && (bp->gmode == 0 || legato);
        if (v->active) {
            if (!want_glide) v->pitch = (float)note + 12.0f * (float)bp->oct;
            voice_start(I, v, note, vel, 0, legato, -1.0f, k, U, gid);
        } else {
            float gf = want_glide && I->last_valid ? I->last_pitch : -1.0f;
            voice_start(I, v, note, vel, 1, 0, gf, k, U, gid);
        }
    }
    I->last_pitch = (float)note + 12.0f * (float)bp->oct;
    I->last_valid = 1;
}

static void mono_release(crest_inst_t *I, int sustain_ok) {
    int U = mono_U(I);
    int base = I->bp.vmode == VM_MONO2 ? I->m2cur * U : 0;
    for (int k = 0; k < U; k++) {
        voice_t *v = &I->voice[base + k];
        if (!v->active || v->fading || !v->gate) continue;
        if (sustain_ok && I->sustain_down) v->sustained = 1;
        else voice_release(I, v);
    }
}

static void mono_note_off(crest_inst_t *I, int note) {
    voice_t *v = mono_first(I);
    for (int k = 0, U = mono_U(I); k < U; k++) /* a start still waiting for a fade-out is cancelled */
        if (v[k].pend && v[k].pend_note == note) v[k].pend = 0;
    if (!v->active || v->fading) return;
    if (I->nstack > 0) { /* fall back to the most recent key still held */
        if (v->note == note) mono_play(I, I->stack[I->nstack - 1], v->vel);
        return;
    }
    if (v->gate && v->note == note) mono_release(I, 1);
}

/* ------------------------------------------------------------------ public */

void voice_note_on(crest_inst_t *I, int note, int vel) {
    if (I->nstack == 0) I->lfo_restart = 1; /* first key of a phrase: restarts the global LFOs */
    int legato = I->bp.vmode >= VM_MONO && I->bp.vmode != VM_MONO2 && I->nstack > 0;
    for (int k = 0; k < 2; k++) { /* CommonKey: the shared LFO restarts on a new note (a legato one per MonoTrig) */
        const lfo_par_t *lp = &I->bp.lfo_p[k];
        if (lp->trig == LT_COMMONKEY && (!legato || lp->mtrig)) {
            I->cshare[k].phase = lp->phase;
            I->cshare[k].sh_prev = I->cshare[k].sh_next;
            I->cshare[k].sh_next = rng_bi(&I->crng);
        }
    }
    stack_push(I, note);
    if (I->bp.vmode >= VM_MONO) mono_play(I, note, vel);
    else poly_note_on(I, note, vel);
}

void voice_note_off(crest_inst_t *I, int note) {
    stack_remove(I, note);
    if (I->bp.vmode >= VM_MONO) mono_note_off(I, note);
    else poly_note_off(I, note);
}

void voice_sustain(crest_inst_t *I, int down) {
    I->sustain_down = down;
    if (down) return;
    for (int i = 0; i < CREST_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (v->active && v->sustained) voice_release(I, v);
    }
}

void voice_all_off(crest_inst_t *I, int hard) {
    I->nstack = 0;
    I->sustain_down = 0;
    for (int i = 0; i < CREST_VOICES; i++) {
        voice_t *v = &I->voice[i];
        if (!v->active) continue;
        v->pend = 0;
        if (hard) v->fading = 1;
        else voice_release(I, v);
    }
}

/* ------------------------------------------------------------------ render */

/* The mixer's headroom (manual p.22: the sum "can clip if all sources are turned up to maximum"). The sum
 * is 0.5 x the oscillators, so one oscillator at full peaks near 0.5 and passes untouched; above
 * MIX_KNEE a tanh knee bends the sum towards MIX_CEIL, so two or three hot sources saturate softly. */
#define MIX_KNEE 0.6f
#define MIX_CEIL 1.1f
static inline float mix_sat(float x) {
    float ax = fabsf(x);
    if (ax <= MIX_KNEE) return x;
    float y = MIX_KNEE + (MIX_CEIL - MIX_KNEE) * fast_tanh((ax - MIX_KNEE) * (1.0f / (MIX_CEIL - MIX_KNEE)));
    return x < 0.0f ? -y : y;
}

static inline float logcosh_f(float x) {
    float ax = fabsf(x);
    return ax + log1pf(expf(-2.0f * ax)) - 0.69314718f;
}

void voice_render(crest_inst_t *I, voice_t *v, float *outl, float *outr, int n) {
    const bparams_t *bp = &I->bp;
    if (v->fading && v->fade <= 0.0f) {
        if (v->pend) {
            int note = v->pend_note, vel = v->pend_vel, ui = v->pend_uidx, us = v->pend_usize;
            uint32_t gid = v->pend_group;
            float gf = v->pend_glide;
            voice_start(I, v, note, vel, 1, 0, gf, ui, us, gid);
        } else {
            v->active = 0;
            v->fading = 0;
            return;
        }
    }
    if (!v->fading && v->env[0].stage == ES_IDLE) {
        v->active = 0;
        return;
    }

    /* ---- control rate: envelopes and LFOs use last tick's modulation for their own time/rate ---- */
    float vel01 = (float)v->vel * (1.0f / 127.0f);
    env_par_t ep[3];
    for (int k = 0; k < 3; k++) {
        ep[k] = bp->env_p[k];
        ep[k].a = clampf(ep[k].a + v->acc[MD_AE_A + 3 * k], 0.0f, 1.0f);
        ep[k].d = clampf(ep[k].d + v->acc[MD_AE_D + 3 * k], 0.0f, 1.0f);
        ep[k].r = clampf(ep[k].r + v->acc[MD_AE_R + 3 * k], 0.0f, 1.0f);
    }
    float e0 = env_tick(&v->env[0], &ep[0]);
    float e1 = env_tick(&v->env[1], &ep[1]) * vel_gain(bp->env_vel[1], vel01);
    float e2 = env_tick(&v->env[2], &ep[2]) * vel_gain(bp->env_vel[2], vel01);
    v->amp_out = e0;
    float l1 = lfo_tick(&v->lfo[0], &bp->lfo_p[0], v->acc[MD_LFO1_RATE], I->beat, I->bpm, &I->cshare[0]);
    float l2 = lfo_tick(&v->lfo[1], &bp->lfo_p[1], v->acc[MD_LFO2_RATE], I->beat, I->bpm, &I->cshare[1]);

    float *src = v->src;
    memcpy(src, I->gsrc, sizeof(float) * MS_COUNT);
    src[MS_POLYAT] = v->polyat;
    src[MS_VEL] = vel01;
    src[MS_KEY] = ((float)v->note - 60.0f) * (1.0f / 60.0f);
    src[MS_RANDOM] = v->rnd;
    src[MS_AENV] = e0;
    src[MS_ME1] = e1;
    src[MS_ME2] = e2;
    src[MS_LFO1] = l1;
    src[MS_LFO2] = l2;
    src[MS_LFO1P] = 0.5f + 0.5f * l1;
    src[MS_LFO2P] = 0.5f + 0.5f * l2;
    mod_eval(bp, src, v->acc);
    const float *acc = v->acc;

    for (int k = 0; k < 4; k++) { /* analog drift: slow filtered noise per voice */
        v->drift[k] += DRIFT_A * (rng_bi(&v->rng) - v->drift[k]);
    }

    /* ---- glide and pitch ---- */
    float tgt = (float)v->note + 12.0f * (float)bp->oct;
    float gx = clampf(bp->glide_x + acc[MD_GLIDE], 0.0f, 1.0f);
    float gt = 5.6f * gx * gx * gx * gx * gx;
    if (gt > 0.0005f) v->pitch += (tgt - v->pitch) * (1.0f - expf(-4.6f * CREST_CTL_DT / gt));
    else v->pitch = tgt;
    float bend = I->s_bend * bp->bend_range;
    float ud = clampf(bp->udet + acc[MD_UDET], 0.0f, 1.0f);
    float cents = v->upos * 50.0f * ud * ud * ud;

    float dt[3], lv[5];
    oscp_t po[3];
    for (int o = 0; o < 3; o++) {
        float dr = clampf(v->drift[o] * DRIFT_SCALE, -1.0f, 1.0f) * bp->drift * 0.10f; /* up to 10 cents */
        float base = bp->osc[o].fixed > 0 ? (float)bp->osc[o].fixed : v->pitch;
        float dv = v->dvg[o] * bp->v_div * 0.4f; /* up to 40 cents */
        float semis = base + cents * 0.01f + dr + dv + bp->osc[o].semis + bend + bp->osc[o].penv * e2 +
                      bp->osc[o].plfo * l2 + (acc[MD_PITCH] + acc[MD_O1_PITCH + o]) * 48.0f;
        dt[o] = clampf(semis_to_hz(semis) * (1.0f / CREST_FS), 1e-5f, 0.49f);
        float sm = bp->osc[o].shsrc == 1 ? e1 : (bp->osc[o].shsrc == 2 ? l1 : 0.0f);
        po[o].wave = bp->osc[o].wave;
        po[o].table = bp->osc[o].table;
        po[o].shape = clampf(bp->osc[o].shape + bp->osc[o].shamt * sm + acc[MD_O1_SHAPE + o], 0.0f, 1.0f);
        po[o].dens = clampf(bp->osc[o].dens + acc[MD_O1_DENS + o], 0.0f, 1.0f);
        po[o].ddet = bp->osc[o].ddet;
        po[o].vsync = clampf(bp->osc[o].vsync + acc[MD_O1_VSYNC + o], 0.0f, 1.0f);
        lv[o] = mix_law(clampf(bp->osc[o].level + acc[MD_O1_LEVEL + o], 0.0f, 1.0f));
    }
    lv[3] = mix_law(clampf(bp->lvl_noise + acc[MD_NOISE], 0.0f, 1.0f));
    lv[4] = mix_law(clampf(bp->lvl_ring + acc[MD_RING], 0.0f, 1.0f));
    float pod = clampf(bp->pre_od + acc[MD_PREOD], 0.0f, 1.0f);
    float pdist = clampf(bp->post_dist + acc[MD_POSTDIST], 0.0f, 1.0f);

    float res = clampf(bp->res + acc[MD_RES], 0.0f, 1.0f);
    float drv = clampf(bp->drive + acc[MD_FDRIVE], 0.0f, 1.0f);
    float k_res = filt_res_to_k(res);
    float sat_a = 0.5f + 3.0f * drv;
    float in_gain = (1.0f + 0.5f * res) * (1.0f + 2.0f * drv);
    float out_gain = 1.0f / (1.0f + drv);
    float dc = clampf(v->drift[3] * DRIFT_SCALE, -1.0f, 1.0f) * bp->drift * 0.3f;
    float cs = 20.0f + 115.0f * clampf(bp->cut, 0.0f, 1.0f) + bp->key * (v->pitch - 60.0f) + bp->env * e1 + bp->ae * e0 +
               bp->lfo * l1 + bp->vel * (vel01 - 0.5f) + dc + acc[MD_CUTOFF] * 96.0f +
               v->fdvg * bp->flt_div * 12.0f;
    float fc = clampf(semis_to_hz(cs), 16.0f, 0.45f * CREST_FS);
    float g = tanf(CREST_PI * fc * (1.0f / CREST_FS));

    float amp = e0 * vel_gain(bp->env_vel[0], vel01) * VOICE_GAIN * clampf(bp->vca + acc[MD_AMP], 0.0f, 2.0f);
    float pan = clampf(v->upos * bp->spread + acc[MD_PAN], -1.0f, 1.0f);
    float ang = (pan + 1.0f) * (CREST_PI * 0.25f);
    float gl = cosf(ang) * SQRT2, gr = sinf(ang) * SQRT2;

    if (v->fresh) {
        for (int o = 0; o < 3; o++) {
            v->p_dt[o] = dt[o];
            v->p_osc[o] = po[o];
        }
        for (int k = 0; k < 5; k++) v->p_lvl[k] = lv[k];
        v->p_g = g;
        v->p_amp = amp;
        v->p_gl = gl;
        v->p_gr = gr;
        v->p_pod = pod;
        v->p_pdist = pdist;
        v->fresh = 0;
    }

    /* ---- audio rate ---- */
    float fmc[4]; /* phase-modulation index in cycles per unit of modulator: O1>O2, O2>O3, O3>O1, Noise>O1 */
    int fm_on = 0;
    for (int k = 0; k < 4; k++) {
        float f = clampf(bp->fm[k] + acc[MD_FM12 + k], 0.0f, 1.0f);
        fmc[k] = f * f * 1.5f;
        fm_on |= fmc[k] > 1e-5f || v->p_fmc[k] > 1e-5f;
    }
    float fo3 = clampf(bp->flt_o3 + acc[MD_O3_FLT], -1.0f, 1.0f);
    float fns = clampf(bp->flt_ns + acc[MD_NS_FLT], -1.0f, 1.0f);
    int ff_on = fabsf(fo3) > 0.002f || fabsf(fns) > 0.002f;
    int use_noise = v->p_lvl[3] > 0.0f || lv[3] > 0.0f;
    float nzb[CREST_SUB];
    if (use_noise || fmc[3] > 0.0f || v->p_fmc[3] > 0.0f || fabsf(fns) > 0.002f) {
        for (int i = 0; i < n; i++) {
            float nz = rng_bi(&v->rng);
            if (bp->nz_mode) {
                v->nz_lp += bp->nz_a * (nz - v->nz_lp);
                nz = bp->nz_mode == 1 ? v->nz_lp * bp->nz_boost : nz - v->nz_lp;
            }
            nzb[i] = nz;
        }
    } else {
        memset(nzb, 0, sizeof nzb);
    }

    float tmp[3][CREST_SUB];
    float inv_n = 1.0f / (float)n;
    if (fm_on) { /* true phase modulation, one oscillator block after the other; O3>O1 uses O3's previous block */
        float pm[CREST_SUB];
        for (int i = 0; i < n; i++) {
            float fr = (float)(i + 1) * inv_n;
            float c31 = v->p_fmc[2] + (fmc[2] - v->p_fmc[2]) * fr, cn1 = v->p_fmc[3] + (fmc[3] - v->p_fmc[3]) * fr;
            pm[i] = c31 * v->o3_prev[i] + cn1 * nzb[i];
        }
        osc_render_pm(&v->osc[0], tmp[0], n, v->p_dt[0], dt[0], &v->p_osc[0], &po[0], pm);
        for (int o = 1; o < 3; o++) {
            for (int i = 0; i < n; i++) {
                float fr = (float)(i + 1) * inv_n;
                pm[i] = (v->p_fmc[o - 1] + (fmc[o - 1] - v->p_fmc[o - 1]) * fr) * tmp[o - 1][i];
            }
            osc_render_pm(&v->osc[o], tmp[o], n, v->p_dt[o], dt[o], &v->p_osc[o], &po[o], pm);
        }
        memcpy(v->o3_prev, tmp[2], sizeof(float) * (size_t)n);
    } else {
        memset(v->o3_prev, 0, sizeof v->o3_prev);
        for (int o = 0; o < 3; o++) {
            int need = v->p_lvl[o] > 0.0f || lv[o] > 0.0f || (o < 2 && (v->p_lvl[4] > 0.0f || lv[4] > 0.0f)) ||
                       (o == 2 && ff_on);
            if (need) osc_render(&v->osc[o], tmp[o], n, v->p_dt[o], dt[o], &v->p_osc[o], &po[o]);
            else osc_advance(&v->osc[o], n, v->p_dt[o], dt[o]);
        }
    }
    int use_od = v->p_pod > 0.001f || pod > 0.001f;
    float buf[CREST_SUB];
    for (int i = 0; i < n; i++) {
        float fr = (float)(i + 1) * inv_n;
        float l0 = v->p_lvl[0] + (lv[0] - v->p_lvl[0]) * fr;
        float l1v = v->p_lvl[1] + (lv[1] - v->p_lvl[1]) * fr;
        float l2v = v->p_lvl[2] + (lv[2] - v->p_lvl[2]) * fr;
        float l3 = v->p_lvl[3] + (lv[3] - v->p_lvl[3]) * fr;
        float l4 = v->p_lvl[4] + (lv[4] - v->p_lvl[4]) * fr;
        float s = 0.0f;
        if (l0 > 0.0f) s += l0 * tmp[0][i];
        if (l1v > 0.0f) s += l1v * tmp[1][i];
        if (l2v > 0.0f) s += l2v * tmp[2][i];
        if (l4 > 0.0f) s += l4 * tmp[0][i] * tmp[1][i];
        if (use_noise) s += l3 * nzb[i];
        s = mix_sat(s * 0.5f);
        if (use_od) { /* pre-filter overdrive: tanh with first-order antiderivative anti-aliasing */
            float p = v->p_pod + (pod - v->p_pod) * fr;
            float gg = 1.0f + 15.0f * p * p;
            float dx = s - v->od_xp;
            float y = fabsf(dx) > 1e-5f ? (logcosh_f(gg * s) - logcosh_f(gg * v->od_xp)) / (gg * dx)
                                        : tanhf(gg * 0.5f * (s + v->od_xp));
            v->od_xp = s;
            s = y / (1.0f + 0.6f * p);
        }
        buf[i] = s * in_gain;
    }
    float gmul[CREST_SUB];
    if (ff_on)
        for (int i = 0; i < n; i++) gmul[i] = exp2f(clampf((fo3 * tmp[2][i] + fns * nzb[i]) * 3.0f, -6.0f, 6.0f));
    filt_process(&v->flt, bp->ftype, buf, n, v->p_g, g, k_res, sat_a, &v->rng, ff_on ? gmul : NULL);

    if (v->p_pdist > 0.001f || pdist > 0.001f) { /* post-filter distortion: asymmetric, with a DC blocker */
        const float hp = 1.0f - expf(-CREST_TAU * 10.0f / CREST_FS);
        for (int i = 0; i < n; i++) {
            float fr = (float)(i + 1) * inv_n;
            float p = v->p_pdist + (pdist - v->p_pdist) * fr;
            float gg = 1.0f + 24.0f * p * p, b = 0.15f * p, tb = tanhf(b);
            float x = buf[i] * out_gain;
            float dx = x - v->pd_xp;
            float y;
            if (fabsf(dx) > 1e-5f) {
                float F = logcosh_f(gg * x + b) / gg - x * tb;
                float Fp = logcosh_f(gg * v->pd_xp + b) / gg - v->pd_xp * tb;
                y = (F - Fp) / dx;
            } else {
                y = tanhf(gg * 0.5f * (x + v->pd_xp) + b) - tb;
            }
            v->pd_xp = x;
            y /= 1.0f + p;
            v->pd_dc += (y - v->pd_dc) * hp;
            buf[i] = y - v->pd_dc;
        }
    } else {
        for (int i = 0; i < n; i++) buf[i] *= out_gain;
    }

    float fade0 = v->fade, fade1 = fade0;
    if (v->fading) {
        fade1 = fade0 - FADE_STEP;
        if (fade1 < 0.0f) fade1 = 0.0f;
    }
    for (int i = 0; i < n; i++) {
        float fr = (float)(i + 1) * inv_n;
        float a = (v->p_amp + (amp - v->p_amp) * fr) * (fade0 + (fade1 - fade0) * fr);
        float s = buf[i] * a;
        outl[i] += s * (v->p_gl + (gl - v->p_gl) * fr);
        outr[i] += s * (v->p_gr + (gr - v->p_gr) * fr);
    }
    v->fade = fade1;

    for (int o = 0; o < 3; o++) {
        v->p_dt[o] = dt[o];
        v->p_osc[o] = po[o];
    }
    for (int k = 0; k < 5; k++) v->p_lvl[k] = lv[k];
    v->p_g = g;
    v->p_amp = amp;
    v->p_gl = gl;
    v->p_gr = gr;
    v->p_pod = pod;
    v->p_pdist = pdist;
    for (int k = 0; k < 4; k++) v->p_fmc[k] = fmc[k];
}
