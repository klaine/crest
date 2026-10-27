/* Tests for the engine features added for the Peak bank import: FM, filter FM, fixed notes,
 * divergence, effect routing and dry/wet. Included by test_main.c after test_m2m3.inc. */

static float render_hz(void *h, int note, int frames) {
    note_on(h, note, 100);
    render(h, 4096, NULL, NULL);
    float hz = hz_now(h, frames);
    note_off(h, note);
    render(h, 20000, NULL, NULL);
    return hz;
}

static double goertzel(const float *x, int n, double f) {
    double w = 2.0 * 3.14159265358979 * f / 44100.0, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double s0 = (double)x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/* FM between oscillators at the same pitch must stay harmonic: compare the strongest partial at a
 * multiple of f0 with the strongest one half-way between. */
static double inharmonic_db(const float *x, int n, double f0) {
    double h = 0, m = 0;
    for (int k = 1; k <= 8; k++) {
        h = fmax(h, goertzel(x, n, f0 * k));
        m = fmax(m, goertzel(x, n, f0 * (k + 0.5)));
    }
    return 10.0 * log10((m + 1e-20) / (h + 1e-20));
}

static void test_peak_features(void) {
    printf("peak features\n");
    void *h = mk();
    enum { N = 8192 };
    float *a = buf(N), *b = buf(N);

    /* Fixed note: the oscillator ignores the key. */
    fresh(h);
    spf(h, "o1_fixed", 81);
    float hz = render_hz(h, 48, 22050);
    CHECK(fabsf(hz - 880.0f) < 3.0f, "fixed note 81 plays %g Hz", (double)hz);

    /* Osc divergence: notes on different voices differ slightly in pitch. */
    fresh(h);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 6; i++) {
        float f = render_hz(h, 69, 44100);
        lo = fminf(lo, f);
        hi = fmaxf(hi, f);
    }
    CHECK(hi - lo < 1.5f, "no divergence is steady (%g..%g)", (double)lo, (double)hi);
    spf(h, "v_div", 1.0);
    lo = 1e9f;
    hi = -1e9f;
    for (int i = 0; i < 6; i++) {
        float f = render_hz(h, 69, 44100);
        lo = fminf(lo, f);
        hi = fmaxf(hi, f);
    }
    CHECK(hi - lo > 2.0f, "divergence spreads the pitch (%g..%g)", (double)lo, (double)hi);

    /* FM: osc 1 (muted) modulates osc 2. */
    fresh(h);
    spf(h, "mx_o1", 0.0);
    spf(h, "mx_o2", 1.0);
    sp(h, "o2_wave", "Sine");
    spf(h, "o2_coarse", 0);
    spf(h, "o2_fine", 0);
    note_on(h, 57, 100);
    render(h, 4096, NULL, NULL);
    render(h, N, a, NULL);
    spf(h, "fm_12", 0.7);
    render(h, 4096, NULL, NULL);
    render(h, N, b, NULL);
    CHECK(all_finite(b, N) && peak(b, N) < 1.5f, "FM output stays finite and bounded (%g)", (double)peak(b, N));
    CHECK(up_crossings(b, N) > up_crossings(a, N) + 10 || diff_rms(a, b, N) > 0.1f, "FM changes the tone (%d vs %d crossings)",
          up_crossings(b, N), up_crossings(a, N));
    spf(h, "fm_12", 0.0);
    note_off(h, 57);
    render(h, 30000, NULL, NULL);
    /* FM keeps the carrier in tune: same-pitch modulators give a harmonic spectrum at any depth. */
    {
        const char *mods[2] = {"Sine", "Saw"};
        for (int m = 0; m < 2; m++)
            for (int d = 1; d <= 4; d++) {
                fresh(h);
                spf(h, "mx_o1", 0.0);
                spf(h, "mx_o2", 1.0);
                sp(h, "o1_wave", mods[m]);
                sp(h, "o2_wave", "Sine");
                spf(h, "flt_cut", 1.0);
                spf(h, "fm_12", 0.25 * d);
                note_on(h, 57, 100);
                render(h, 4096, NULL, NULL);
                render(h, N, b, NULL);
                note_off(h, 57);
                render(h, 30000, NULL, NULL);
                double db = inharmonic_db(b, N, 220.0);
                CHECK(db < -25.0, "FM %s depth %.2f stays harmonic (%.1f dB)", mods[m], 0.25 * d, db);
            }
    }
    spf(h, "fm_31", 0.8);
    sp(h, "o3_wave", "Sine");
    spf(h, "o3_coarse", 7);
    render(h, 4096, NULL, NULL);
    render(h, N, b, NULL);
    CHECK(all_finite(b, N) && peak(b, N) < 1.5f, "FM through the Osc3 loop stays bounded");
    note_off(h, 57);
    render(h, 20000, NULL, NULL);

    /* Filter FM from osc 3 and from noise. */
    fresh(h);
    sp(h, "o1_wave", "Saw");
    spf(h, "flt_cut", 0.5);
    spf(h, "mx_o3", 0.0);
    sp(h, "o3_wave", "Sine");
    spf(h, "o3_coarse", 12);
    note_on(h, 52, 100);
    render(h, 4096, NULL, NULL);
    render(h, N, a, NULL);
    spf(h, "flt_o3", 0.8);
    render(h, 4096, NULL, NULL);
    render(h, N, b, NULL);
    CHECK(all_finite(b, N) && diff_rms(a, b, N) > 0.02f, "Osc3 > cutoff changes the sound (%g)", (double)diff_rms(a, b, N));
    spf(h, "flt_o3", 0.0);
    spf(h, "flt_ns", 0.8);
    render(h, 4096, NULL, NULL);
    render(h, N, b, NULL);
    CHECK(all_finite(b, N) && diff_rms(a, b, N) > 0.02f, "Noise > cutoff changes the sound (%g)", (double)diff_rms(a, b, N));
    note_off(h, 52);
    render(h, 20000, NULL, NULL);

    /* Effect routing and dry/wet. */
    float *c = buf(N);
    for (int pass = 0; pass < 3; pass++) {
        fresh(h);
        sp(h, "o1_wave", "Saw");
        sp(h, "osc_reset", "Reset");
        spf(h, "v_drift", 0.0);
        spf(h, "cho_lvl", 0.6);
        spf(h, "dly_lvl", 0.6);
        spf(h, "dly_time", 0.2);
        spf(h, "rev_lvl", 0.6);
        sp(h, "fx_route", pass == 0 ? "Parallel" : "Cho>Dly>Rev");
        if (pass == 2) spf(h, "fx_wet", 0.0);
        note_on(h, 57, 100);
        render(h, 8192, NULL, NULL);
        render(h, N, pass == 1 ? b : a, NULL);
        if (pass == 2) {
            note_off(h, 57);
            render(h, 20000, NULL, NULL);
            fresh(h);
            sp(h, "o1_wave", "Saw");
            sp(h, "osc_reset", "Reset");
            spf(h, "v_drift", 0.0);
            note_on(h, 57, 100);
            render(h, 8192, NULL, NULL);
            render(h, N, c, NULL);
            CHECK(diff_rms(a, c, N) < 0.01f, "wet 0 equals the dry sound (%g)", (double)diff_rms(a, c, N));
        } else {
            note_off(h, 57);
            render(h, 20000, NULL, NULL);
        }
    }
    free(a);
    free(b);
    free(c);

    char out[64];
    sp(h, "mm01_s1", "LFO 2+");
    params_get((crest_inst_t *)h, "mm01_s1", out, sizeof out);
    CHECK(strcmp(out, "LFO 2+") == 0 || atoi(out) == MS_LFO2P, "new unipolar source is selectable (%s)", out);
    api->destroy_instance(h);
}

/* Arpeggiator: collect the order in which notes get gated on. */
static int arp_run(void *h, int *notes, int max, int frames) {
    crest_inst_t *I = (crest_inst_t *)h;
    int n = 0, last = -1;
    for (int done = 0; done < frames; done += 128) {
        render(h, 128, NULL, NULL);
        int cur = -1, cnt = 0;
        for (int i = 0; i < CREST_VOICES; i++)
            if (I->voice[i].active && I->voice[i].gate && !I->voice[i].fading) {
                cur = I->voice[i].note;
                cnt++;
            }
        if (cnt == 0) {
            last = -1;
            continue;
        }
        if (cur != last && n < max) notes[n++] = cur;
        last = cur;
    }
    return n;
}

static void test_arp(void) {
    printf("arpeggiator\n");
    void *h = mk();
    int seq[64];
    fresh(h);
    sp(h, "arp_rate", "1/16");
    sp(h, "arp_on", "On");
    spf(h, "ae_r", 0.0);
    spf(h, "arp_gate", 0.5);
    note_on(h, 55, 100);
    note_on(h, 48, 100);
    note_on(h, 52, 100);
    int n = arp_run(h, seq, 64, 44100 * 2);
    CHECK(n >= 12, "arp steps (%d)", n);
    int ok = n >= 12;
    for (int i = 0; i < n && ok; i++) ok = seq[i] == (int[]){48, 52, 55}[i % 3];
    CHECK(ok, "up order 48 52 55 repeats (%d %d %d %d)", seq[0], seq[1], seq[2], seq[3]);
    CHECK(n >= 12 && n <= 15, "1/16 at 100 bpm gives about 13 steps in 2 s (%d)", n);

    sp(h, "arp_mode", "Down");
    n = arp_run(h, seq, 64, 44100);
    ok = n >= 6;
    for (int i = 0; i + 1 < n; i++) ok &= seq[i + 1] < seq[i] || (seq[i] == 48 && seq[i + 1] == 55);
    CHECK(ok, "down order (%d %d %d %d)", seq[0], seq[1], seq[2], seq[3]);

    sp(h, "arp_mode", "UpDown2");
    n = arp_run(h, seq, 64, 44100);
    int pat = 0;
    for (int i = 0; i + 3 < n; i++) pat += seq[i] == seq[i + 2] || 1;
    CHECK(n >= 6, "updown2 runs");
    sp(h, "arp_mode", "Up");
    spf(h, "arp_oct", 2);
    n = arp_run(h, seq, 64, 44100);
    int mx = 0;
    for (int i = 0; i < n; i++) mx = seq[i] > mx ? seq[i] : mx;
    CHECK(mx == 67, "two octaves reach 67 (%d)", mx);
    spf(h, "arp_oct", 1);

    sp(h, "arp_mode", "Chord");
    render(h, 8192, NULL, NULL);
    int gated = 0;
    for (int i = 0; i < 128 * 40; i += 128) {
        render(h, 128, NULL, NULL);
        int c = 0;
        crest_inst_t *I = (crest_inst_t *)h;
        for (int k = 0; k < CREST_VOICES; k++) c += I->voice[k].active && I->voice[k].gate && !I->voice[k].fading;
        if (c > gated) gated = c;
    }
    CHECK(gated == 3, "chord mode gates all keys together (%d)", gated);
    sp(h, "arp_mode", "Up");

    /* Latch keeps playing after the keys are released; a new phrase replaces it. */
    sp(h, "arp_latch", "On");
    note_off(h, 55);
    note_off(h, 48);
    note_off(h, 52);
    n = arp_run(h, seq, 64, 22050);
    CHECK(n >= 3, "latched arp keeps playing (%d)", n);
    note_on(h, 60, 100);
    note_off(h, 60);
    n = arp_run(h, seq, 64, 22050);
    int only60 = n > 0;
    for (int i = 0; i < n; i++) only60 &= seq[i] == 60;
    CHECK(only60, "a new phrase replaces the latched keys");

    /* Rhythm pattern 1 plays every other step. */
    sp(h, "arp_latch", "Off");
    midi(h, 0xB0, 123, 0);
    render(h, 22050, NULL, NULL);
    spf(h, "arp_rhy", 1);
    note_on(h, 60, 100);
    n = arp_run(h, seq, 64, 44100 * 2);
    CHECK(n >= 7 && n <= 9, "rhythm 1 halves the step count (%d)", n);
    note_off(h, 60);
    n = arp_run(h, seq, 64, 22050);
    CHECK(n == 0, "released keys stop the arp");

    /* Switching it off releases everything. */
    note_on(h, 60, 100);
    render(h, 4096, NULL, NULL);
    sp(h, "arp_on", "Off");
    n = arp_run(h, seq, 64, 22050);
    CHECK(n == 0, "arp off silences the stream (%d)", n);
    note_on(h, 64, 100);
    render(h, 2048, NULL, NULL);
    CHECK(active_voices((crest_inst_t *)h) >= 1, "keys play directly again");
    api->destroy_instance(h);
    (void)pat;
}

/* A voice-mode change followed by a note in the same block (or a preset/state load in a mono mode
 * followed by notes before the first render) must not lose the note. */
static void test_vmode_change(void) {
    printf("voice mode changes\n");
    void *h = mk();
    enum { N = 4096 };
    float *a = buf(N);
    const char *modes[4] = {"Poly2", "Mono", "MonoLG", "Mono2"};
    for (int m = 0; m < 4; m++) {
        fresh(h);
        render(h, 512, NULL, NULL);
        sp(h, "v_mode", modes[m]);
        note_on(h, 60, 100);
        render(h, N, a, NULL);
        CHECK(peak(a, N) > 0.01f, "%s: the note right after the mode change sounds (%g)", modes[m], (double)peak(a, N));
        midi(h, 0xB0, 120, 0);
        render(h, 2048, NULL, NULL);

        char st[80];
        snprintf(st, sizeof st, "{\"fmt\":\"crest\",\"v_mode\":\"%s\",\"flt_cut\":\"1\"}", modes[m]);
        sp(h, "state", st);
        note_on(h, 60, 100);
        render(h, N, a, NULL);
        CHECK(peak(a, N) > 0.01f, "%s: a note before the first render after a state load sounds (%g)", modes[m],
              (double)peak(a, N));
        midi(h, 0xB0, 120, 0);
        render(h, 2048, NULL, NULL);
    }
    free(a);
    api->destroy_instance(h);
}
