#include "filter.h"

#include "dsp_util.h"

enum { MODE_LP = 0, MODE_BP = 1, MODE_HP = 2 };

#define BP_KMIN 0.5f /* band-pass output scale floor: self-oscillation about 8 dB below the low-pass */

void filt_reset(filt_t *f) {
    f->s[0].ic1 = f->s[0].ic2 = 0.0f;
    f->s[1].ic1 = f->s[1].ic2 = 0.0f;
}

float filt_res_to_k(float res) {
    return 2.0f - 2.08f * powf(clampf(res, 0.0f, 1.0f), 0.9f);
}

/* Cytomic/Simper TPT SVF stage. The band-pass state is soft-saturated, which bounds
 * self-oscillation and gives the drive its character. */
static inline float stage(svf_t *s, float x, float g, float k, int mode, float a, float ia) {
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;
    float v3 = x - s->ic2;
    float v1 = a1 * s->ic1 + a2 * v3;
    float v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
    s->ic1 = fast_tanh((2.0f * v1 - s->ic1) * a) * ia;
    s->ic2 = clampf(2.0f * v2 - s->ic2, -8.0f, 8.0f);
    switch (mode) {
    case MODE_LP: return v2;
    /* Unity gain at the centre frequency, until the resonance is high: k falls through 0 to -0.08 at
     * self-oscillation, which would mute the ringing. Below BP_KMIN the peak grows instead, as a low-pass's. */
    case MODE_BP: return (k > BP_KMIN ? k : BP_KMIN) * v1;
    default: return x - k * v1 - v2;
    }
}

void filt_process(filt_t *f, int type, float *x, int n, float g0, float g1, float k, float sat_a,
                  uint32_t *rng, const float *gmul) {
    float g = g0, dg = (g1 - g0) / (float)n;
    float a = sat_a > 0.05f ? sat_a : 0.05f, ia = 1.0f / a;
    const float k1 = 1.41421356f; /* first stage of the 24 dB modes: not resonant */
    for (int i = 0; i < n; i++) {
        float gi = g;
        if (gmul) {
            gi = g * gmul[i];
            if (gi > 6.0f) gi = 6.0f;
        }
        float in = x[i];
        /* A whisper of noise lets a self-oscillating filter start without input. */
        if (k < 0.2f) in += 1e-6f * rng_bi(rng);
        float y;
        switch (type) {
        case FLT_LP24:
            y = stage(&f->s[1], stage(&f->s[0], in, gi, k1, MODE_LP, a, ia), gi, k, MODE_LP, a, ia);
            break;
        case FLT_LP12: y = stage(&f->s[0], in, gi, k, MODE_LP, a, ia); break;
        case FLT_BP12: y = stage(&f->s[0], in, gi, k, MODE_BP, a, ia); break;
        case FLT_BP6: y = stage(&f->s[0], in, gi, k > 1.0f ? k : 1.0f, MODE_BP, a, ia); break;
        case FLT_HP24:
            y = stage(&f->s[1], stage(&f->s[0], in, gi, k1, MODE_HP, a, ia), gi, k, MODE_HP, a, ia);
            break;
        default: y = stage(&f->s[0], in, gi, k, MODE_HP, a, ia); break;
        }
        x[i] = y;
        g += dg;
    }
}
