/* Tests for the Peak envelope and LFO behaviour (manual pp.20-23): LFO ranges, repeats, fade modes,
 * MonoTrig / FadeSync, Common mode, envelope triggers in the mono modes and negative velocity.
 * Included by test_main.c after test_peakfx.inc. */

static int lfo_cycles(lfo_t *l, const lfo_par_t *p, float seconds) {
    int up = 0;
    float prev = 0.0f;
    for (int i = 0; i < (int)(seconds / CREST_CTL_DT); i++) {
        float v = lfo_tick(l, p, 0.0f, -1.0, 120.0f, NULL);
        if (prev < 0.0f && v >= 0.0f) up++;
        prev = v;
    }
    return up;
}

static void test_lfo_peak(void) {
    printf("lfo (Peak ranges, repeats, fade modes)\n");
    /* Knob ranges: Low 0.02..200 Hz, High 0.16..1600 Hz (capped below the control-rate limit). */
    lfo_par_t r = {.wave = LW_SINE};
    r.rate = 0.0f;
    float lo_min = lfo_rate_hz(&r, 0, 120);
    r.rate = 1.0f;
    float lo_max = lfo_rate_hz(&r, 0, 120);
    r.range = LR_HIGH;
    float hi_max = lfo_rate_hz(&r, 0, 120);
    r.rate = 0.0f;
    float hi_min = lfo_rate_hz(&r, 0, 120);
    r.range = LR_WIDE;
    float w_min = lfo_rate_hz(&r, 0, 120);
    CHECK(fabsf(lo_min - 0.02f) < 1e-4f && fabsf(lo_max - 200.0f) < 0.1f && fabsf(hi_min - 0.16f) < 1e-3f &&
              hi_max == LFO_MAX_HZ && fabsf(w_min - 0.02f) < 1e-4f,
          "rate ranges: low %g..%g, high %g..%g Hz", (double)lo_min, (double)lo_max, (double)hi_min, (double)hi_max);

    /* A fast High-range LFO really runs at its rate (400 Hz). */
    lfo_par_t f = {.wave = LW_SINE, .range = LR_HIGH, .rate = 0.849485f};
    lfo_t l = {0};
    lfo_start(&l, &f, 0);
    int up = lfo_cycles(&l, &f, 1.0f);
    CHECK(abs(up - 400) <= 2, "400 Hz LFO: %d cycles in 1 s", up);

    /* The sync table is the Peak's: 64 beats down to a 1/32 triplet. */
    CHECK(lfo_sync_beats(1) == 64.0f && lfo_sync_beats(26) == 1.0f && fabsf(lfo_sync_beats(35) - 1.0f / 12.0f) < 1e-6f &&
              lfo_sync_beats(36) == 0.0f,
          "LFO sync table");

    /* Repeats: three cycles from the key, then the LFO holds where the last cycle ends. */
    lfo_par_t rp = {.wave = LW_SINE, .trig = LT_KEY, .rep = 3, .rate = 0.5995f}; /* 5 Hz */
    lfo_start(&l, &rp, 0);
    up = lfo_cycles(&l, &rp, 2.0f);
    float held = lfo_tick(&l, &rp, 0.0f, -1.0, 120.0f, NULL);
    CHECK(up == 3 && l.done && fabsf(held) < 1e-3f, "rep=3: %d cycles, then holds %g", up, (double)held);
    lfo_par_t sd = {.wave = LW_SAWDN, .trig = LT_KEY, .rep = 1, .rate = 0.5995f};
    lfo_start(&l, &sd, 0);
    float first = lfo_tick(&l, &sd, 0.0f, -1.0, 120.0f, NULL), last = 0.0f;
    for (int i = 0; i < (int)(1.0f / CREST_CTL_DT); i++) last = lfo_tick(&l, &sd, 0.0f, -1.0, 120.0f, NULL);
    CHECK(first > 0.95f && last < -0.99f, "one falling saw: %g down to %g, held", (double)first, (double)last);

    /* Fade modes over a 1 s Fade Time on a square: measure the depth early (0.2 s) and late (1.5 s). */
    struct { int mode; float early, late; } fm[] = {
        {LF_FADEIN, 0.2f, 1.0f}, {LF_FADEOUT, 0.8f, 0.0f}, {LF_GATEIN, 0.0f, 1.0f}, {LF_GATEOUT, 1.0f, 0.0f}};
    for (int k = 0; k < 4; k++) {
        lfo_par_t q = {.wave = LW_SQUARE, .rate = 0.6f, .fade = 0.1f, .fmode = fm[k].mode};
        lfo_start(&l, &q, 0);
        float e = 0.0f, la = 0.0f;
        for (int i = 0; i < (int)(1.6f / CREST_CTL_DT); i++) {
            float x = fabsf(lfo_tick(&l, &q, 0.0f, -1.0, 120.0f, NULL));
            float t = (float)(i + 1) * CREST_CTL_DT;
            if (fabsf(t - 0.2f) < CREST_CTL_DT * 0.5f) e = x;
            if (t > 1.5f) la = x;
        }
        CHECK(fabsf(e - fm[k].early) < 0.02f && fabsf(la - fm[k].late) < 0.02f, "fade mode %d: %g then %g", fm[k].mode,
              (double)e, (double)la);
        q.fade = 0.0f; /* Fade Time 0: full depth in every mode */
        lfo_start(&l, &q, 0);
        float x = fabsf(lfo_tick(&l, &q, 0.0f, -1.0, 120.0f, NULL));
        CHECK(x > 0.99f, "fade mode %d at Fade Time 0 is full depth (%g)", fm[k].mode, (double)x);
    }
}

/* A poly or mono patch with LFO 1 on a slow sine, fading in over 1 s. */
static void lfo_patch(void *h, const char *mode, const char *trig) {
    fresh(h);
    sp(h, "v_mode", mode);
    sp(h, "l1_wave", "Sine");
    spf(h, "l1_rate", 0.3);
    spf(h, "l1_fade", 0.1);
    spf(h, "l1_phase", 0.25);
    sp(h, "l1_trig", trig);
}

static void test_lfo_voice(void) {
    printf("lfo retrigger, MonoTrig, FadeSync, Common\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;

    /* Poly: the same key again reuses its releasing voice; the fade starts over (Free phase kept). */
    lfo_patch(h, "Poly", "Free");
    spf(h, "ae_r", 0.6);
    note_on(h, 60, 100);
    render(h, 44100, NULL, NULL);
    note_off(h, 60);
    render(h, 1024, NULL, NULL);
    voice_t *v = &I->voice[0];
    float ph = v->lfo[0].phase;
    note_on(h, 60, 100);
    render(h, 128, NULL, NULL);
    CHECK(v->active && v->lfo[0].t < 0.01f, "reused poly voice restarts the fade (t = %g)", (double)v->lfo[0].t);
    CHECK(fabsf(v->lfo[0].phase - ph) < 0.05f, "a Free LFO keeps its phase on the reused voice");
    midi(h, 0xB0, 120, 0);
    render(h, 4096, NULL, NULL);

    /* Mono, legato: MonoTrig Legato keeps the Key LFO's phase, Re-Trig restarts it; FadeSync restarts the fade. */
    for (int mt = 0; mt < 2; mt++)
        for (int fs = 0; fs < 2; fs++) {
            lfo_patch(h, "Mono", "Key");
            sp(h, "l1_mtrig", mt ? "Re-Trig" : "Legato");
            sp(h, "l1_fsync", fs ? "On" : "Off");
            note_on(h, 60, 100);
            render(h, 22050, NULL, NULL);
            note_on(h, 64, 100); /* 60 still held */
            render(h, 128, NULL, NULL);
            v = &I->voice[0];
            int rephased = fabsf(v->lfo[0].phase - 0.25f) < 0.01f, retimed = v->lfo[0].t < 0.01f;
            CHECK(rephased == mt && retimed == fs, "mono legato, MonoTrig %d FadeSync %d: rephased %d retimed %d", mt, fs,
                  rephased, retimed);
            midi(h, 0xB0, 123, 0);
            render(h, 44100, NULL, NULL);
        }

    /* Common: every voice reads the same phase and the same random values (S&H). */
    lfo_patch(h, "Poly", "Common");
    sp(h, "l1_wave", "S&H");
    spf(h, "l1_rate", 0.6);
    spf(h, "l1_fade", 0.0);
    note_on(h, 60, 100);
    render(h, 3000, NULL, NULL);
    note_on(h, 64, 100);
    render(h, 9000, NULL, NULL);
    int same = 1;
    for (int b = 0; b < 20; b++) {
        render(h, 128, NULL, NULL);
        same &= I->voice[0].src[MS_LFO1] == I->voice[1].src[MS_LFO1];
    }
    CHECK(I->voice[0].active && I->voice[1].active && same, "Common S&H is shared by the voices");
    midi(h, 0xB0, 123, 0);
    render(h, 44100, NULL, NULL);

    /* CommonKey: the shared phase restarts at the panel phase on each new note. */
    lfo_patch(h, "Poly", "CommonKey");
    note_on(h, 60, 100);
    render(h, 20000, NULL, NULL);
    float before = I->cshare[0].phase;
    note_on(h, 67, 100);
    CHECK(fabsf(before - 0.25f) > 0.02f && I->cshare[0].phase == 0.25f, "CommonKey restarts the shared phase");
    midi(h, 0xB0, 123, 0);
    render(h, 44100, NULL, NULL);
    api->destroy_instance(h);
}

/* LFO 2 Depth reaches +-5 octaves at full and stays fine near zero (manual p.18). */
static void test_lfo2_depth(void) {
    printf("LFO 2 pitch depth\n");
    void *h = mk();
    struct { float depth, want_st; int note; } cs[] = {{1.0f, 60.0f, 45}, {-1.0f, -60.0f, 93}, {0.5f, 7.5f, 57}};
    for (int k = 0; k < 3; k++) {
        fresh(h);
        sp(h, "l2_wave", "Square"); /* +1 for the first 25 s at the slowest Low rate */
        sp(h, "l2_trig", "Key");
        spf(h, "l2_rate", 0.0);
        spf(h, "o1_plfo", (double)cs[k].depth);
        note_on(h, cs[k].note, 100);
        render(h, 4096, NULL, NULL);
        float want = semis_to_hz((float)cs[k].note + cs[k].want_st), hz = hz_now(h, 88200);
        CHECK(fabsf(hz - want) < 0.02f * want + 0.5f, "LFO 2 depth %g: %g Hz, want %g", (double)cs[k].depth, (double)hz,
              (double)want);
        midi(h, 0xB0, 123, 0);
        render(h, 44100, NULL, NULL);
    }
    api->destroy_instance(h);
}

/* Filter LFO 1 depth reaches eight octaves at full: a square LFO at +1 moves the cutoff up 96 st. */
static void test_flt_lfo_depth(void) {
    printf("filter LFO 1 depth\n");
    crest_inst_t *I = (crest_inst_t *)mk();
    void *h = I;
    fresh(h);
    sp(h, "l1_wave", "Square");
    sp(h, "l1_trig", "Key");
    spf(h, "l1_rate", 0.0);
    spf(h, "flt_cut", 0.0);
    spf(h, "flt_key", 0.0);
    spf(h, "flt_lfo", 1.0);
    note_on(h, 60, 100);
    render(h, 1024, NULL, NULL);
    float g = I->voice[0].p_g, want = tanf(CREST_PI * semis_to_hz(20.0f + 96.0f) / CREST_FS);
    CHECK(fabsf(g - want) < 0.01f * want, "LFO 1 at full depth puts the cutoff 96 st up (g %g, want %g)", (double)g, (double)want);
    midi(h, 0xB0, 123, 0);
    render(h, 44100, NULL, NULL);
    api->destroy_instance(h);
}

/* Mixer headroom (manual p.22): one oscillator at full passes clean, all three at full saturate. Sines at
 * the same pitch with the filter open, so the third harmonic is the saturation alone. */
static void test_mixer_headroom(void) {
    printf("mixer headroom\n");
    void *h = mk();
    enum { N = 22050 };
    float *x = buf(N);
    double h3db[2];
    for (int k = 0; k < 2; k++) {
        fresh(h);
        for (int o = 1; o <= 3; o++) {
            char key[16];
            snprintf(key, sizeof key, "o%d_wave", o);
            sp(h, key, "Sine");
            snprintf(key, sizeof key, "o%d_coarse", o);
            sp(h, key, "0");
            snprintf(key, sizeof key, "o%d_fine", o);
            sp(h, key, "0");
        }
        sp(h, "osc_reset", "Reset");
        spf(h, "v_drift", 0.0);
        spf(h, "patch_vol", 0.3);
        spf(h, "mx_o1", 1.0);
        spf(h, "mx_o2", k ? 1.0 : 0.0);
        spf(h, "mx_o3", k ? 1.0 : 0.0);
        note_on(h, 45, 100);
        render(h, 8192, NULL, NULL);
        render(h, N, x, NULL);
        h3db[k] = 10.0 * log10(goertzel(x, N, 330.0) / goertzel(x, N, 110.0));
        midi(h, 0xB0, 123, 0);
        render(h, 44100, NULL, NULL);
    }
    /* Level law x^1.5: the middle of a level knob is -9 dB. */
    float lvl[2];
    for (int k = 0; k < 2; k++) {
        fresh(h);
        spf(h, "v_drift", 0.0);
        spf(h, "mx_o1", k ? 0.5 : 1.0);
        note_on(h, 57, 100);
        render(h, 8192, NULL, NULL);
        render(h, N, x, NULL);
        lvl[k] = rms(x, N);
        midi(h, 0xB0, 123, 0);
        render(h, 44100, NULL, NULL);
    }
    float mid_db = 20.0f * log10f(lvl[1] / lvl[0]);
    CHECK(fabsf(mid_db + 9.03f) < 0.2f, "mixer level 0.5 is %g dB", (double)mid_db);
    CHECK(h3db[0] < -80.0 && h3db[1] > -30.0, "one osc at full: H3 %.0f dB; three at full saturate: H3 %.0f dB", h3db[0],
          h3db[1]);
    free(x);
    api->destroy_instance(h);
}

/* A steep triangle stays within range at any pitch and with sync (its ramps are kept two samples long). */
static void test_tri_peak(void) {
    printf("triangle peak\n");
    float worst = 0.0f;
    for (int vi = 0; vi <= 2; vi++)
        for (int fi = 0; fi < 4; fi++)
            for (int si = 0; si <= 4; si++) {
                static const float F[4] = {55.0f, 440.0f, 1760.0f, 3520.0f};
                osc_t o = {0};
                oscp_t p = {.wave = WAVE_TRI, .shape = (float)si / 4.0f, .vsync = (float)vi / 2.0f, .ddet = 0.5f};
                float dt = F[fi] / CREST_FS, b[16];
                for (int k = 0; k < 2000; k++) {
                    osc_render(&o, b, 16, dt, dt, &p, &p);
                    for (int i = 0; i < 16; i++) worst = fmaxf(worst, fabsf(b[i]));
                }
            }
    CHECK(worst < 1.2f, "triangle peak %g", (double)worst);
}

/* Effect levels and ranges against the manual (pp.31-32): the chorus Level adds the effect to the dry
 * signal (the dry stays at full), and the reverb PreDelay reaches about half a second. */
static void test_fx_peak(void) {
    printf("fx levels and ranges\n");
    void *h = mk();
    enum { N = 44100 };
    float *x = buf(N);
    /* Chorus delayed 30 ms with no sweep: before the wet copy arrives, the output is the dry alone. */
    float early[2];
    for (int k = 0; k < 2; k++) {
        fresh(h);
        spf(h, "ae_r", 0.0);
        sp(h, "fx_route", "Parallel");
        spf(h, "cho_lvl", k ? 1.0 : 0.0);
        spf(h, "cho_depth", 0.0);
        spf(h, "cho_dly", 1.0);
        render(h, 128 * 8, NULL, NULL);
        note_on(h, 69, 100);
        render(h, 2048, x, NULL);
        early[k] = rms(x + 300, 600);
        midi(h, 0xB0, 123, 0);
        render(h, N, NULL, NULL);
    }
    CHECK(fabsf(early[1] / early[0] - 1.0f) < 0.05f, "chorus at full keeps the dry at full (%g of it)", (double)(early[1] / early[0]));

    /* Reverb with PreDelay at full: nothing for the first ~0.45 s after a click, then the reverb. */
    fresh(h);
    spf(h, "ae_r", 0.0);
    spf(h, "rev_lvl", 1.0);
    spf(h, "rev_pre", 1.0);
    spf(h, "rev_decay", 0.5);
    render(h, 128 * 40, NULL, NULL);
    note_on(h, 69, 100);
    render(h, 256, NULL, NULL);
    note_off(h, 69);
    render(h, N, x, NULL);
    float gap = rms(x + 2205, 13230), tail = rms(x + 24255, 8820); /* 50..350 ms, 550..750 ms */
    CHECK(gap < 0.05f * tail && tail > 1e-4f, "reverb pre-delay: %g in the gap, %g after", (double)gap, (double)tail);

    /* Delay L/R Ratio 2/1: the right echoes come at half the left time; Width 0 makes them mono. */
    float *y = buf(N);
    for (int wdt = 1; wdt >= 0; wdt--) {
        fresh(h);
        spf(h, "ae_r", 0.0);
        spf(h, "dly_lvl", 1.0);
        spf(h, "dly_fb", 0.0);
        sp(h, "dly_sync", "Off");
        spf(h, "dly_time", 0.5); /* 141.4 ms = 6235 samples */
        sp(h, "dly_ratio", "2/1");
        spf(h, "dly_width", (double)wdt);
        render(h, 128 * 120, NULL, NULL);
        note_on(h, 69, 100);
        render(h, 1280, x, y);
        note_off(h, 69);
        render(h, N - 1280, x + 1280, y + 1280);
        float l_half = rms(x + 3117 + 300, 600), r_half = rms(y + 3117 + 300, 600);
        float l_full = rms(x + 6235 + 300, 600), r_full = rms(y + 6235 + 300, 600);
        if (wdt)
            CHECK(r_half > 20.0f * l_half && l_full > 20.0f * r_full && r_half > 0.05f,
                  "L/R 2/1: right echo at T/2 (%g vs %g), left at T (%g vs %g)", (double)r_half, (double)l_half,
                  (double)l_full, (double)r_full);
        else
            CHECK(fabsf(l_half - r_half) < 1e-3f && fabsf(l_full - r_full) < 1e-3f && l_half > 0.03f,
                  "width 0 centres the echoes (%g / %g)", (double)l_half, (double)r_half);
        midi(h, 0xB0, 123, 0);
        render(h, N, NULL, NULL);
    }
    free(y);

    /* Slew Rate: a free delay time follows its knob slowly at the top of the range, at once at 0. */
    crest_inst_t *I = (crest_inst_t *)h;
    float moved[2];
    for (int k = 0; k < 2; k++) {
        fresh(h);
        spf(h, "dly_lvl", 1.0);
        sp(h, "dly_sync", "Off");
        spf(h, "dly_time", 0.3);
        spf(h, "dly_slew", k ? 1.0 : 0.0);
        render(h, 128 * 40, NULL, NULL);
        float from = I->fx.y_dt;
        spf(h, "dly_time", 0.7);
        render(h, 4410, NULL, NULL); /* 100 ms */
        float to = 0.010f * powf(200.0f, 0.7f) * CREST_FS;
        moved[k] = (I->fx.y_dt - from) / (to - from);
    }
    CHECK(moved[0] > 0.99f && moved[1] < 0.1f, "delay slew: %g of the way at 0, %g at full after 100 ms", (double)moved[0],
          (double)moved[1]);
    free(x);
    api->destroy_instance(h);
}

/* Peak banks on the Move (peak_bank.c): the worker reads and converts the file, syx_patch picks a patch,
 * its loudness arrives later; before the bank is ready, syx_patch leaves the patch alone. */
static void wait_ms(void *h, int ms) { /* real time, so the worker gets to run; the instance keeps rendering */
    struct timespec t = {0, 10 * 1000000L};
    for (int i = 0; i < ms / 10; i++) {
        nanosleep(&t, NULL);
        render(h, 128, NULL, NULL);
    }
}

static void test_peak_bank(void) {
    printf("peak bank loading\n");
    const char *path = "reference_novation/Mylar Melodies'.syx";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("  (%s missing: skipped)\n", path);
        return;
    }
    static uint8_t raw[PEAK_BANK_MAX * PEAK_MSG_LEN];
    int len = (int)fread(raw, 1, sizeof raw, f);
    fclose(f);
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    sp(h, "preset", "3");
    char before[24];
    strcpy(before, I->name);
    sp(h, "syx_patch", "9"); /* no bank yet: kept, not loaded */
    CHECK(strcmp(I->name, before) == 0 && (int)gp(h, "syx_patch") == 9, "syx_patch before a bank leaves the patch");
    sp(h, "syx_file", path);
    char got[300];
    api->get_param(h, "syx_file", got, sizeof got);
    CHECK(strcmp(got, path) == 0, "syx_file reads back (%s)", got);
    for (int t = 0; t < 300 && peak_bank_count(I->slot) == 0; t++) wait_ms(h, 10);
    CHECK(peak_bank_count(I->slot) == 128, "bank loaded on the worker (%d patches)", peak_bank_count(I->slot));
    sp(h, "syx_patch", "9");
    int offs[PEAK_BANK_MAX];
    peak_scan(raw, len, offs, PEAK_BANK_MAX);
    float want[P_COUNT];
    char nm[PEAK_NAME_MAX];
    peak_convert(raw + offs[9], want, nm);
    int same = 1;
    for (int k = 0; k < P_COUNT; k++)
        if ((CREST_PARAMS[k].flags & PF_STATE) && k != P_PATCH_VOL && I->v[k] != want[k]) same = 0; /* saved values */
    CHECK(strcmp(I->name, nm) == 0 && same && I->syx_cur == 9, "patch 9 loaded: %s", I->name);
    for (int t = 0; t < 3000 && !I->syx_vol_set; t++) wait_ms(h, 10);
    CHECK(I->syx_vol_set && I->v[P_PATCH_VOL] >= 0.02f && I->v[P_PATCH_VOL] <= 1.0f, "its loudness arrives (%g)",
          (double)I->v[P_PATCH_VOL]);
    sp(h, "preset", "2");
    CHECK(I->syx_cur == -1, "a factory preset ends the bank patch");
    sp(h, "syx_file", "");
    for (int t = 0; t < 300 && peak_bank_count(I->slot) != 0; t++) wait_ms(h, 10);
    CHECK(peak_bank_count(I->slot) == 0, "an empty path empties the bank");
    api->destroy_instance(h);
}

static void test_env_peak(void) {
    printf("envelope triggers and velocity\n");
    void *h = mk();
    crest_inst_t *I = (crest_inst_t *)h;
    float *x = buf(8192);

    /* Mono and MonoLG treat the triggers alike (MonoLG only changes glide): Single (Legato) keeps
     * running on a legato note, Multi (Re-Trig) starts the attack again. */
    const char *modes[] = {"Mono", "MonoLG"};
    for (int m = 0; m < 2; m++) {
        fresh(h);
        sp(h, "v_mode", modes[m]);
        sp(h, "ae_trig", "Single");
        sp(h, "m1_trig", "Multi");
        spf(h, "m1_a", 0.3);
        spf(h, "m1_s", 0.5);
        note_on(h, 60, 100);
        render(h, 44100, NULL, NULL);
        voice_t *v = &I->voice[0];
        int ae0 = v->env[0].stage, m10 = v->env[1].stage;
        note_on(h, 62, 100);
        render(h, 128, NULL, NULL);
        CHECK(ae0 == ES_SUSTAIN && m10 == ES_SUSTAIN && v->env[0].stage == ES_SUSTAIN && v->env[1].stage == ES_ATTACK,
              "%s legato: amp env (Single) %d, mod env 1 (Multi) %d", modes[m], v->env[0].stage, v->env[1].stage);
        midi(h, 0xB0, 123, 0);
        render(h, 44100, NULL, NULL);
    }

    /* Negative amp velocity: harder notes are quieter. */
    float lv[2];
    int vels[2] = {30, 127};
    for (int k = 0; k < 2; k++) {
        fresh(h);
        spf(h, "ae_vel", -1.0);
        note_on(h, 60, vels[k]);
        render(h, 8192, x, NULL);
        lv[k] = rms(x + 4096, 4096);
        midi(h, 0xB0, 123, 0);
        render(h, 44100, NULL, NULL);
    }
    CHECK(lv[0] > 2.0f * lv[1] && lv[1] < 0.01f, "negative velocity: soft %g, hard %g", (double)lv[0], (double)lv[1]);
    free(x);
    api->destroy_instance(h);
}
