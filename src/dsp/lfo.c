#include "lfo.h"

#include <math.h>

#include "dsp_util.h"
#include "env.h"

/* Index = lN_sync enum: Off, then the Peak's LFO sync table (manual p.38) from 64 beats to a 1/32
 * triplet, in beats per cycle. */
static const float SYNC_BEATS[36] = {
    0.0f,  64.0f, 48.0f, 42.0f, 36.0f, 32.0f, 30.0f, 28.0f, 24.0f, 64.0f / 3.0f, 20.0f, 56.0f / 3.0f,
    18.0f, 16.0f, 40.0f / 3.0f, 12.0f, 32.0f / 3.0f, 8.0f, 6.0f, 16.0f / 3.0f, 4.0f, 3.0f, 8.0f / 3.0f,
    2.0f,  1.5f,  4.0f / 3.0f, 1.0f, 0.75f, 2.0f / 3.0f, 0.5f, 0.375f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f,
    0.125f, 1.0f / 12.0f};

float lfo_sync_beats(int sync) {
    return sync > 0 && sync < 36 ? SYNC_BEATS[sync] : 0.0f;
}

static float unit_rand(uint32_t *s) {
    return (float)(rng_u32(s) & 0xFFFFFF) * (1.0f / 16777216.0f);
}

void lfo_retime(lfo_t *l) {
    l->t = 0.0f;
    l->trav = 0.0f;
    l->done = 0;
    l->p0 = l->phase;
}

void lfo_rephase(lfo_t *l, const lfo_par_t *p) {
    l->phase = p->phase;
    l->sh_prev = l->sh_next;
    l->sh_next = rng_bi(&l->rng);
}

void lfo_start(lfo_t *l, const lfo_par_t *p, int random_phase) {
    if (!l->rng) l->rng = 0x2545F491u;
    l->phase = random_phase ? unit_rand(&l->rng) : p->phase;
    l->sh_prev = rng_bi(&l->rng);
    l->sh_next = rng_bi(&l->rng);
    l->slew_y = 0.0f;
    lfo_retime(l);
}

float lfo_rate_hz(const lfo_par_t *p, float rate_mod, float bpm) {
    float hz;
    if (p->sync > 0) {
        float beats = lfo_sync_beats(p->sync) / exp2f(4.0f * rate_mod);
        hz = (bpm / 60.0f) / beats;
    } else {
        float x = clampf(p->rate + rate_mod, 0.0f, 1.0f);
        if (p->range == LR_WIDE) hz = 0.02f * powf(80000.0f, x);
        else hz = (p->range == LR_HIGH ? 0.16f : 0.02f) * powf(10000.0f, x);
    }
    return hz < LFO_MAX_HZ ? hz : LFO_MAX_HZ;
}

float lfo_wave(int wave, float ph, float sh_prev, float sh_next) {
    switch (wave) {
    case LW_SINE: return sinf(CREST_TAU * ph);
    case LW_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case LW_SAWUP: return 2.0f * ph - 1.0f;
    case LW_SAWDN: return 1.0f - 2.0f * ph;
    case LW_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    case LW_SH: return sh_next;
    case LW_RND: {
        float x = 0.5f - 0.5f * cosf(CREST_PI * ph);
        return sh_prev + (sh_next - sh_prev) * x;
    }
    default: {
        float u = 1.0f - ph;
        return 2.0f * u * u * u - 1.0f;
    }
    }
}

float lfo_tick(lfo_t *l, const lfo_par_t *p, float rate_mod, double beat, float bpm, const lfo_shared_t *shared) {
    float hz = lfo_rate_hz(p, rate_mod, bpm);
    int common = (p->trig == LT_COMMON || p->trig == LT_COMMONKEY) && shared;
    if (!l->done) {
        float prev = l->phase;
        int locked = !common && p->sync > 0 && beat >= 0.0 && p->trig == LT_FREE;
        if (common) {
            l->phase = shared->phase;
        } else if (locked) {
            /* Locked to the host transport: the same beat always gives the same phase. */
            double cyc = beat * (double)hz * 60.0 / (double)bpm + (double)p->phase;
            l->phase = (float)(cyc - floor(cyc));
        } else {
            l->phase += hz * CREST_CTL_DT;
            l->phase -= floorf(l->phase);
        }
        if ((common || locked) && l->trav == 0.0f) l->p0 = l->phase; /* the phase comes from outside */
        if (common) { /* every voice reads the same random values */
            l->sh_prev = shared->sh_prev;
            l->sh_next = shared->sh_next;
        }
        l->trav += hz * CREST_CTL_DT;
        if (p->rep > 0 && l->trav >= (float)p->rep) {
            /* repeats done: stop where the last cycle ends (just before p0) and hold that value */
            l->done = 1;
            l->phase = l->p0 > 0.0f ? l->p0 : 1.0f;
        } else if (l->phase < prev && !common) { /* a new cycle began */
            l->sh_prev = l->sh_next;
            l->sh_next = rng_bi(&l->rng);
        }
    }
    float v = lfo_wave(p->wave, l->phase, l->sh_prev, l->sh_next);
    if (p->slew > 0.001f) {
        float f = 200.0f * powf(0.0025f, p->slew); /* 200 Hz .. 0.5 Hz */
        float c = 1.0f - expf(-CREST_TAU * f * CREST_CTL_DT);
        l->slew_y += (v - l->slew_y) * c;
        v = l->slew_y;
    }

    /* Delay, then the fade mode over Fade Time, measured from the last trigger. A Fade Time of 0
     * leaves the LFO at full depth whatever the mode (manual p.21). */
    l->t += CREST_CTL_DT;
    float dly = p->dly > 0.0f ? 0.005f * powf(2000.0f, p->dly) : 0.0f;
    if (l->t < dly) return 0.0f;
    float T = p->fade * LFO_FADE_MAX, e = l->t - dly;
    if (T > 0.001f) {
        switch (p->fmode) {
        case LF_FADEOUT: v *= e < T ? 1.0f - e / T : 0.0f; break;
        case LF_GATEIN: if (e < T) v = 0.0f; break;
        case LF_GATEOUT: if (e >= T) v = 0.0f; break;
        default: v *= e < T ? e / T : 1.0f; break;
        }
    }
    return v;
}
