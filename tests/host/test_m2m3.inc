/* Tests for the M2 (character) and M3 (modulation) features. Included by test_main.c. */

#include "lfo.h"
#include "wavetables.h"

static void osc_block(osc_t *o, float *x, int n, int wave, int table, float dt, float shape, float dens, float vsync) {
    oscp_t p = {wave, table, shape, dens, dens > 0.0f ? 1.0f : 0.3f, vsync}; /* density tests need real detune */
    osc_render(o, x, n, dt, dt, &p, &p);
}

static float hz_now(void *h, int frames) {
    float *x = buf(frames);
    render(h, frames, x, NULL);
    float hz = (float)up_crossings(x, frames) / ((float)frames / 44100.0f);
    free(x);
    return hz;
}

static void slot(void *h, int n, const char *s1, const char *s2, const char *dst, double dep) {
    char k[16];
    snprintf(k, sizeof k, "mm%02d_s1", n);
    sp(h, k, s1);
    snprintf(k, sizeof k, "mm%02d_s2", n);
    sp(h, k, s2);
    snprintf(k, sizeof k, "mm%02d_dst", n);
    sp(h, k, dst);
    snprintf(k, sizeof k, "mm%02d_dep", n);
    spf(h, k, dep);
}

static void fresh(void *h) { /* INIT patch + a plain open sine voice */
    sp(h, "state", "{\"fmt\":\"crest\"}");
    tone_setup(h);
}

/* ================================================================== M2 */

static void test_wavetables(void) {
    printf("wavetables\n");
    int bad = 0;
    for (int t = 0; t < WT_TABLES; t++)
        for (int f = 0; f < WT_FRAMES; f++)
            for (int k = 0; k < WT_LEVELS; k++) {
                const int16_t *d = WT_DATA + (t * WT_FRAMES + f) * WT_FRAME_STRIDE + WT_LEVEL_OFF[k];
                int L = WT_LEN[k];
                double sum = 0, sq = 0;
                int pk = 0;
                for (int i = 0; i < L; i++) {
                    sum += d[i];
                    sq += (double)d[i] * d[i];
                    if (abs(d[i]) > pk) pk = abs(d[i]);
                }
                double rmsv = sqrt(sq / L), mean = sum / L;
                if ((rmsv < 150.0 && k < 6) || pk >= 32767 || fabs(mean) > 0.05 * pk || (L & (L - 1))) bad++;
            }
    CHECK(bad == 0, "%d table/frame/level combinations are silent, clipped, off-centre or not a power of two", bad);
    CHECK(WT_TABLES == (int)(sizeof(CREST_PARAMS[P_O1_TABLE].opts) ? CREST_PARAMS[P_O1_TABLE].nopts : 0),
          "table names match the data");

    enum { N = 10000 };
    float *x = buf(N);
    for (int t = 0; t < WT_TABLES; t++)
        for (int s = 0; s < 3; s++) {
            osc_t o = {0};
            osc_block(&o, x, N, WAVE_WT, t, 0.01f, 0.5f * (float)s, 0.0f, 0.0f);
            CHECK(all_finite(x, N) && peak(x, N) <= 1.0f && rms(x, N) > 0.04f, "table %d shape %d: peak %g rms %g", t,
                  s, (double)peak(x, N), (double)rms(x, N));
            float maxd = 0;
            for (int i = 1000; i < N - 100; i++) maxd = fmaxf(maxd, fabsf(x[i] - x[i + 100]));
            CHECK(maxd < 0.01f, "table %d shape %d is not periodic (%g)", t, s, (double)maxd);
        }
    /* A table that is a plain sawtooth stays clean at high pitch (mip-mapping works). */
    float *y = buf(FN);
    double f0s[3] = {523.25, 2093.0, 4186.0};
    double lim[3] = {-33, -30, -28};
    for (int i = 0; i < 3; i++) {
        osc_t o = {0};
        osc_block(&o, y, FN, WAVE_WT, 10 /* Additive */, (float)(f0s[i] / 44100.0), 0.0f, 0.0f, 0.0f);
        double a = alias_db(y, f0s[i]);
        CHECK(a <= lim[i], "wavetable %.0f Hz alias %.1f dB, limit %.0f", f0s[i], a, lim[i]);
    }
    free(x);
    free(y);
}

static void test_density_sync(void) {
    printf("density and sync\n");
    enum { N = 10000 };
    float *a = buf(N), *b = buf(N);
    for (int w = 1; w <= 3; w++) { /* Tri, Saw, Pulse */
        osc_t o1 = {0}, o2 = {0};
        for (int u = 0; u < 4; u++) o2.ph[u] = 0x12345678u * (unsigned)(u + 1);
        osc_block(&o1, a, N, w, 0, 0.01f, 0.0f, 0.0f, 0.0f);
        osc_block(&o2, b, N, w, 0, 0.01f, 0.0f, 1.0f, 0.0f);
        float ra = rms(a, N), rb = rms(b, N);
        CHECK(rb > 0.6f * ra && rb < 1.5f * ra, "wave %d: density keeps the level (%g vs %g)", w, (double)rb, (double)ra);
        CHECK(diff_rms(a, b, N) > 0.05f && all_finite(b, N) && peak(b, N) < 2.2f, "wave %d: density changes the sound (%g, %g)", w, (double)diff_rms(a, b, N), (double)peak(b, N));
    }
    /* Virtual sync: the pattern still repeats at the master frequency, with ratio-many slave cycles. */
    osc_t o = {0};
    osc_block(&o, a, N, WAVE_SAW, 0, 0.01f, 0.0f, 0.0f, 0.5f); /* ratio 2^(0.5*4) = 4 */
    int off = 0; /* edge samples may land on either side of a sample boundary; everything else must repeat */
    for (int i = 1000; i < N - 100; i++) off += fabsf(a[i] - a[i + 100]) > 0.05f;
    int cr = up_crossings(a, N);
    CHECK(off < N / 25, "synced saw repeats every master cycle (%d of %d samples differ)", off, N);
    CHECK(cr >= 390 && cr <= 410, "synced saw has ~4 slave cycles per master cycle (%d crossings)", cr);
    CHECK(peak(a, N) < 1.3f && all_finite(a, N), "synced saw is bounded");
    float *y = buf(FN);
    osc_t o3 = {0};
    osc_block(&o3, y, FN, WAVE_SAW, 0, (float)(1046.5 / 44100.0), 0.0f, 0.0f, 0.25f);
    double al = alias_db(y, 1046.5);
    CHECK(al <= -20.0, "synced saw alias %.1f dB", al);
    /* Changing wave type mid-stream crossfades instead of clicking. */
    osc_t o4 = {0};
    oscp_t pa = {WAVE_SAW, 0, 0, 0, 0.3f, 0}, pb = {WAVE_PULSE, 0, 0, 0, 0.3f, 0};
    float cf[16], lo[16];
    osc_render(&o4, cf, 16, 0.01f, 0.01f, &pa, &pa);
    osc_render(&o4, cf, 16, 0.01f, 0.01f, &pa, &pb);
    osc_t o5 = {0};
    osc_render(&o5, lo, 16, 0.01f, 0.01f, &pa, &pa);
    osc_render(&o5, lo, 16, 0.01f, 0.01f, &pa, &pa);
    float jump = 0;
    for (int i = 1; i < 16; i++) jump = fmaxf(jump, fabsf(cf[i] - cf[i - 1]));
    CHECK(jump < 0.9f, "wave change is crossfaded (largest step %g)", (double)jump);
    free(a);
    free(b);
    free(y);
}

static void test_drives(void) {
    printf("drives\n");
    void *h = mk();
    float *x = buf(FN), *y = buf(FN);
    for (int pass = 0; pass < 2; pass++) {
        fresh(h);
        sp(h, "o1_wave", "Sine");
        spf(h, "ae_r", 0.2);
        spf(h, "flt_cut", 1.0);
        if (pass == 0) spf(h, "pre_od", 1.0);
        else spf(h, "post_dist", 1.0);
        note_on(h, 80, 100); /* ~830 Hz: odd harmonics reach well past Nyquist */
        render(h, 4096, NULL, NULL);
        render(h, FN, pass ? y : x, NULL);
        float *z = pass ? y : x;
        CHECK(all_finite(z, FN) && peak(z, FN) < 1.0f && rms(z, FN) > 0.03f, "%s: bounded and audible", pass ? "post" : "pre");
        double f0 = 440.0 * pow(2.0, (80 - 69) / 12.0);
        double a = alias_db(z, f0);
        CHECK(a <= -20.0, "%s: alias %.1f dB at %.0f Hz", pass ? "post" : "pre", a, f0);
        midi(h, 0xB0, 120, 0);
        render(h, 2048, NULL, NULL);
    }
    /* A drive adds harmonics: more energy above the fundamental than the clean sine. */
    fresh(h);
    sp(h, "o1_wave", "Sine");
    note_on(h, 60, 100);
    render(h, 4096, NULL, NULL);
    render(h, FN, x, NULL);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);
    spf(h, "pre_od", 0.8);
    note_on(h, 60, 100);
    render(h, 4096, NULL, NULL);
    render(h, FN, y, NULL);
    CHECK(diff_rms(x, y, FN) > 0.02f, "pre-overdrive changes the sound");
    free(x);
    free(y);
    api->destroy_instance(h);
}

static void test_drift_unison(void) {
    printf("unison and drift\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *L = buf(44100), *R = buf(44100);

    /* Drift moves the pitch slowly; with drift 0 it is steady. */
    for (int pass = 0; pass < 2; pass++) {
        fresh(h);
        spf(h, "v_drift", pass ? 1.0 : 0.0);
        note_on(h, 69, 100);
        render(h, 8192, NULL, NULL);
        float lo = 1e9f, hi = -1e9f;
        for (int w = 0; w < 6; w++) {
            float hz = hz_now(h, 22050 / 2);
            lo = fminf(lo, hz);
            hi = fmaxf(hi, hz);
        }
        if (pass) CHECK(hi - lo > 0.15f, "drift moves the pitch (%g Hz range)", (double)(hi - lo));
        else CHECK(hi - lo < 0.5f, "no drift is steady (%g Hz range)", (double)(hi - lo));
        midi(h, 0xB0, 120, 0);
        render(h, 4096, NULL, NULL);
    }

    /* Unison: groups of voices, whole-group stealing, stereo spread. */
    fresh(h);
    spf(h, "v_drift", 0.0);
    spf(h, "v_unison", 4);
    note_on(h, 60, 100);
    note_on(h, 64, 100);
    render(h, 2048, NULL, NULL);
    CHECK(active_voices(I) == 8, "two unison-4 notes fill 8 voices (%d)", active_voices(I));
    note_on(h, 67, 100);
    render(h, 128 * 12, NULL, NULL);
    int n60 = 0, n64 = 0, n67 = 0;
    for (int i = 0; i < CREST_VOICES; i++) {
        if (!I->voice[i].active || I->voice[i].fading) continue;
        n60 += I->voice[i].note == 60;
        n64 += I->voice[i].note == 64;
        n67 += I->voice[i].note == 67;
    }
    CHECK(n67 == 4 && n64 == 4 && n60 == 0, "third note takes the oldest whole group (%d %d %d)", n60, n64, n67);
    float pmin = 9, pmax = -9;
    for (int i = 0; i < CREST_VOICES; i++)
        if (I->voice[i].active && I->voice[i].note == 67) {
            pmin = fminf(pmin, I->voice[i].upos);
            pmax = fmaxf(pmax, I->voice[i].upos);
        }
    CHECK(pmin == -1.0f && pmax == 1.0f, "unison spreads from -1 to +1 (%g..%g)", (double)pmin, (double)pmax);
    midi(h, 0xB0, 120, 0);
    render(h, 4096, NULL, NULL);

    for (int pass = 0; pass < 2; pass++) { /* spread 0 is mono, spread 1 is wide */
        fresh(h);
        spf(h, "v_unison", 4);
        spf(h, "v_spread", pass ? 1.0 : 0.0);
        spf(h, "v_udet", 0.6);
        sp(h, "o1_wave", "Saw");
        note_on(h, 57, 100);
        render(h, 8192, NULL, NULL);
        render(h, 8192, L, R);
        float d = diff_rms(L, R, 8192);
        if (pass) CHECK(d > 0.02f, "spread decorrelates the channels (%g)", (double)d);
        else CHECK(d < 1e-4f, "spread 0 is mono (%g)", (double)d);
        midi(h, 0xB0, 120, 0);
        render(h, 4096, NULL, NULL);
    }

    /* Mono2: notes alternate between two groups, the old one rings out. */
    fresh(h);
    sp(h, "v_mode", "Mono2");
    spf(h, "v_unison", 2);
    spf(h, "ae_r", 0.4);
    render(h, 256, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 1024, NULL, NULL);
    CHECK(active_voices(I) == 2, "Mono2 starts one group (%d)", active_voices(I));
    note_on(h, 64, 100);
    render(h, 1024, NULL, NULL);
    CHECK(active_voices(I) == 4, "Mono2 overlaps the previous group (%d)", active_voices(I));
    int gate = 0;
    for (int i = 0; i < CREST_VOICES; i++) gate += I->voice[i].active && I->voice[i].gate;
    CHECK(gate == 2, "only the newest group is held (%d)", gate);
    note_off(h, 64);
    note_off(h, 60);
    render(h, 44100, NULL, NULL);
    CHECK(active_voices(I) == 0, "Mono2 releases fully");
    free(L);
    free(R);
    api->destroy_instance(h);
}

/* ================================================================== M3 */

static void test_lfo(void) {
    printf("lfo\n");
    lfo_par_t p = {.wave = LW_SINE, .trig = LT_FREE, .rate = 0.5995f}; /* 5 Hz (Low range) */
    lfo_t l = {0};
    lfo_start(&l, &p, 0);
    int ticks = (int)(4.0f / CREST_CTL_DT), up = 0;
    float prev = 0, pk = 0;
    for (int i = 0; i < ticks; i++) {
        float v = lfo_tick(&l, &p, 0.0f, -1.0, 120.0f, NULL);
        if (prev < 0 && v >= 0) up++;
        prev = v;
        pk = fmaxf(pk, fabsf(v));
    }
    CHECK(abs(up - 20) <= 1 && pk > 0.95f && pk <= 1.0f, "5 Hz sine over 4 s: %d cycles, peak %g", up, (double)pk);

    p.sync = 26; /* 1/4 note */
    lfo_start(&l, &p, 0);
    up = 0;
    prev = 0;
    ticks = (int)(5.0f / CREST_CTL_DT);
    for (int i = 0; i < ticks; i++) {
        float v = lfo_tick(&l, &p, 0.0f, -1.0, 120.0f, NULL);
        if (prev < 0 && v >= 0) up++;
        prev = v;
    }
    CHECK(abs(up - 10) <= 1, "1/4 note at 120 BPM over 5 s: %d cycles", up);

    /* Locked to the transport, the phase follows the beat position exactly. */
    double beat = 7.3;
    float v = lfo_tick(&l, &p, 0.0f, beat, 120.0f, NULL);
    CHECK(fabsf(v - sinf(CREST_TAU * (float)(beat - floor(beat)))) < 1e-3f, "beat-locked phase");

    lfo_par_t d = {.wave = LW_SINE, .rate = 0.6f, .phase = 0.25f, .dly = 0.5f}; /* delay 0.2236 s */
    lfo_start(&l, &d, 0);
    float early = 0, late = 0;
    for (int i = 0; i < (int)(0.4f / CREST_CTL_DT); i++) {
        float x = lfo_tick(&l, &d, 0.0f, -1.0, 120.0f, NULL);
        if (i * CREST_CTL_DT < 0.2f) early = fmaxf(early, fabsf(x));
        if (i * CREST_CTL_DT > 0.3f) late = fmaxf(late, fabsf(x));
    }
    CHECK(early == 0.0f && late > 0.1f, "delay holds the LFO at 0 (%g then %g)", (double)early, (double)late);

    lfo_par_t o = {.wave = LW_SAWUP, .trig = LT_KEY, .rep = 1, .rate = 0.6f}; /* one-shot */
    lfo_start(&l, &o, 0);
    float last = 0;
    for (int i = 0; i < (int)(3.0f / CREST_CTL_DT); i++) last = lfo_tick(&l, &o, 0.0f, -1.0, 120.0f, NULL);
    CHECK(last > 0.98f && l.done, "one-shot holds its final value (%g)", (double)last);

    lfo_par_t sh = {.wave = LW_SH, .rate = 0.55f};
    lfo_par_t rn = {.wave = LW_RND, .rate = 0.55f};
    lfo_start(&l, &sh, 0);
    int changes = 0;
    float pv = 2, mx = 0;
    for (int i = 0; i < (int)(2.0f / CREST_CTL_DT); i++) {
        float x = lfo_tick(&l, &sh, 0.0f, -1.0, 120.0f, NULL);
        if (x != pv) changes++;
        pv = x;
        mx = fmaxf(mx, fabsf(x));
    }
    CHECK(changes > 1 && changes < 400 && mx <= 1.0f, "sample&hold steps (%d changes)", changes);
    lfo_start(&l, &rn, 0);
    mx = 0;
    for (int i = 0; i < (int)(2.0f / CREST_CTL_DT); i++) mx = fmaxf(mx, fabsf(lfo_tick(&l, &rn, 0.0f, -1.0, 120.0f, NULL)));
    CHECK(mx <= 1.0f && mx > 0.05f, "smooth random is bounded (%g)", (double)mx);

    lfo_par_t fd = {.wave = LW_SQUARE, .rate = 0.6f, .fade = 0.2f}; /* fade in over 2 s */
    lfo_start(&l, &fd, 0);
    float first = 0, final = 0;
    for (int i = 0; i < (int)(3.0f / CREST_CTL_DT); i++) {
        float x = fabsf(lfo_tick(&l, &fd, 0.0f, -1.0, 120.0f, NULL));
        if (i * CREST_CTL_DT < 0.2f) first = fmaxf(first, x);
        if (i * CREST_CTL_DT > 2.5f) final = fmaxf(final, x);
    }
    CHECK(first < 0.2f && final > 0.95f, "fade in (%g -> %g)", (double)first, (double)final);
}

static void test_modmatrix(void) {
    printf("modulation matrix\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *x = buf(8192);

    /* Names and indices agree between params.py and the C enums. */
    struct { int i; const char *n; } src[] = {{MS_DIRECT, "Direct"}, {MS_WHEEL, "Mod Wheel"}, {MS_POLYAT, "Poly AT"},
                                             {MS_VEL, "Velocity"}, {MS_LFO1, "LFO 1"}, {MS_LFO4, "LFO 4"},
                                             {MS_ANIM1, "Animate 1"}, {MS_MAC1, "Macro 1"}, {MS_MAC8, "Macro 8"}};
    for (size_t k = 0; k < sizeof src / sizeof *src; k++)
        CHECK(strcmp(CREST_PARAMS[P_MM01_S1].opts[src[k].i], src[k].n) == 0, "source %d is %s", src[k].i, src[k].n);
    struct { int i; const char *n; } dst[] = {{MD_PITCH, "Pitch (all)"}, {MD_O3_PITCH, "Osc 3 Pitch"}, {MD_CUTOFF, "Cutoff"},
                                             {MD_AMP, "Amp Level"}, {MD_AE_A, "Amp Attack"}, {MD_M2_R, "Mod2 Release"},
                                             {MD_LFO4_RATE, "LFO 4 Rate"}, {MD_FX_REV, "Reverb Level"}, {MD_DLY_FB, "Delay Feedback"}};
    for (size_t k = 0; k < sizeof dst / sizeof *dst; k++)
        CHECK(strcmp(CREST_PARAMS[P_MM01_DST].opts[dst[k].i], dst[k].n) == 0, "destination %d is %s", dst[k].i, dst[k].n);

    /* Direct -> Pitch at +0.25 is one octave. */
    fresh(h);
    slot(h, 1, "Direct", "Off", "Pitch (all)", 0.25);
    note_on(h, 69, 100);
    render(h, 4096, NULL, NULL);
    float hz = hz_now(h, 22050);
    CHECK(fabsf(hz - 880.0f) < 10.0f, "Direct->Pitch +0.25: %g Hz", (double)hz);
    slot(h, 1, "Direct", "Off", "Pitch (all)", -0.25);
    render(h, 4096, NULL, NULL);
    hz = hz_now(h, 22050);
    CHECK(fabsf(hz - 220.0f) < 6.0f, "Direct->Pitch -0.25: %g Hz", (double)hz);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);

    /* Two sources multiply: Wheel x Direct -> Amp at -1 mutes only while the wheel is up. */
    fresh(h);
    slot(h, 1, "Mod Wheel", "Direct", "Amp Level", -1.0);
    note_on(h, 60, 100);
    render(h, 4096, x, NULL);
    float quiet_off = rms(x + 2048, 2048);
    midi(h, 0xB0, 1, 127);
    render(h, 8192, x, NULL);
    float quiet_on = rms(x + 6144, 2048);
    CHECK(quiet_off > 0.05f && quiet_on < 0.01f, "wheel x direct -> amp: %g then %g", (double)quiet_off, (double)quiet_on);
    midi(h, 0xB0, 120, 0);
    midi(h, 0xB0, 121, 0);
    render(h, 2048, NULL, NULL);

    /* Poly aftertouch reaches only its own voice. */
    fresh(h);
    slot(h, 1, "Poly AT", "Off", "Amp Level", -1.0);
    note_on(h, 60, 100);
    render(h, 2048, x, NULL);
    float before = rms(x + 1024, 1024);
    midi(h, 0xA0, 60, 127);
    render(h, 4096, x, NULL);
    float after = rms(x + 3072, 1024);
    CHECK(before > 0.05f && after < 0.01f, "poly AT -> amp: %g then %g", (double)before, (double)after);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);

    /* A slow LFO on pitch gives vibrato. */
    fresh(h);
    sp(h, "l1_wave", "Sine");
    spf(h, "l1_rate", 0.5995); /* 5 Hz */
    slot(h, 1, "LFO 1", "Off", "Pitch (all)", 0.04);
    note_on(h, 69, 100);
    render(h, 4096, NULL, NULL);
    float lo = 1e9f, hi = -1e9f;
    for (int w = 0; w < 20; w++) { /* quarter-cycle windows: a half-cycle window would average the sweep away */
        float f = hz_now(h, 2205);
        lo = fminf(lo, f);
        hi = fmaxf(hi, f);
    }
    CHECK(hi - lo > 25.0f, "LFO vibrato sweeps %g..%g Hz", (double)lo, (double)hi);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);

    /* Envelope time destinations, cutoff and a global (effect) destination. */
    fresh(h);
    slot(h, 1, "Direct", "Off", "Amp Attack", 1.0);
    spf(h, "ae_a", 0.0);
    note_on(h, 60, 100);
    render(h, 8192, x, NULL);
    CHECK(peak(x, 8192) < 0.05f, "Direct->Amp Attack makes the attack very slow (%g)", (double)peak(x, 8192));
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);
    fresh(h);
    sp(h, "o1_wave", "Saw");
    spf(h, "flt_cut", 0.5);
    note_on(h, 60, 100);
    render(h, 4096, x, NULL);
    float dark = rms(x + 2048, 2048);
    slot(h, 1, "Direct", "Off", "Cutoff", 0.3);
    render(h, 4096, x, NULL);
    float bright = rms(x + 2048, 2048);
    CHECK(bright > 1.2f * dark, "Direct->Cutoff opens the filter (%g -> %g)", (double)dark, (double)bright);
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);
    fresh(h);
    spf(h, "ae_r", 0.1);
    slot(h, 1, "Direct", "Off", "Reverb Level", 1.0);
    render(h, 128 * 120, NULL, NULL);
    note_on(h, 60, 100);
    render(h, 4096, NULL, NULL);
    note_off(h, 60);
    render(h, 22050, x, NULL);
    CHECK(rms(x + 11025, 4096) > 1e-3f, "Direct->Reverb Level makes a tail (%g)", (double)rms(x + 11025, 4096));

    /* Proxy keys edit the selected slot; the list page reflects it. */
    fresh(h);
    sp(h, "mm_sel", "3");
    sp(h, "mm_s1", "LFO 1");
    sp(h, "mm_s2", "Mod Wheel");
    sp(h, "mm_dst", "Cutoff");
    sp(h, "mm_dep", "0.5");
    CHECK((int)gp(h, "mm04_s1") == MS_LFO1 && (int)gp(h, "mm04_s2") == MS_WHEEL && (int)gp(h, "mm04_dst") == MD_CUTOFF &&
              fabsf(gp(h, "mm04_dep") - 0.5f) < 1e-3f,
          "proxy writes land in slot 4");
    static char items[2000];
    int n = api->get_param(h, "mm_items", items, sizeof items);
    CHECK(n > 0 && strstr(items, "\"label\":\"4: LFO1xWhl>Cut\"") && strstr(items, "\"label\":\"1: --\""), "matrix items: %.80s", items);
    sp(h, "mm_sel", "0");
    CHECK((int)gp(h, "mm_s1") == MS_OFF && (int)gp(h, "mm_slot") == 1, "selecting slot 1 shows its values");
    sp(h, "mm_slot", "4");
    CHECK((int)gp(h, "mm_sel") == 3 && (int)gp(h, "mm_s1") == MS_LFO1, "mm_slot selects");
    sp(h, "mm_clear", "Clear");
    CHECK((int)gp(h, "mm04_s1") == MS_OFF && fabsf(gp(h, "mm04_dep")) < 1e-6f && (int)gp(h, "mm_clear") == 0, "clear empties the slot");
    CHECK(I->bp.nslots == 0, "no active slots after clear");

    /* Macros: one knob moves its assigned destinations. */
    fresh(h);
    sp(h, "mac_sel", "0");
    sp(h, "mac_k", "1");
    sp(h, "mac_dst", "Pitch (all)");
    sp(h, "mac_amt", "0.25");
    CHECK((int)gp(h, "mac1_d1") == MD_PITCH && fabsf(gp(h, "mac1_a1") - 0.25f) < 1e-3f, "macro proxy writes the raw assignment");
    note_on(h, 69, 100);
    render(h, 4096, NULL, NULL);
    float hz0 = hz_now(h, 22050);
    spf(h, "mac1", 1.0);
    render(h, 4096, NULL, NULL);
    float hz1 = hz_now(h, 22050);
    CHECK(fabsf(hz0 - 440.0f) < 6.0f && fabsf(hz1 - 880.0f) < 10.0f, "macro 1 raises pitch an octave (%g -> %g)", (double)hz0, (double)hz1);
    n = api->get_param(h, "mac_items", items, sizeof items);
    CHECK(n > 0 && strstr(items, "\"label\":\"M1: Pitc\"") && strstr(items, "\"label\":\"M2: --\""), "macro items: %.80s", items);
    sp(h, "mac_k", "2");
    CHECK((int)gp(h, "mac_dst") == MD_OFF, "second assignment is empty");
    midi(h, 0xB0, 120, 0);
    render(h, 2048, NULL, NULL);

    /* Animate: parameter, hold via CC 80 and latch. */
    fresh(h);
    slot(h, 1, "Animate 1", "Off", "Pitch (all)", 0.25);
    note_on(h, 69, 100);
    render(h, 4096, NULL, NULL);
    CHECK(fabsf(hz_now(h, 22050) - 440.0f) < 6.0f, "animate off");
    midi(h, 0xB0, 80, 127);
    render(h, 4096, NULL, NULL);
    CHECK(fabsf(hz_now(h, 22050) - 880.0f) < 10.0f, "CC 80 holds Animate 1 on");
    midi(h, 0xB0, 80, 0);
    render(h, 8192, NULL, NULL);
    CHECK(fabsf(hz_now(h, 22050) - 440.0f) < 6.0f, "CC 80 release turns it off");
    sp(h, "anim1_mode", "Latch");
    midi(h, 0xB0, 80, 127);
    midi(h, 0xB0, 80, 0);
    render(h, 8192, NULL, NULL);
    CHECK(fabsf(hz_now(h, 22050) - 880.0f) < 10.0f, "latch stays on after release");
    midi(h, 0xB0, 80, 127);
    midi(h, 0xB0, 80, 0);
    render(h, 8192, NULL, NULL);
    CHECK(fabsf(hz_now(h, 22050) - 440.0f) < 6.0f, "second press latches off");
    sp(h, "anim1_mode", "Hold");
    spf(h, "anim1", 1.0);
    render(h, 8192, NULL, NULL);
    CHECK(fabsf(hz_now(h, 22050) - 880.0f) < 10.0f, "anim1 parameter turns it on");

    /* Hard-wired LFO routes. */
    fresh(h);
    sp(h, "l2_wave", "Sine");
    spf(h, "l2_rate", 0.5995); /* 5 Hz */
    spf(h, "o1_plfo", 0.3); /* cubic: 0.027 * 60 st = 1.6 st */
    note_on(h, 69, 100);
    render(h, 4096, NULL, NULL);
    lo = 1e9f;
    hi = -1e9f;
    for (int w = 0; w < 10; w++) {
        float f = hz_now(h, 4410);
        lo = fminf(lo, f);
        hi = fmaxf(hi, f);
    }
    CHECK(hi - lo > 8.0f, "osc LFO2>pitch sweeps %g..%g Hz", (double)lo, (double)hi);
    free(x);
    api->destroy_instance(h);
}
