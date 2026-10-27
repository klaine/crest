/* Low-frequency oscillators, ticked at control rate (plan/06 section 2). */
#ifndef CREST_LFO_H
#define CREST_LFO_H

#include <stdint.h>

enum { LW_SINE = 0, LW_TRI, LW_SAWUP, LW_SAWDN, LW_SQUARE, LW_SH, LW_RND, LW_EXP };
/* Key restarts at the panel phase on each note (with Repeats 1 it is a one-shot). Common: all voices
 * share one free-running phase; CommonKey: the shared phase also restarts on every new note. */
enum { LT_FREE = 0, LT_KEY, LT_COMMON, LT_COMMONKEY };
enum { LR_LOW = 0, LR_HIGH, LR_WIDE };               /* Wide: LFO 3/4, Low and High in one knob */
enum { LF_FADEIN = 0, LF_FADEOUT, LF_GATEIN, LF_GATEOUT };

/* The control rate is 2756 Hz, so a free rate is capped below its Nyquist limit; the knob still
 * spans the Peak's ranges (Low 0.02..200 Hz, High 0.16..1600 Hz). */
#define LFO_MAX_HZ 1240.0f
#define LFO_FADE_MAX 10.0f /* seconds at full Fade Time */

typedef struct {
    float phase;     /* 0..1 */
    float p0;        /* phase when the LFO was last (re)triggered: where repeats end */
    float sh_prev, sh_next;
    float slew_y;
    float t;         /* seconds since the last trigger (delay, fade) */
    float trav;      /* cycles run since the last trigger (repeats) */
    uint8_t done;    /* repeats finished: holds the end value */
    uint32_t rng;
} lfo_t;

/* Shared phase and random values of a Common-mode LFO (all voices read the same). */
typedef struct {
    float phase, sh_prev, sh_next;
} lfo_shared_t;

/* Normalised panel values (shared by every voice). */
typedef struct {
    int wave, sync, trig, range, rep, fmode, mtrig, fsync;
    float rate, phase, dly, fade, slew;
} lfo_par_t;

/* Restart everything: Key starts at the panel phase; Free LFOs may start anywhere (random_phase). */
void lfo_start(lfo_t *l, const lfo_par_t *p, int random_phase);
/* Restart the waveform at the panel phase (a key-triggered LFO on a new note). */
void lfo_rephase(lfo_t *l, const lfo_par_t *p);
/* Restart the delay, fade and repeat count, keeping the phase (every new note). */
void lfo_retime(lfo_t *l);

/* Beats per cycle for a sync setting (0 for Off). */
float lfo_sync_beats(int sync);

/* One control tick. rate_mod is added to the normalised rate (free) or scales the synced rate by
 * 2^(4 mod). beat < 0 means no host clock. shared (if not NULL) is used by Common mode.
 * Returns a bipolar value in -1..1. */
float lfo_tick(lfo_t *l, const lfo_par_t *p, float rate_mod, double beat, float bpm, const lfo_shared_t *shared);

/* Rate in Hz of a free-running (or synced, at this bpm) LFO, after the cap. */
float lfo_rate_hz(const lfo_par_t *p, float rate_mod, float bpm);

/* Value of a waveform at phase ph (S&H and Rnd use the two random values). */
float lfo_wave(int wave, float ph, float sh_prev, float sh_next);

#endif
