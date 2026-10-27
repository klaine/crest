/* Band-limited oscillators: classic waves, wavetables, density stacks, virtual sync (plan/04). */
#ifndef CREST_OSC_H
#define CREST_OSC_H

#include <stdint.h>

/* 2 (default) or 4 point polyBLEP; see plan/04-oscillators.md section 3.1. */
#ifndef CREST_BLEP_POINTS
#define CREST_BLEP_POINTS 2
#endif

enum { WAVE_SINE = 0, WAVE_TRI = 1, WAVE_SAW = 2, WAVE_PULSE = 3, WAVE_WT = 4 };

typedef struct {
    uint32_t ph[4];  /* master phases: the oscillator and its 3 density copies; 2^32 = one cycle */
    uint32_t sph[4]; /* virtual-sync slave phases */
    float J[4];      /* output jump at the most recent sync reset (for the polyBLEP correction) */
    float pm_prev;   /* last phase-modulation offset (cycles), for the instantaneous increment */
} osc_t;

/* Panel values for one sub-block; osc_render ramps from `a` (previous) to `b` (new). */
typedef struct {
    int wave, table;
    float shape; /* pulse width / fold / slope / double-saw / wavetable frame position */
    float dens, ddet;
    float vsync;
} oscp_t;

/* Render n samples. dt = phase increment in cycles/sample, ramped dt0 -> dt1. Advances the phases.
 * If the wave (or wavetable) differs between a and b the two are crossfaded over the call. */
void osc_render(osc_t *o, float *out, int n, float dt0, float dt1, const oscp_t *a, const oscp_t *b);

/* Same, with phase modulation: pm[i] (cycles) is added to the read phase of sample i. The phase
 * accumulators are not touched by pm, so FM never shifts the oscillator's pitch. */
void osc_render_pm(osc_t *o, float *out, int n, float dt0, float dt1, const oscp_t *a, const oscp_t *b,
                   const float *pm);

/* Advance the phase without producing audio (muted oscillators keep their place). */
void osc_advance(osc_t *o, int n, float dt0, float dt1);

#endif
