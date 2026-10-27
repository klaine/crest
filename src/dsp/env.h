/* Delay-AHDSR envelope with repeats (plan/06 section 1). Ticked at control rate (16 samples). */
#ifndef CREST_ENV_H
#define CREST_ENV_H

#include <stdint.h>

#define CREST_SUB 16
#define CREST_CTL_DT ((float)CREST_SUB / 44100.0f)

enum { ES_IDLE = 0, ES_DELAY, ES_ATTACK, ES_HOLD, ES_DECAY, ES_SUSTAIN, ES_RELEASE };
enum { ENV_MULTI = 0, ENV_SINGLE = 1, ENV_RESET = 2 };

/* Longest time of each stage at full knob, as on the Peak (manual p.23): attack over 18 s, decay
 * about 22 s, release over 24 s; hold at most 500 ms. */
#define ENV_ATTACK_MAX 20.0f
#define ENV_DECAY_MAX 22.0f
#define ENV_RELEASE_MAX 25.0f
#define ENV_HOLD_MAX 0.5f
#define ENV_RELEASE_BEND 0.6f /* release knob exponent (env_release_time) */
#define ENV_REP_LOOP 127 /* repeats value that loops until note-off (the Peak's "On") */

typedef struct {
    uint8_t stage;
    int reps;        /* extra attack/hold/decay cycles still to run */
    float y;         /* current level 0..1 */
    float rel_ln;    /* ln ratio fixing the release curve */
    float dly_left;  /* seconds */
    float hold_left; /* seconds */
} env_t;

/* Normalised (0..1) panel values, shared by every voice. */
typedef struct {
    float a, d, s, r, dly, hold;
    int rep; /* 0..127: extra cycles, ENV_REP_LOOP = loop until note-off */
    int trig;
} env_par_t;

/* 0..1 -> 0.5 ms .. tmax, exponential. */
float env_time(float x, float tmax);
float env_release_time(float x);

void env_trigger(env_t *e, const env_par_t *p, int hard_reset);
void env_release(env_t *e, const env_par_t *p);
float env_tick(env_t *e, const env_par_t *p);

#endif
