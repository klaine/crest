/* Small realtime-safe helpers shared by the DSP sources. */
#ifndef CREST_DSP_UTIL_H
#define CREST_DSP_UTIL_H

#include <math.h>
#include <stdint.h>

#define CREST_FS 44100.0f
#define CREST_TAU 6.28318530717958647692f
#define CREST_PI 3.14159265358979323846f

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline int clampi(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Rational (Pade) tanh approximation, within ~0.025 of tanh; exactly +-1 at |x| = 3,
 * so clamping there keeps it continuous and bounded. Same curve as Amino's am_tanh. */
static inline float fast_tanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline uint32_t rng_u32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x1234567u;
    return *s;
}

/* Uniform in [-1, 1). */
static inline float rng_bi(uint32_t *s) {
    return (float)(rng_u32(s) & 0xFFFFFFu) * (2.0f / 16777216.0f) - 1.0f;
}

/* MIDI note number (fractional) to Hz. */
static inline float semis_to_hz(float semis) {
    return 440.0f * exp2f((semis - 69.0f) / 12.0f);
}

/* Flush denormals to zero for the duration of render_block (aarch64 FPCR.FZ). */
#if defined(__aarch64__)
static inline uint64_t ftz_enter(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v));
    __asm__ volatile("msr fpcr, %0" : : "r"(v | (1ull << 24)));
    return v;
}
static inline void ftz_exit(uint64_t v) {
    __asm__ volatile("msr fpcr, %0" : : "r"(v));
}
#else
static inline uint64_t ftz_enter(void) { return 0; }
static inline void ftz_exit(uint64_t v) { (void)v; }
#endif

#endif
