#include "osc.h"

#include <math.h>
#include <stddef.h>

#include "dsp_util.h"
#include "wavetables.h"

#define PH_SCALE 4294967296.0f

/* ------------------------------------------------------------------ polyBLEP */

/* polyBLEP step residual r(x) (plan/04 3.1). x is the sample's distance from the edge in samples,
 * negative before the edge. A corrected edge is naive + jump * r(x). t is the sample's phase
 * since the edge (in [0,1)), dt the phase increment. dt = 0 yields no correction. */
static inline float blep_res(float t, float dt) {
#if CREST_BLEP_POINTS == 4
    if (t < 2.0f * dt) {
        float x = t / dt;
        if (x < 1.0f) return x * (2.0f / 3.0f + x * x * (-1.0f / 3.0f + x * 0.125f)) - 0.5f;
        float u = 2.0f - x;
        return -(u * u * u * u) * (1.0f / 24.0f);
    }
    if (t > 1.0f - 2.0f * dt) {
        float x = (t - 1.0f) / dt;
        if (x > -1.0f) return x * (2.0f / 3.0f + x * x * (-1.0f / 3.0f - x * 0.125f)) + 0.5f;
        float u = 2.0f + x;
        return (u * u * u * u) * (1.0f / 24.0f);
    }
    return 0.0f;
#else
    if (t < dt) {
        float u = 1.0f - t / dt;
        return -0.5f * u * u;
    }
    if (t > 1.0f - dt) {
        float u = 1.0f + (t - 1.0f) / dt;
        return 0.5f * u * u;
    }
    return 0.0f;
#endif
}

/* 2-point polyBLAMP residual (integral of the 2-point BLEP residual), x in samples. */
static inline float blamp_res(float x) {
    if (x <= -1.0f || x >= 1.0f) return 0.0f;
    float u = x < 0.0f ? 1.0f + x : 1.0f - x;
    return u * u * u * (1.0f / 6.0f);
}

/* ------------------------------------------------------------------ wavetable read */

typedef struct {
    const int16_t *tab; /* start of the selected table */
    int mip_off, len, mask;
} wtc_t;

static inline float wt_read(const wtc_t *w, float shape, float t) {
    float fp = shape * (float)(WT_FRAMES - 1);
    int f0 = (int)fp;
    if (f0 > WT_FRAMES - 2) f0 = WT_FRAMES - 2;
    float mix = fp - (float)f0;
    const int16_t *a = w->tab + f0 * WT_FRAME_STRIDE + w->mip_off;
    const int16_t *b = a + WT_FRAME_STRIDE;
    float pos = t * (float)w->len;
    int i = (int)pos;
    float f = pos - (float)i;
    int i0 = i & w->mask, i1 = (i + 1) & w->mask;
    float a0 = (float)a[i0], a1 = (float)a[i1], b0 = (float)b[i0], b1 = (float)b[i1];
    float va = a0 + (a1 - a0) * f;
    float vb = b0 + (b1 - b0) * f;
    return (va + (vb - va) * mix) * (1.0f / 32768.0f);
}

static void wt_setup(wtc_t *w, int table, float f_hz) {
    int k = 0;
    if (f_hz > 86.13f) k = (int)ceilf(logf(f_hz / 86.13f) * 1.44269504f);
    k = clampi(k, 0, WT_LEVELS - 1);
    w->tab = WT_DATA + table * WT_FRAMES * WT_FRAME_STRIDE;
    w->mip_off = WT_LEVEL_OFF[k];
    w->len = WT_LEN[k];
    w->mask = w->len - 1;
}

/* ------------------------------------------------------------------ waveforms */

static inline float wave_sine(float t, float dt, float shape) {
    float s = sin_cycle(t);
    if (shape <= 0.001f) return s;
    /* Self phase modulation: adds harmonics smoothly. Attenuated at high pitch (aliasing). */
    float f0 = dt * CREST_FS;
    float k = 2.2f * shape * shape * (f0 > 4000.0f ? 4000.0f / f0 : 1.0f);
    return sin_cycle(t + k * s * (1.0f / CREST_TAU));
}

static inline float wave_saw(float t, float dt, float shape) {
    float a = (2.0f * t - 1.0f) - 2.0f * blep_res(t, dt);
    if (shape <= 0.0005f) return a;
    /* "Double saw": a second saw half a cycle * shape later; shape 1 sounds an octave up. */
    float t2 = t + 0.5f * shape;
    if (t2 >= 1.0f) t2 -= 1.0f;
    float b = (2.0f * t2 - 1.0f) - 2.0f * blep_res(t2, dt);
    return 0.5f * (a + b);
}

static inline float wave_pulse(float t, float dt, float shape) {
    float pw = 0.5f - 0.47f * shape; /* 50% -> 3% */
    /* Zero-mean pulse normalised to a peak of 1: high = 1, low = -pw / (1 - pw). */
    float J = 1.0f / (1.0f - pw);
    float naive = t < pw ? 1.0f : -pw * J;
    float tp = t - pw;
    if (tp < 0.0f) tp += 1.0f;
    return naive + J * blep_res(t, dt) - J * blep_res(tp, dt);
}

static inline float wave_tri(float t, float dt, float shape) {
    /* Rising fraction. Each ramp is kept at least two samples long: a shorter one puts both corners'
     * polyBLAMP corrections on the same samples, where they no longer cancel and spike far past +-1. */
    float w = clampf(0.5f - 0.49f * shape, 2.0f * dt, 1.0f - 2.0f * dt);
    float y = t < w ? -1.0f + 2.0f * t / w : 1.0f - 2.0f * (t - w) / (1.0f - w);
    float D = 2.0f / (w * (1.0f - w)) * dt; /* slope change per sample at each corner */
    if (dt > 0.0f) {
        float x0 = 0.0f;
        int c0 = 1;
        if (t < dt) x0 = t / dt;
        else if (t > 1.0f - dt) x0 = (t - 1.0f) / dt;
        else c0 = 0;
        if (c0) y += D * blamp_res(x0);
        float d = t - w;
        if (d > -dt && d < dt) y -= D * blamp_res(d / dt);
    }
    return y;
}

/* Band-limited sample of `wave` at phase t. dt = 0 gives the naive (aliased) value, which is what
 * the virtual-sync jump estimate needs. Above 0.2 fs the classic waves fade to a sine: a
 * band-limited saw/pulse/triangle there holds only the fundamental. */
static inline float wave_bl(int wave, float t, float dt, float shape, const wtc_t *wt) {
    if (wave == WAVE_WT) return wt_read(wt, shape, t);
    if (wave == WAVE_SINE) return wave_sine(t, dt, shape);
    float sm = clampf((dt - 0.2f) * 20.0f, 0.0f, 1.0f);
    if (sm >= 1.0f) return sin_cycle(t);
    float y;
    switch (wave) {
    case WAVE_TRI: y = wave_tri(t, dt, shape); break;
    case WAVE_SAW: y = wave_saw(t, dt, shape); break;
    default: y = wave_pulse(t, dt, shape); break;
    }
    if (sm > 0.0f) y += sm * (sin_cycle(t) - y);
    return y;
}

/* ------------------------------------------------------------------ one oscillator unit */

/* Render one sample of unit u and advance its phases. With sync on, the waveform is read from the
 * slave phase, which runs at ratio r and restarts whenever the master phase wraps; the output jump at
 * that reset gets the same polyBLEP correction as any other edge. */
static inline float frac1(float x) { return x - floorf(x); }

/* pm / dpm: phase-modulation offset of this sample and its change since the last one (cycles). The
 * waveform is read at the offset phase with the instantaneous increment as edge width; the
 * accumulator itself only ever advances by dt. */
static inline float unit_run(osc_t *o, int u, float dt, float r, int sync, int wave, float shape,
                             const wtc_t *wt, float pm, float dpm) {
    float t = (float)o->ph[u] * (1.0f / PH_SCALE);
    uint32_t inc = (uint32_t)(dt * PH_SCALE);
    float y;
    if (!sync) {
        if (pm != 0.0f || dpm != 0.0f) {
            float de = clampf(fabsf(dt + dpm), 1e-6f, 0.49f);
            y = wave_bl(wave, frac1(t + pm), de, shape, wt);
        } else {
            y = wave_bl(wave, t, dt, shape, wt);
        }
        o->ph[u] += inc;
        return y;
    }
    float s = (float)o->sph[u] * (1.0f / PH_SCALE);
    float dsl = dt * r;
    float dts = clampf(fabsf(dsl + dpm), 1e-6f, 0.49f);
    y = wave_bl(wave, pm != 0.0f ? frac1(s + pm) : s, dts, shape, wt);
    float Jv = 0.0f;
    int near = t >= 1.0f - 2.0f * dt; /* inside the pre-edge BLEP window (2 or 4 point) */
    if (t < 0.5f) {
        Jv = o->J[u];
    } else if (near) { /* about to wrap: predict the jump the reset will cause */
        float sw = s + r * (1.0f - t);
        sw -= floorf(sw);
        Jv = wave_bl(wave, 0.0f, 0.0f, shape, wt) - wave_bl(wave, sw, 0.0f, shape, wt);
    }
    y += Jv * blep_res(t, dt);
    uint32_t old = o->ph[u];
    o->ph[u] = old + inc;
    if (o->ph[u] < old) { /* wrapped: the slave restarts, keeping the overshoot */
        float tn = (float)o->ph[u] * (1.0f / PH_SCALE);
        if (near && t >= 0.5f) { /* the jump predicted above */
            o->J[u] = Jv;
        } else {
            float sw = s + r * (1.0f - t);
            sw -= floorf(sw);
            o->J[u] = wave_bl(wave, 0.0f, 0.0f, shape, wt) - wave_bl(wave, sw, 0.0f, shape, wt);
        }
        float ns = r * tn;
        ns -= floorf(ns);
        o->sph[u] = (uint32_t)(ns * PH_SCALE);
    } else {
        o->sph[u] += (uint32_t)((dsl - floorf(dsl)) * PH_SCALE);
    }
    return y;
}

static void render_one(osc_t *o, int wave, int table, float *out, int n, float dt0, float dt1,
                       const oscp_t *a, const oscp_t *b, const float *pm) {
    float dt = dt0, ddt = (dt1 - dt0) / (float)n;
    float sh = a->shape, dsh = (b->shape - a->shape) / (float)n;
    float dn = a->dens, ddn = (b->dens - a->dens) / (float)n;
    float dvs = (b->vsync - a->vsync) / (float)n;
    int sync = a->vsync > 0.001f || b->vsync > 0.001f;
    float rmax = exp2f(4.0f * (a->vsync > b->vsync ? a->vsync : b->vsync));
    float dtmax = dt0 > dt1 ? dt0 : dt1;
    wtc_t wt = {0, 0, 1, 0};
    if (wave == WAVE_WT) wt_setup(&wt, table, dtmax * (sync ? rmax : 1.0f) * CREST_FS);
    int copies = wave != WAVE_SINE && wave != WAVE_WT && (a->dens > 0.001f || b->dens > 0.001f);
    float ratio[3] = {1, 1, 1};
    if (copies) {
        static const float OFF[3] = {-1.0f, 1.0f, -0.5f};
        float dd = b->ddet * b->ddet * b->ddet * 35.0f; /* cents, cubic for fine control */
        for (int k = 0; k < 3; k++) ratio[k] = exp2f(OFF[k] * dd * (1.0f / 1200.0f));
    }
    /* Sync ratio 2^(4 vsync): vsync ramps linearly, so the ratio steps by a constant factor. */
    float r = exp2f(4.0f * clampf(a->vsync, 0.0f, 1.0f)), rmul = exp2f(4.0f * dvs);
    float pprev = o->pm_prev;
    for (int i = 0; i < n; i++) {
        float d = clampf(dt, 1e-6f, 0.49f);
        float s = clampf(sh, 0.0f, 1.0f);
        float p = pm ? pm[i] : 0.0f, dp = p - pprev;
        pprev = p;
        float y = unit_run(o, 0, d, r, sync, wave, s, &wt, p, dp);
        if (copies) {
            float norm = 1.0f;
            for (int k = 0; k < 3; k++) {
                float lv = clampf(dn * 3.0f - (float)k, 0.0f, 1.0f);
                if (lv <= 0.001f) {
                    o->ph[k + 1] += (uint32_t)(d * ratio[k] * PH_SCALE);
                    continue;
                }
                float dk = clampf(d * ratio[k], 1e-6f, 0.49f);
                y += lv * unit_run(o, k + 1, dk, r, sync, wave, s, &wt, p, dp);
                norm += lv * lv;
            }
            y *= 1.0f / sqrtf(norm);
        }
        out[i] = y;
        dt += ddt;
        sh += dsh;
        dn += ddn;
        r *= rmul;
    }
    o->pm_prev = pprev;
}

void osc_render_pm(osc_t *o, float *out, int n, float dt0, float dt1, const oscp_t *a, const oscp_t *b,
                   const float *pm) {
    if (n <= 16 && (a->wave != b->wave || (b->wave == WAVE_WT && a->table != b->table))) {
        float old[16];
        osc_t keep = *o; /* the outgoing wave starts from the same phases */
        render_one(&keep, a->wave, a->table, old, n, dt0, dt1, a, b, pm);
        render_one(o, b->wave, b->table, out, n, dt0, dt1, a, b, pm);
        for (int i = 0; i < n; i++) {
            float f = (float)(i + 1) / (float)n;
            out[i] = old[i] + (out[i] - old[i]) * f;
        }
        return;
    }
    render_one(o, b->wave, b->table, out, n, dt0, dt1, a, b, pm);
}

void osc_render(osc_t *o, float *out, int n, float dt0, float dt1, const oscp_t *a, const oscp_t *b) {
    osc_render_pm(o, out, n, dt0, dt1, a, b, NULL);
}

void osc_advance(osc_t *o, int n, float dt0, float dt1) {
    float dt = clampf(0.5f * (dt0 + dt1), 1e-6f, 0.49f);
    double cyc = (double)dt * (double)n;
    cyc -= floor(cyc); /* whole cycles don't move the phase; keeps the float-to-int cast in range */
    uint32_t step = (uint32_t)(cyc * 4294967296.0);
    for (int u = 0; u < 4; u++) o->ph[u] += step;
    o->pm_prev = 0.0f;
}
