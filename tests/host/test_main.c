/* Native tests: drive Crest through the Schwung plugin API with a fake host (plan/14 section 2). */
#define _POSIX_C_SOURCE 200809L /* nanosleep: the Peak bank test waits for the worker thread */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crest.h"
#include "dsp_util.h"
#include "level.h"

static int g_pass, g_fail;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (cond) {                                                        \
            g_pass++;                                                      \
        } else {                                                           \
            g_fail++;                                                      \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                  \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

static plugin_api_v2_t *api;
static host_api_v1_t host;
static float g_bpm = 120.0f;
static float fake_bpm(void) { return g_bpm; }

/* ------------------------------------------------------------------ helpers */

static void *mk(void) {
    void *h = api->create_instance(".", "");
    return h;
}
static void sp(void *h, const char *k, const char *v) { api->set_param(h, k, v); }
static void spf(void *h, const char *k, double v) {
    char b[40];
    snprintf(b, sizeof b, "%.6f", v);
    sp(h, k, b);
}
static float gp(void *h, const char *k) {
    char b[64];
    int n = api->get_param(h, k, b, sizeof b);
    return n < 0 ? -9999.0f : strtof(b, NULL);
}
static void midi(void *h, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}
static void note_on(void *h, int n, int v) { midi(h, 0x90, n, v); }
static void note_off(void *h, int n) { midi(h, 0x80, n, 0); }

/* Render `frames` (multiple of 128) into L and R. */
static void render(void *h, int frames, float *L, float *R) {
    int16_t blk[256];
    for (int p = 0; p < frames; p += 128) {
        api->render_block(h, blk, 128);
        for (int i = 0; i < 128; i++) {
            if (L) L[p + i] = blk[2 * i] * (1.0f / 32768.0f);
            if (R) R[p + i] = blk[2 * i + 1] * (1.0f / 32768.0f);
        }
    }
}

static float rms(const float *x, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * (double)x[i];
    return (float)sqrt(s / (n > 0 ? n : 1));
}
static float peak(const float *x, int n) {
    float p = 0;
    for (int i = 0; i < n; i++) p = fmaxf(p, fabsf(x[i]));
    return p;
}
static int all_finite(const float *x, int n) {
    for (int i = 0; i < n; i++)
        if (!isfinite(x[i])) return 0;
    return 1;
}
static int up_crossings(const float *x, int n) {
    int c = 0;
    for (int i = 1; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) c++;
    return c;
}
static float *buf(int n) {
    float *p = (float *)calloc((size_t)n, sizeof(float));
    if (!p) exit(2);
    return p;
}

static uint32_t g_rng = 12345;
static float frand(void) { return (float)(rng_u32(&g_rng) & 0xFFFFFF) / 16777216.0f; }

/* Silent default voice: sine, open filter, instant attack, full sustain. */
static void tone_setup(void *h) {
    sp(h, "o1_wave", "Sine");
    spf(h, "flt_cut", 1.0);
    spf(h, "flt_key", 0.0);
    spf(h, "ae_a", 0.0);
    spf(h, "ae_s", 1.0);
    spf(h, "ae_vel", 0.0);
    spf(h, "ae_r", 0.2);
}

static int active_voices(crest_inst_t *I) {
    int n = 0;
    for (int i = 0; i < CREST_VOICES; i++)
        if (I->voice[i].active && !I->voice[i].fading) n++;
    return n;
}

/* ------------------------------------------------------------------ FFT (alias measurement) */

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double a = re[i]; re[i] = re[j]; re[j] = a;
            a = im[i]; im[i] = im[j]; im[j] = a;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                double t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

/* Energy more than 3 bins from any harmonic of f0, relative to the total (dB). */
#define FN 32768
static double alias_db(const float *x, double f0) {
    static double re[FN], im[FN];
    for (int n = 0; n < FN; n++) {
        re[n] = (double)x[n] * (0.5 - 0.5 * cos(2.0 * M_PI * n / FN));
        im[n] = 0;
    }
    fft(re, im, FN);
    double tot = 0, bad = 0;
    for (int k = 1; k < FN / 2; k++) {
        double e = re[k] * re[k] + im[k] * im[k];
        double h = (k * 44100.0 / FN) / f0;
        tot += e;
        if (fabs(h - round(h)) * f0 / (44100.0 / FN) > 3.0) bad += e;
    }
    return 10.0 * log10(bad / tot + 1e-30);
}

/* ================================================================== tests */

static int g_cancel_polls;
static int cancel_after(void *ctx) { /* lets a few blocks render, then cancels */
    (void)ctx;
    return ++g_cancel_polls >= 3;
}

static void test_abi(void) {
    printf("abi\n");
    CHECK(api && api->api_version == 2, "api_version");
    void *h[CREST_MAX_INSTANCES];
    for (int i = 0; i < CREST_MAX_INSTANCES; i++) {
        h[i] = mk();
        CHECK(h[i] != NULL, "instance %d", i);
    }
    CHECK(mk() == NULL, "pool should be exhausted");
    char eb[80];
    CHECK(api->get_error(NULL, eb, sizeof eb) > 0, "pool-full error text");
    /* The loudness solver renders on its own instance, so a full pool does not stop it, and a cancel
     * abandons the render at once. */
    {
        static float lv[P_COUNT];
        for (int i = 0; i < P_COUNT; i++) lv[i] = CREST_PARAMS[i].def;
        double lpk, lrms;
        CHECK(crest_level_measure(api, lv, 0.3, &lpk, &lrms), "level measure with the pool full");
        CHECK(mk() == NULL, "the scratch instance is not handed to the host");
        clock_t t0 = clock();
        double r = crest_level_solve_ex(api, lv, 1, cancel_after, NULL);
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        CHECK(r == CREST_LEVEL_CANCELLED, "cancelled solve returns CREST_LEVEL_CANCELLED (%g)", r);
        CHECK(g_cancel_polls >= 3, "the render polled for a cancel (%d)", g_cancel_polls);
        CHECK(secs < 0.5, "a cancelled solve returns promptly (%.2f s)", secs);
        void *sc = crest_scratch_instance();
        CHECK(sc != NULL, "the scratch instance is free again after a cancel");
        if (sc) api->destroy_instance(sc);
    }
    api->destroy_instance(h[0]);
    h[0] = mk();
    CHECK(h[0] != NULL, "slot reusable after destroy");
    for (int i = 0; i < CREST_MAX_INSTANCES; i++) api->destroy_instance(h[i]);
}

static void test_params(void) {
    printf("params\n");
    void *h = mk();
    for (int i = 1; i < P_COUNT; i++)
        CHECK(strcmp(CREST_PARAMS[CREST_PARAMS_SORTED[i - 1]].key, CREST_PARAMS[CREST_PARAMS_SORTED[i]].key) < 0,
              "sorted index out of order at %d", i);
    for (int id = 0; id < P_COUNT; id++) {
        const param_desc_t *d = &CREST_PARAMS[id];
        float vals[3] = {d->mn, d->mx, d->def};
        for (int k = 0; k < 3; k++) {
            spf(h, d->key, (double)vals[k]);
            float got = gp(h, d->key);
            if (strcmp(d->key, "mm_clear") == 0) continue; /* a trigger: reads back as 0 */
            CHECK(fabsf(got - vals[k]) < 2e-4f, "%s: set %g got %g", d->key, (double)vals[k], (double)got);
        }
        if (d->kind == PK_ENUM && strcmp(d->key, "mm_clear") != 0)
            for (int o = 0; o < d->nopts; o++) {
                sp(h, d->key, d->opts[o]);
                CHECK((int)gp(h, d->key) == o, "%s: option name %s", d->key, d->opts[o]);
            }
        sp(h, d->key, "99999");
        CHECK(gp(h, d->key) <= d->mx + 1e-4f, "%s: not clamped high", d->key);
        sp(h, d->key, "-99999");
        CHECK(gp(h, d->key) >= d->mn - 1e-4f, "%s: not clamped low", d->key);
    }
    char b[64];
    sp(h, "no_such_key", "1");
    CHECK(api->get_param(h, "no_such_key", b, sizeof b) == -1, "unknown key");
    CHECK(api->get_param(h, "chain_params", b, 10) == -1, "small buffer");
    static char big[40000];
    int n = api->get_param(h, "chain_params", big, sizeof big);
    CHECK(n == CREST_CHAIN_PARAMS_LEN && big[0] == '[', "chain_params copy");
    n = api->get_param(h, "ui_hierarchy", big, sizeof big);
    CHECK(n == CREST_UI_HIERARCHY_LEN && strncmp(big, "{\"levels\"", 9) == 0, "ui_hierarchy is bare levels");
    api->destroy_instance(h);
}

static void randomise(void *h) {
    for (int id = 0; id < P_COUNT; id++) {
        const param_desc_t *d = &CREST_PARAMS[id];
        spf(h, d->key, (double)(d->mn + (d->mx - d->mn) * frand()));
    }
}

static void test_state(void) {
    printf("state\n");
    void *a = mk(), *b = mk();
    static char s1[16000], s2[16000];
    for (int t = 0; t < 100; t++) {
        randomise(a);
        sp(a, "name", t & 1 ? "Quote\"Back\\slash" : "Plain");
        int n1 = api->get_param(a, "state", s1, sizeof s1);
        CHECK(n1 > 0, "state size %d", n1);
        sp(b, "state", s1);
        int n2 = api->get_param(b, "state", s2, sizeof s2);
        CHECK(n1 == n2 && strcmp(s1, s2) == 0, "state round trip %d", t);
    }
    CHECK(strncmp(s1, "{\"fmt\":\"crest\"", 14) == 0, "state header");
    CHECK(api->get_param(a, "state", s2, 100) == -1, "state small buffer");

    /* Bad input must not crash or change the patch. */
    spf(b, "flt_cut", 0.321);
    const char *bad[] = {"", "garbage", "{", "{\"fmt\":\"nope\"}", "{\"v\":\"1\",\"flt_cut\":\"0.9\"}",
                         "\"\"\"\"::::,,,}{", "{\"fmt\":\"crest\",\"flt_cut\":\"0.9"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        float before = gp(b, "flt_cut");
        sp(b, "state", bad[i]);
        if (i < 5) CHECK(fabsf(gp(b, "flt_cut") - before) < 1e-6f, "bad state %zu changed the patch", i);
    }
    static char huge[100000];
    memset(huge, 'x', sizeof huge - 1);
    huge[sizeof huge - 1] = 0;
    sp(b, "state", huge);
    s1[strlen(s1) / 2] = 0; /* truncated */
    sp(b, "state", s1);
    CHECK(1, "survived malformed state");
    /* Unknown keys are ignored, missing keys fall back to INIT. */
    sp(b, "state", "{\"fmt\":\"crest\",\"v\":\"9\",\"future_key\":\"3\",\"flt_cut\":\"0.5\",\"name\":\"X\"}");
    CHECK(fabsf(gp(b, "flt_cut") - 0.5f) < 1e-4f, "known key applied");
    CHECK(fabsf(gp(b, "o1_wave") - 2.0f) < 1e-4f, "missing key falls back to INIT");
    api->destroy_instance(a);
    api->destroy_instance(b);
}

static void test_presets(void) {
    printf("presets\n");
    void *h = mk();
    float *L = buf(4096), *R = buf(4096);
    CHECK(CREST_FACTORY_COUNT >= 1, "has presets");
    CHECK((int)gp(h, "preset_count") == CREST_FACTORY_COUNT, "preset_count");
    for (int i = 0; i < CREST_FACTORY_COUNT; i++) {
        char v[16], nm[64];
        snprintf(v, sizeof v, "%d", i);
        sp(h, "preset", v);
        api->get_param(h, "preset_name", nm, sizeof nm);
        CHECK(strcmp(nm, CREST_FACTORY_NAMES[i]) == 0, "preset %d name", i);
        CHECK((int)gp(h, "preset") == i, "preset index");
        note_on(h, 48, 100);
        note_on(h, 55, 100);
        note_on(h, 64, 100);
        render(h, 4096, L, R);
        CHECK(all_finite(L, 4096) && all_finite(R, 4096), "preset %d finite", i);
        float pk = peak(L, 4096), thr = i >= 11 ? 0.0003f : 0.002f;
        /* Imported pads can have long attacks or delays: give them a few seconds. Mute by design: VCA Gain 0,
         * or Dry 0 with every effect level down (Animate / the wheel brings the effects in). */
        int mute_ok = i >= 11 && (gp(h, "vca_gain") <= 0.01f ||
                                  (gp(h, "fx_dry") <= 0.01f && gp(h, "cho_lvl") + gp(h, "dly_lvl") + gp(h, "rev_lvl") <= 0.0f));
        for (int k = 0; k < 40 && pk <= thr && i >= 11 && !mute_ok; k++) {
            render(h, 4096, L, R);
            pk = peak(L, 4096);
        }
        CHECK(pk > thr || mute_ok, "preset %d is silent (%g)", i, (double)pk);
        midi(h, 0xB0, 120, 0);
        render(h, 512, NULL, NULL);
    }
    free(L);
    free(R);
    api->destroy_instance(h);
}

static void test_osc(void) {
    printf("oscillators\n");
    enum { N = 44100 };
    float *x = buf(N);
    const char *names[] = {"sine", "tri", "saw", "pulse"};
    for (int w = 0; w < 4; w++) {
        for (int s = 0; s < 3; s++) {
            float shape = 0.5f * (float)s;
            osc_t o = {0};
            oscp_t pp = {w, 0, shape, 0.0f, 0.3f, 0.0f};
            osc_render(&o, x, 10000, 0.01f, 0.01f, &pp, &pp); /* 441 Hz, 100 cycles */
            double mean = 0;
            for (int i = 0; i < 10000; i++) mean += (double)x[i];
            mean /= 10000;
            CHECK(fabs(mean) < 0.02, "%s shape %.1f mean %g", names[w], (double)shape, mean);
            CHECK(peak(x, 10000) <= 1.12f, "%s shape %.1f peak %g", names[w], (double)shape, (double)peak(x, 10000));
            CHECK(rms(x, 10000) > 0.1f, "%s shape %.1f too quiet", names[w], (double)shape);
            CHECK(all_finite(x, 10000), "%s finite", names[w]);
            if (s == 0) {
                int c = up_crossings(x, 10000);
                CHECK(abs(c - 100) <= 1, "%s: %d crossings, want 100", names[w], c);
            }
        }
    }
    /* Alias floor of a plain saw (plan/04 section 10; thresholds carry about 3 dB margin). */
    double f0s[5] = {523.25, 1046.5, 2093.0, 4186.0, 8000.3};
#if CREST_BLEP_POINTS == 4
    double lim[5] = {-40, -38, -34, -33, -24};
#else
    double lim[5] = {-31, -28, -24, -22, -15};
#endif
    float *y = buf(FN);
    for (int i = 0; i < 5; i++) {
        osc_t o = {0};
        oscp_t pp = {WAVE_SAW, 0, 0.0f, 0.0f, 0.3f, 0.0f};
        float dt = (float)(f0s[i] / 44100.0);
        osc_render(&o, y, FN, dt, dt, &pp, &pp);
        double a = alias_db(y, f0s[i]);
        CHECK(a <= lim[i], "saw %.0f Hz alias %.1f dB, limit %.0f", f0s[i], a, lim[i]);
    }
    /* Above fs/4 the band-limited waves collapse to a sine. */
    osc_t o = {0};
    oscp_t hp = {WAVE_SAW, 0, 0.0f, 0.0f, 0.3f, 0.0f};
    osc_render(&o, x, 4096, 0.3f, 0.3f, &hp, &hp);
    CHECK(all_finite(x, 4096) && peak(x, 4096) <= 1.05f, "very high pitch is bounded");
    free(x);
    free(y);
}

static float tone_gain(int type, float g_k, float freq, float amp) {
    filt_t f;
    filt_reset(&f);
    uint32_t rng = 1;
    float g = tanf(CREST_PI * 1000.0f / CREST_FS); /* cutoff fixed at 1 kHz */
    enum { N = 8192 };
    static float x[N];
    for (int i = 0; i < N; i++) x[i] = amp * sinf(CREST_TAU * freq * (float)i / CREST_FS);
    filt_process(&f, type, x, N, g, g, g_k, 0.05f, &rng, NULL);
    return peak(x + N - 2048, 2048) / amp;
}

static void test_filter(void) {
    printf("filter\n");
    const float K = 1.41421356f;
    float lp = tone_gain(FLT_LP12, K, 1000.0f, 0.2f);
    CHECK(fabsf(lp - 0.707f) < 0.05f, "LP12 gain at fc %g", (double)lp);
    CHECK(fabsf(tone_gain(FLT_LP12, K, 100.0f, 0.2f) - 1.0f) < 0.03f, "LP12 passband");
    float s12 = 20.0f * log10f(tone_gain(FLT_LP12, K, 4000.0f, 0.2f));
    CHECK(s12 < -21.0f && s12 > -28.0f, "LP12 gain at 4fc %g dB", (double)s12);
    float s24 = 20.0f * log10f(tone_gain(FLT_LP24, K, 4000.0f, 0.2f) + 1e-9f);
    CHECK(s24 < -42.0f && s24 > -58.0f, "LP24 gain at 4fc %g dB", (double)s24);
    CHECK(fabsf(tone_gain(FLT_HP12, K, 1000.0f, 0.2f) - 0.707f) < 0.05f, "HP12 gain at fc");
    float h4 = 20.0f * log10f(tone_gain(FLT_HP12, K, 250.0f, 0.2f));
    CHECK(h4 < -21.0f && h4 > -28.0f, "HP12 gain at fc/4 %g dB", (double)h4);
    CHECK(fabsf(tone_gain(FLT_BP12, K, 1000.0f, 0.2f) - 1.0f) < 0.08f, "BP12 unity at centre");
    CHECK(tone_gain(FLT_BP12, K, 100.0f, 0.2f) < 0.3f, "BP12 rejects lows");

    /* Self-oscillation: no input, resonance at maximum. */
    filt_t f;
    filt_reset(&f);
    uint32_t rng = 7;
    float g = tanf(CREST_PI * 1000.0f / CREST_FS);
    float kmax = filt_res_to_k(1.0f);
    CHECK(kmax < 0.0f, "res=1 gives negative damping (%g)", (double)kmax);
    static float z[44100];
    memset(z, 0, sizeof z);
    filt_process(&f, FLT_LP12, z, 44100, g, g, kmax, 0.5f, &rng, NULL);
    filt_process(&f, FLT_LP12, z, 44100, g, g, kmax, 0.5f, &rng, NULL);
    float pk = peak(z + 44100 - 8820, 8820);
    CHECK(pk > 0.05f && pk < 4.0f, "self-oscillation amplitude %g", (double)pk);
    float hz = (float)up_crossings(z + 44100 - 22050, 22050) * 2.0f;
    CHECK(fabsf(hz - 1000.0f) < 80.0f, "self-oscillation frequency %g Hz", (double)hz);

    /* Fuzz: random type, cutoff and damping, hot input. */
    int bad = 0;
    static float fz[512];
    for (int t = 0; t < 400; t++) {
        filt_reset(&f);
        int type = (int)(frand() * 6.0f) % 6;
        float g0 = 0.001f + frand() * 6.0f, g1 = 0.001f + frand() * 6.0f;
        float k = -0.2f + frand() * 2.4f;
        for (int b = 0; b < 40; b++) {
            for (int i = 0; i < 512; i++) fz[i] = (frand() * 2.0f - 1.0f) * 2.0f;
            filt_process(&f, type, fz, 512, g0, g1, k, 0.5f + 3.0f * frand(), &rng, NULL);
            if (!all_finite(fz, 512) || peak(fz, 512) > 200.0f) bad++;
        }
    }
    CHECK(bad == 0, "filter fuzz produced %d bad blocks", bad);
}

static int ticks_until(env_t *e, const env_par_t *p, int stage_now, int max) {
    int n = 0;
    while (e->stage == stage_now && n < max) {
        env_tick(e, p);
        n++;
    }
    return n;
}

static void test_env(void) {
    printf("envelopes\n");
    env_par_t p = {.a = 0.5f, .d = 0.5f, .s = 0.5f, .r = 0.5f, .trig = ENV_MULTI};
    env_t e = {0};
    env_trigger(&e, &p, 1);
    float T = env_time(0.5f, ENV_ATTACK_MAX), TD = env_time(0.5f, ENV_DECAY_MAX), TR = env_release_time(0.5f);
    float ta = (float)ticks_until(&e, &p, ES_ATTACK, 100000) * CREST_CTL_DT;
    CHECK(fabsf(ta - T) < 0.1f * T, "attack %g s, want %g", (double)ta, (double)T);
    float td = (float)ticks_until(&e, &p, ES_DECAY, 100000) * CREST_CTL_DT;
    CHECK(fabsf(td - TD) < 0.12f * TD, "decay %g s, want %g", (double)td, (double)TD);
    CHECK(e.stage == ES_SUSTAIN && fabsf(e.y - 0.5f) < 1e-3f, "sustain level %g", (double)e.y);
    env_release(&e, &p);
    float tr = (float)ticks_until(&e, &p, ES_RELEASE, 100000) * CREST_CTL_DT;
    CHECK(fabsf(tr - TR) < 0.12f * TR, "release %g s, want %g", (double)tr, (double)TR);
    CHECK(e.stage == ES_IDLE && e.y == 0.0f, "ends idle at zero");

    /* Release time does not depend on the level it starts from. */
    env_t e2 = {0};
    p.s = 0.9f;
    env_trigger(&e2, &p, 1);
    ticks_until(&e2, &p, ES_ATTACK, 100000);
    ticks_until(&e2, &p, ES_DECAY, 100000);
    env_release(&e2, &p);
    float tr2 = (float)ticks_until(&e2, &p, ES_RELEASE, 100000) * CREST_CTL_DT;
    CHECK(fabsf(tr2 - TR) < 0.12f * TR, "release from 0.9: %g s", (double)tr2);

    /* Repeats: rep=3 gives four attack peaks; the envelope then sits at sustain. */
    env_par_t pr = {.a = 0.1f, .d = 0.1f, .s = 0.0f, .r = 0.3f, .rep = 3, .trig = ENV_MULTI};
    env_t e3 = {0};
    env_trigger(&e3, &pr, 1);
    int peaks = 0, prev = ES_IDLE;
    for (int i = 0; i < 100000; i++) {
        env_tick(&e3, &pr);
        if (e3.stage == ES_DECAY && prev == ES_ATTACK) peaks++;
        prev = e3.stage;
    }
    CHECK(peaks == 4, "rep=3 produced %d peaks", peaks);
    CHECK(e3.stage == ES_SUSTAIN, "ends in sustain");
    pr.rep = ENV_REP_LOOP;
    env_trigger(&e3, &pr, 1);
    peaks = 0;
    prev = ES_IDLE;
    for (int i = 0; i < 20000; i++) {
        env_tick(&e3, &pr);
        if (e3.stage == ES_DECAY && prev == ES_ATTACK) peaks++;
        prev = e3.stage;
    }
    CHECK(peaks > 10, "rep=Loop loops (%d peaks)", peaks);
    env_release(&e3, &pr);
    for (int i = 0; i < 20000 && e3.stage != ES_IDLE; i++) env_tick(&e3, &pr);
    CHECK(e3.stage == ES_IDLE, "loop envelope releases");

    /* Delay stage: silent for about the delay time, then the attack starts. */
    env_par_t pd = {.a = 0.2f, .d = 0.5f, .s = 0.5f, .r = 0.5f, .dly = 0.5f, .trig = ENV_MULTI};
    env_t e4 = {0};
    env_trigger(&e4, &pd, 1);
    float tdl = (float)ticks_until(&e4, &pd, ES_DELAY, 100000) * CREST_CTL_DT;
    CHECK(fabsf(tdl - 0.1f) < 0.01f && e4.y == 0.0f, "delay %g s", (double)tdl);

    /* Retrigger keeps the level (no click) unless Reset is asked for. */
    env_t e5 = {0};
    p.s = 0.6f;
    env_trigger(&e5, &p, 1);
    for (int i = 0; i < 100000 && e5.stage != ES_SUSTAIN; i++) env_tick(&e5, &p);
    env_trigger(&e5, &p, 0);
    CHECK(e5.y > 0.55f, "retrigger from current level (%g)", (double)e5.y);
    env_trigger(&e5, &p, 1);
    CHECK(e5.y == 0.0f, "hard reset");

    /* Hold: the peak lasts the hold time (127 = 500 ms) before the decay starts; repeats loop A-H-D. */
    env_par_t ph = {.a = 0.0f, .d = 0.3f, .s = 0.2f, .r = 0.3f, .hold = 0.5f, .trig = ENV_MULTI};
    env_t e6 = {0};
    env_trigger(&e6, &ph, 1);
    ticks_until(&e6, &ph, ES_ATTACK, 100000);
    CHECK(e6.stage == ES_HOLD && e6.y == 1.0f, "attack hands over to hold");
    float th = (float)ticks_until(&e6, &ph, ES_HOLD, 100000) * CREST_CTL_DT;
    CHECK(fabsf(th - 0.25f) < 0.01f && e6.stage == ES_DECAY, "hold %g s, want 0.25", (double)th);
    ph.rep = 2;
    env_trigger(&e6, &ph, 1);
    int holds = 0;
    prev = ES_IDLE;
    for (int i = 0; i < 100000; i++) {
        env_tick(&e6, &ph);
        if (e6.stage == ES_HOLD && prev != ES_HOLD) holds++;
        prev = e6.stage;
    }
    CHECK(holds == 3 && e6.stage == ES_SUSTAIN, "rep=2 holds the peak %d times", holds);
    ph.hold = 0.0f;
    env_trigger(&e6, &ph, 1);
    ticks_until(&e6, &ph, ES_ATTACK, 100000);
    CHECK(e6.stage == ES_DECAY, "no hold stage at hold 0");

    /* The full-scale times follow the Peak's manual: attack over 18 s, decay ~22 s, release over 24 s. */
    CHECK(env_time(1.0f, ENV_ATTACK_MAX) > 18.0f && fabsf(env_time(1.0f, ENV_DECAY_MAX) - 22.0f) < 0.01f &&
              env_time(1.0f, ENV_RELEASE_MAX) > 24.0f && env_time(0.0f, ENV_ATTACK_MAX) < 0.001f,
          "envelope time ranges");
}

static void test_pitch(void) {
    printf("pitch\n");
    void *h = mk();
    tone_setup(h);
    float *L = buf(44100);
    struct { int note; float hz; } cases[] = {{69, 440.0f}, {48, 130.813f}, {81, 880.0f}};
    for (int c = 0; c < 3; c++) {
        note_on(h, cases[c].note, 100);
        render(h, 44100, L, NULL);
        int cr = up_crossings(L + 8832, 44100 - 8832);
        float hz = (float)cr / ((44100 - 8832) / 44100.0f);
        CHECK(fabsf(hz - cases[c].hz) < 0.01f * cases[c].hz + 1.0f, "note %d: %g Hz, want %g", cases[c].note,
              (double)hz, (double)cases[c].hz);
        note_off(h, cases[c].note);
        render(h, 8192, NULL, NULL);
    }
    /* Pitch bend: full up with the default 2 semitone range. */
    note_on(h, 69, 100);
    midi(h, 0xE0, 127, 127);
    render(h, 44100, L, NULL);
    float hz = (float)up_crossings(L + 8832, 44100 - 8832) / ((44100 - 8832) / 44100.0f);
    CHECK(fabsf(hz - 493.88f) < 6.0f, "bend up: %g Hz", (double)hz);
    midi(h, 0xE0, 0, 64);
    note_off(h, 69);
    render(h, 8192, NULL, NULL);
    /* Octave and coarse/fine. */
    spf(h, "v_oct", 1);
    note_on(h, 69, 100);
    render(h, 44100, L, NULL);
    hz = (float)up_crossings(L + 8832, 44100 - 8832) / ((44100 - 8832) / 44100.0f);
    CHECK(fabsf(hz - 880.0f) < 9.0f, "octave up: %g Hz", (double)hz);
    free(L);
    api->destroy_instance(h);
}

static void test_voices(void) {
    printf("voices\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *L = buf(44100), *R = buf(44100);
    tone_setup(h);

    /* Polyphony and stealing: the oldest held key goes. */
    for (int n = 60; n < 68; n++) note_on(h, n, 100);
    render(h, 1024, NULL, NULL);
    CHECK(active_voices(I) == 8, "8 voices sounding, got %d", active_voices(I));
    note_on(h, 70, 100);
    render(h, 128 * 8, NULL, NULL);
    CHECK(active_voices(I) == 8, "still 8 after steal, got %d", active_voices(I));
    int has70 = 0, has60 = 0;
    for (int i = 0; i < CREST_VOICES; i++) {
        if (I->voice[i].active && I->voice[i].note == 70) has70 = 1;
        if (I->voice[i].active && I->voice[i].note == 60) has60 = 1;
    }
    CHECK(has70 && !has60, "new note stole the oldest (70:%d 60:%d)", has70, has60);
    /* A chord right after a hard all-off: every voice is fading, and each note must queue on a
     * voice of its own rather than all overwriting the oldest one's queued note. */
    midi(h, 0xB0, 120, 0);
    for (int n = 72; n < 76; n++) note_on(h, n, 100);
    render(h, 2048, NULL, NULL);
    int held_chord = 0;
    for (int i = 0; i < CREST_VOICES; i++)
        if (I->voice[i].active && I->voice[i].gate && I->voice[i].note >= 72 && I->voice[i].note < 76) held_chord++;
    CHECK(held_chord == 4, "chord during the all-off fade keeps 4 notes, got %d", held_chord);
    for (int n = 72; n < 76; n++) note_off(h, n);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);
    CHECK(active_voices(I) == 0, "all sound off");

    /* Same key twice: Poly retriggers, Poly2 stacks. */
    note_on(h, 60, 100);
    note_on(h, 60, 100);
    render(h, 256, NULL, NULL);
    CHECK(active_voices(I) == 1, "Poly reuses the voice (%d)", active_voices(I));
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);
    sp(h, "v_mode", "Poly2");
    render(h, 256, NULL, NULL);
    note_on(h, 60, 100);
    note_on(h, 60, 100);
    render(h, 256, NULL, NULL);
    CHECK(active_voices(I) == 2, "Poly2 stacks voices (%d)", active_voices(I));
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);
    sp(h, "v_mode", "Poly");
    render(h, 256, NULL, NULL);

    /* Release reaches silence; sustain pedal holds it. */
    note_on(h, 60, 100);
    render(h, 4096, L, NULL);
    float held = rms(L + 2048, 2048);
    CHECK(held > 0.02f, "note sounds (%g)", (double)held);
    note_off(h, 60);
    render(h, 22050, L, NULL);
    CHECK(rms(L + 22050 - 2048, 2048) < 1e-3f, "silent after release");
    CHECK(active_voices(I) == 0, "voice freed after release");

    midi(h, 0xB0, 64, 127);
    note_on(h, 60, 100);
    render(h, 2048, NULL, NULL);
    note_off(h, 60);
    render(h, 22050, L, NULL);
    CHECK(rms(L + 22050 - 2048, 2048) > 0.02f, "pedal holds the note");
    midi(h, 0xB0, 64, 0);
    render(h, 22050, L, NULL);
    CHECK(rms(L + 22050 - 2048, 2048) < 1e-3f, "pedal up releases it");

    /* Velocity scales level when the amp envelope asks for it. */
    spf(h, "ae_vel", 1.0);
    note_on(h, 60, 127);
    render(h, 4096, L, NULL);
    float loud = rms(L + 2048, 2048);
    note_off(h, 60);
    render(h, 22050, NULL, NULL);
    note_on(h, 60, 32);
    render(h, 4096, L, NULL);
    float soft = rms(L + 2048, 2048);
    note_off(h, 60);
    render(h, 22050, NULL, NULL);
    CHECK(loud > 2.5f * soft, "velocity: %g vs %g", (double)loud, (double)soft);
    spf(h, "ae_vel", 0.0);

    /* Mono: last note priority with fall-back to the held key. */
    sp(h, "v_mode", "Mono");
    render(h, 256, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 512, NULL, NULL);
    note_on(h, 64, 100);
    render(h, 512, NULL, NULL);
    CHECK(I->voice[0].note == 64 && active_voices(I) == 1, "mono plays the last note");
    note_off(h, 64);
    render(h, 512, NULL, NULL);
    CHECK(I->voice[0].note == 60 && I->voice[0].gate, "mono returns to the held note (%d)", I->voice[0].note);
    note_off(h, 60);
    render(h, 22050, NULL, NULL);
    CHECK(active_voices(I) == 0, "mono releases");

    /* Mono legato keeps the envelope running; plain Mono retriggers it. */
    spf(h, "ae_a", 0.6);
    spf(h, "ae_s", 0.8);
    sp(h, "v_mode", "MonoLG");
    render(h, 256, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 128 * 40, NULL, NULL);
    float y_before = I->voice[0].env[0].y;
    note_on(h, 64, 100);
    render(h, 128, NULL, NULL);
    CHECK(I->voice[0].env[0].y >= y_before * 0.95f, "legato does not retrigger (%g -> %g)", (double)y_before,
          (double)I->voice[0].env[0].y);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);

    /* Glide. */
    sp(h, "v_mode", "Mono");
    spf(h, "ae_a", 0.0);
    spf(h, "v_glide", 0.5);
    render(h, 256, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 2048, NULL, NULL);
    note_on(h, 72, 100);
    render(h, 128 * 40, NULL, NULL);
    float p = I->voice[0].pitch;
    CHECK(p > 60.5f && p < 71.5f, "glide in progress (%g)", (double)p);
    render(h, 44100 * 6, NULL, NULL);
    CHECK(fabsf(I->voice[0].pitch - 72.0f) < 0.05f, "glide arrives (%g)", (double)I->voice[0].pitch);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);

    /* Switching mode with notes held must not crash or leave stuck voices. */
    sp(h, "v_mode", "Poly");
    spf(h, "v_glide", 0.0);
    for (int n = 60; n < 66; n++) note_on(h, n, 100);
    render(h, 1024, NULL, NULL);
    sp(h, "v_mode", "Mono");
    render(h, 4096, L, R);
    CHECK(all_finite(L, 4096), "mode switch is clean");
    midi(h, 0xB0, 123, 0);
    render(h, 44100, NULL, NULL);
    CHECK(active_voices(I) == 0, "all notes off releases everything");

    free(L);
    free(R);
    api->destroy_instance(h);
}

static float diff_rms(const float *a, const float *b, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)(a[i] - b[i]) * (double)(a[i] - b[i]);
    return (float)sqrt(s / n);
}

static void test_fx(void) {
    printf("effects\n");
    enum { N = 44100 };
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *a = buf(N), *b = buf(N), *r = buf(N);
    const char *lv[] = {"dst_lvl", "cho_lvl", "dly_lvl", "rev_lvl"};
    for (int f = 0; f < 4; f++) {
        for (int pass = 0; pass < 2; pass++) {
            sp(h, "state", "{\"fmt\":\"crest\"}");
            tone_setup(h);
            sp(h, "o1_wave", "Saw");
            if (pass) {
                spf(h, lv[f], f == 0 ? 0.6 : 0.8);
                if (f == 1) spf(h, "cho_depth", 0.9);
            }
            render(h, 128 * 120, NULL, NULL); /* let an enabled effect finish clearing */
            note_on(h, 57, 100);
            render(h, N, pass ? b : a, NULL);
            note_off(h, 57);
            render(h, 22050, NULL, NULL);
        }
        float d = diff_rms(a, b, N);
        CHECK(d > 0.003f, "%s changes the sound (%g)", lv[f], (double)d);
        CHECK(all_finite(b, N), "%s finite", lv[f]);
    }

    /* Reverb leaves a tail after the note ends; with the level at 0 there is none. */
    for (int pass = 0; pass < 2; pass++) {
        sp(h, "state", "{\"fmt\":\"crest\"}");
        tone_setup(h);
        spf(h, "ae_r", 0.1);
        if (pass) {
            spf(h, "rev_lvl", 1.0);
            spf(h, "rev_decay", 0.6);
        }
        render(h, 128 * 120, NULL, NULL);
        note_on(h, 60, 100);
        render(h, 4096, NULL, NULL);
        note_off(h, 60);
        render(h, 22050, r, NULL);
        float tail = rms(r + 11025, 8192);
        if (pass) CHECK(tail > 1e-3f, "reverb tail %g", (double)tail);
        else CHECK(tail < 1e-5f, "dry has no tail (%g)", (double)tail);
    }

    /* Delay: an echo arrives at the set time and, without feedback, only one. */
    sp(h, "state", "{\"fmt\":\"crest\"}");
    tone_setup(h);
    spf(h, "ae_r", 0.0);
    spf(h, "dly_lvl", 1.0);
    spf(h, "dly_fb", 0.0);
    sp(h, "dly_sync", "Off");
    spf(h, "dly_time", 0.5); /* 10 ms * 200^0.5 = 141.4 ms = 6235 samples */
    spf(h, "dly_lp", 1.0);
    spf(h, "dly_hp", 0.0);
    render(h, 128 * 120, NULL, NULL);
    note_on(h, 69, 100);
    render(h, 1280, a, NULL);
    note_off(h, 69);
    render(h, N - 1280, a + 1280, NULL);
    float dry = rms(a + 300, 600);
    float echo = rms(a + 6235 + 300, 600);
    float second = rms(a + 2 * 6235 + 300, 600);
    float between = rms(a + 3200, 600);
    CHECK(echo > 0.5f * dry, "echo level %g vs dry %g", (double)echo, (double)dry);
    CHECK(second < 0.1f * echo, "no second echo without feedback (%g)", (double)second);
    CHECK(between < 0.1f * echo, "silence between taps (%g)", (double)between);

    /* Feedback at the Peak's middle setting (64): each repeat 0.98 * 0.504^1.5 = 0.35 of the last. */
    spf(h, "dly_fb", 0.504);
    render(h, 44100, NULL, NULL);
    note_on(h, 69, 100);
    render(h, 1280, a, NULL);
    note_off(h, 69);
    render(h, N - 1280, a + 1280, NULL);
    float e1 = rms(a + 6235 + 300, 600), e2 = rms(a + 2 * 6235 + 300, 600), e3 = rms(a + 3 * 6235 + 300, 600);
    CHECK(fabsf(e2 / e1 - 0.35f) < 0.04f && fabsf(e3 / e2 - 0.35f) < 0.04f, "feedback 64/127 repeats at %g, %g of the last",
          (double)(e2 / e1), (double)(e3 / e2));
    g_bpm = 100.0f;
    sp(h, "dly_sync", "1/4");
    CHECK((int)gp(h, "dly_sync") == 10, "sync enum by name");

    /* Everything on and wide open for ten seconds: finite, bounded, no NaN resets. */
    sp(h, "state", "{\"fmt\":\"crest\"}");
    tone_setup(h);
    sp(h, "o1_wave", "Saw");
    spf(h, "mx_o2", 1.0);
    spf(h, "mx_o3", 1.0);
    spf(h, "dst_lvl", 1.0);
    spf(h, "cho_lvl", 1.0);
    spf(h, "cho_fb", 1.0);
    spf(h, "dly_lvl", 1.0);
    spf(h, "dly_fb", 1.0);
    spf(h, "rev_lvl", 1.0);
    spf(h, "rev_decay", 1.0);
    spf(h, "rev_size", 1.0);
    render(h, 128 * 120, NULL, NULL);
    for (int n = 48; n < 72; n += 4) note_on(h, n, 127);
    float worst = 0;
    int ok = 1;
    for (int s = 0; s < 10; s++) {
        render(h, N, a, b);
        ok &= all_finite(a, N) && all_finite(b, N);
        worst = fmaxf(worst, fmaxf(peak(a, N), peak(b, N)));
    }
    CHECK(ok, "max-effects run stays finite");
    CHECK(worst <= 1.0f, "max-effects peak %g", (double)worst);
    CHECK(I->nan_resets == 0, "NaN guard fired %d times", I->nan_resets);
    free(a);
    free(b);
    free(r);
    api->destroy_instance(h);
}

static void test_fuzz(void) {
    printf("fuzz\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *L = buf(22050);
    int bad = 0;
    for (int t = 0; t < 40; t++) {
        randomise(h);
        for (int n = 0; n < 6; n++) note_on(h, 24 + (int)(frand() * 72), 1 + (int)(frand() * 126));
        if (frand() < 0.5f) midi(h, 0xB0, 64, 127);
        for (int s = 0; s < 4; s++) {
            if (frand() < 0.5f) midi(h, 0xE0, (int)(frand() * 127), (int)(frand() * 127));
            render(h, 5504, L, NULL);
            if (!all_finite(L, 5504)) bad++;
        }
        midi(h, 0xB0, 120, 0);
        render(h, 1024, NULL, NULL);
    }
    CHECK(bad == 0, "fuzz: %d non-finite blocks", bad);
    CHECK(I->nan_resets == 0, "fuzz: NaN guard fired %d times", I->nan_resets);
    free(L);
    api->destroy_instance(h);
}

static int g_bpm_calls;
static float counting_bpm(void) {
    g_bpm_calls++;
    return 120.0f;
}

/* Fixes from the October 2026 code review. */
static void test_review_fixes(void) {
    printf("review fixes\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *a = buf(22016);
    tone_setup(h);

    /* Lowering Unison while a mono note is held leaves nothing sounding after the release. */
    sp(h, "v_mode", "Mono");
    sp(h, "v_unison", "4");
    render(h, 1280, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 4480, NULL, NULL);
    CHECK(active_voices(I) == 4, "mono unison 4 sounds 4 voices (%d)", active_voices(I));
    sp(h, "v_unison", "2");
    render(h, 1280, NULL, NULL);
    note_off(h, 60);
    render(h, 44160, NULL, NULL);
    int left = 0;
    for (int i = 0; i < CREST_VOICES; i++) left += I->voice[i].active;
    CHECK(left == 0, "no voice left sounding after a unison change (%d)", left);
    note_on(h, 62, 100);
    render(h, 1280, NULL, NULL);
    CHECK(active_voices(I) == 2, "the next mono note uses the new unison (%d)", active_voices(I));
    note_off(h, 62);

    /* "nan" is not a value: the parameter keeps what it had. */
    spf(h, "flt_cut", 0.5);
    sp(h, "flt_cut", "nan");
    CHECK(fabsf(gp(h, "flt_cut") - 0.5f) < 1e-4f, "flt_cut rejects nan (%g)", (double)gp(h, "flt_cut"));
    sp(h, "v_unison", "nan");
    CHECK((int)gp(h, "v_unison") == 2, "v_unison rejects nan (%g)", (double)gp(h, "v_unison"));

    /* BP12 self-oscillates about 8 dB (RMS) below LP12; it used to be about 24 dB down. */
    float pk[2];
    for (int t = 0; t < 2; t++) {
        filt_t f;
        filt_reset(&f);
        uint32_t rng = 7;
        float g = tanf(CREST_PI * 1000.0f / CREST_FS);
        static float z[44100];
        memset(z, 0, sizeof z);
        for (int pass = 0; pass < 2; pass++)
            filt_process(&f, t ? FLT_BP12 : FLT_LP12, z, 44100, g, g, filt_res_to_k(1.0f), 0.5f, &rng, NULL);
        pk[t] = rms(z + 44100 - 8820, 8820);
    }
    CHECK(pk[1] > 0.35f * pk[0], "BP12 self-oscillation RMS %g vs LP12 %g", (double)pk[1], (double)pk[0]);

    /* A delay level dipping to 0 for half a second keeps its echoes; two seconds at 0 switch it off. */
    sp(h, "state", "{\"fmt\":\"crest\"}");
    tone_setup(h);
    spf(h, "ae_r", 0.1);
    spf(h, "dly_lvl", 0.8);
    spf(h, "dly_fb", 0.8);
    render(h, 128 * 120, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 4096, NULL, NULL);
    note_off(h, 60);
    spf(h, "dly_lvl", 0.0);
    render(h, 22016, NULL, NULL);
    CHECK(I->fx.z[1].active, "delay stays on through a short dip"); /* z[1]: the delay (fx.c FZ_DLY) */
    spf(h, "dly_lvl", 0.8);
    render(h, 22016, a, NULL);
    CHECK(rms(a, 22016) > 1e-3f, "echoes survive the dip (%g)", (double)rms(a, 22016));
    spf(h, "dly_lvl", 0.0);
    render(h, 44100 * 5 / 2 / 128 * 128, NULL, NULL);
    CHECK(!I->fx.z[1].active, "delay switches off after 2 s at level 0");

    /* The loudness solve renders offline: it never asks the host for its tempo or transport. */
    host.get_bpm = counting_bpm;
    g_bpm_calls = 0;
    double lpk, lrms;
    CHECK(crest_level_measure(api, I->v, 0.3, &lpk, &lrms), "level measure found an instance");
    CHECK(g_bpm_calls == 0, "level measure asked the host for bpm %d times", g_bpm_calls);
    host.get_bpm = fake_bpm;

    free(a);
    api->destroy_instance(h);
}

#include "test_m2m3.inc"
#include "test_peakfx.inc"
#include "peak_bank.h"
#include "test_peakmod.inc"

int main(void) {
    memset(&host, 0, sizeof host);
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    host.get_bpm = fake_bpm;
    api = move_plugin_init_v2(&host);
    test_abi();
    test_params();
    test_state();
    test_presets();
    test_osc();
    test_filter();
    test_env();
    test_pitch();
    test_voices();
    test_fx();
    test_wavetables();
    test_density_sync();
    test_drives();
    test_drift_unison();
    test_lfo();
    test_modmatrix();
    test_peak_features();
    test_arp();
    test_arp_scale();
    test_vmode_change();
    test_lfo_peak();
    test_lfo_voice();
    test_env_peak();
    test_lfo2_depth();
    test_flt_lfo_depth();
    test_mixer_headroom();
    test_tri_peak();
    test_fx_peak();
    test_peak_bank();
    test_review_fixes();
    test_fuzz();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
