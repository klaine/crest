#include "env.h"

#include <math.h>

#include "dsp_util.h"

float env_time(float x, float tmax) {
    return 0.0005f * powf(tmax / 0.0005f, clampf(x, 0.0f, 1.0f));
}

/* Release knob law: the same 0.5 ms .. tmax span, bent by x^0.6 so the lower half of the knob gives longer
 * tails (Peak release 52/127 was a 40 ms blip; now about 280 ms). The ends are unchanged. */
float env_release_time(float x) {
    return env_time(powf(clampf(x, 0.0f, 1.0f), ENV_RELEASE_BEND), ENV_RELEASE_MAX);
}

static float delay_time(float x) {
    return x <= 0.0f ? 0.0f : 0.001f * powf(10000.0f, clampf(x, 0.0f, 1.0f));
}

void env_trigger(env_t *e, const env_par_t *p, int hard_reset) {
    if (hard_reset) e->y = 0.0f;
    e->reps = p->rep >= ENV_REP_LOOP ? 0 : p->rep;
    e->dly_left = delay_time(p->dly);
    e->stage = e->dly_left > 0.0f ? ES_DELAY : ES_ATTACK;
}

void env_release(env_t *e, const env_par_t *p) {
    (void)p;
    if (e->stage == ES_IDLE) return;
    if (e->y < 0.001f) {
        e->y = 0.0f;
        e->stage = ES_IDLE;
        return;
    }
    e->rel_ln = logf((e->y + 0.01f) / 0.01f);
    e->stage = ES_RELEASE;
}

/* End of attack: hold at the peak (if any), then decay. */
static void start_hold(env_t *e, const env_par_t *p) {
    e->hold_left = clampf(p->hold, 0.0f, 1.0f) * ENV_HOLD_MAX;
    e->stage = e->hold_left > 0.0f ? ES_HOLD : ES_DECAY;
}

/* Analog-style one-pole segments: attack aims at 3.0 and hands over at 1.0 (gently convex, near-linear), decay aims
 * just below sustain, release aims just below zero. The time constants are chosen so each segment
 * lasts its panel time regardless of level. */
float env_tick(env_t *e, const env_par_t *p) {
    switch (e->stage) {
    case ES_DELAY:
        e->dly_left -= CREST_CTL_DT;
        if (e->dly_left <= 0.0f) e->stage = ES_ATTACK;
        break;
    case ES_ATTACK: {
        float c = expf(-0.4055f * CREST_CTL_DT / env_time(p->a, ENV_ATTACK_MAX));
        e->y = 3.0f + (e->y - 3.0f) * c;
        if (e->y >= 1.0f) {
            e->y = 1.0f;
            start_hold(e, p);
        }
        break;
    }
    case ES_HOLD:
        e->hold_left -= CREST_CTL_DT;
        if (e->hold_left <= 0.0f) e->stage = ES_DECAY;
        break;
    case ES_DECAY: {
        float s = p->s;
        if (e->y <= s + 0.002f) {
            e->y = s;
        } else {
            float ln = logf((1.0f - s + 0.01f) / 0.01f);
            float c = expf(-ln * CREST_CTL_DT / env_time(p->d, ENV_DECAY_MAX));
            float target = s - 0.01f;
            e->y = target + (e->y - target) * c;
        }
        if (e->y <= s + 0.002f) {
            e->y = s;
            if (p->rep >= ENV_REP_LOOP || e->reps > 0) { /* attack, hold and decay again */
                if (p->rep < ENV_REP_LOOP) e->reps--;
                e->stage = ES_ATTACK;
            } else {
                e->stage = ES_SUSTAIN;
            }
        }
        break;
    }
    case ES_SUSTAIN: e->y = p->s; break;
    case ES_RELEASE: {
        float c = expf(-e->rel_ln * CREST_CTL_DT / env_release_time(p->r));
        e->y = -0.01f + (e->y + 0.01f) * c;
        if (e->y <= 0.0f) {
            e->y = 0.0f;
            e->stage = ES_IDLE;
        }
        break;
    }
    default: break;
    }
    return e->y;
}
