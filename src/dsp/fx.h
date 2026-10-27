/* Global effects chain: Distortion -> Chorus -> Delay -> Reverb (plan/10).
 *
 * The algorithms are adapted from Amino's src/dsp/fx.c (MIT, same author): the delay line, ADAA
 * drive, chorus, tempo-synced delay with crossfaded taps, and the 8-line FDN reverb. Amino's
 * generic two-slot machinery is replaced by fixed per-effect buffers inside the instance. */
#ifndef CREST_FX_H
#define CREST_FX_H

#include <stdint.h>

#include "params_gen.h"

#define FX_CHO_N 3540   /* 80 ms per channel */
#define FX_DLY_N 92620  /* 2.1 s per channel */
#define FX_RL_N 5200    /* reverb line */
#define FX_DF_N 448     /* reverb diffuser */
#define FX_PRE_N 22320  /* reverb pre-delay, 0.506 s */

#define FX_CHO_FLOATS (2 * FX_CHO_N)
#define FX_DLY_FLOATS (2 * FX_DLY_N)
#define FX_REV_FLOATS (8 * FX_RL_N + 8 * FX_DF_N + 2 * FX_PRE_N)
#define FX_MEM_FLOATS (FX_CHO_FLOATS + FX_DLY_FLOATS + FX_REV_FLOATS)

typedef struct {
    float *buf;
    int size, w;
} dl_t;

typedef struct {
    uint8_t active, dirty, need_clear;
    uint32_t pos, len;
    uint32_t idle; /* samples since the effect was last wanted */
} fxz_t;

typedef struct {
    /* distortion */
    float d_hp[2], d_lp[2], d_xp[2], d_fp[2], d_hold[2], d_cnt;
    /* chorus */
    dl_t c_d[2];
    float c_phase, c_lp[2], c_hp[2];
    /* delay */
    dl_t y_d[2];
    float y_dt, y_dstart, y_dtarget, y_dfade, y_lp[2], y_hp[2];
    float y_rt, y_rstart, y_rtarget; /* right channel taps (L/R Ratio) */
    /* reverb */
    dl_t r_line[8], r_diff[2][4], r_pre[2];
    int r_len[8];
    float r_gain[8], r_damp[8], r_mph[8], r_hp[2], r_olp[2], r_ohp[2];
    /* wet levels used for the per-block ramps: distortion, chorus, delay, reverb */
    float wet_last[4];
    fxz_t z[3]; /* chorus, delay, reverb activity and incremental clearing */
    uint32_t z_off[3], z_bytes[3];
    /* Everything above is reset by create_instance; this arena is cleared incrementally. */
    float mem[FX_MEM_FLOATS];
} fx_t;

/* Wire up buffers. was_used: the arena may hold old audio and must be cleared before reuse. */
void fx_init(fx_t *f, int was_used);
/* Drop all effect state and tails (preset or state load). */
void fx_reset(fx_t *f);
/* Process n <= 256 frames in place. pv = the instance parameter values. */
void fx_process(fx_t *f, const float *pv, float bpm, float *l, float *r, int n);

#endif
