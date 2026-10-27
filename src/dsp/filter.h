/* Multimode TPT state-variable filter with saturated state (plan/05 section 3). */
#ifndef CREST_FILTER_H
#define CREST_FILTER_H

#include <stdint.h>

enum { FLT_LP24 = 0, FLT_LP12, FLT_BP12, FLT_BP6, FLT_HP24, FLT_HP12 };

typedef struct {
    float ic1, ic2;
} svf_t;

typedef struct {
    svf_t s[2];
} filt_t;

void filt_reset(filt_t *f);

/* Resonance knob (0..1) to the damping term k: 2 = no resonance, < 0 = self-oscillation. */
float filt_res_to_k(float res);

/* Filter x[0..n) in place. The prewarped coefficient g = tan(pi fc / fs) ramps g0 -> g1.
 * sat_a > 0 sets the soft-saturation of the band-pass state (larger = earlier onset). */
/* gmul (optional, n values) scales g per sample for audio-rate cutoff modulation. */
void filt_process(filt_t *f, int type, float *x, int n, float g0, float g1, float k, float sat_a,
                  uint32_t *rng, const float *gmul);

#endif
