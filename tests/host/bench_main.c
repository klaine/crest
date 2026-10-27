/* Native CPU benchmark: time render_block for a few patches (plan/14 section 5).
 * The host machine is several times faster than Move's Cortex-A72, so read the numbers as a ratio
 * against the 2902 us block period, then calibrate on the device with the cpu_us debug key. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crest.h"

static plugin_api_v2_t *api;

static void sp(void *h, const char *k, const char *v) { api->set_param(h, k, v); }
static void spf(void *h, const char *k, double v) {
    char b[40];
    snprintf(b, sizeof b, "%.6f", v);
    sp(h, k, b);
}
static void note_on(void *h, int n) {
    uint8_t m[3] = {0x90, (uint8_t)n, 100};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec * 1e-3;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y);
}

static void run(const char *name, void *h, int voices) {
    static double t[3000];
    int16_t out[256];
    for (int i = 0; i < voices; i++) note_on(h, 40 + 5 * i);
    for (int i = 0; i < 400; i++) api->render_block(h, out, 128); /* settle, finish FX clears */
    for (int i = 0; i < 3000; i++) {
        double a = now_us();
        api->render_block(h, out, 128);
        t[i] = now_us() - a;
    }
    qsort(t, 3000, sizeof t[0], cmp);
    double sum = 0;
    for (int i = 0; i < 3000; i++) sum += t[i];
    printf("%-34s mean %6.1f us   p99 %6.1f us   max %6.1f us   (%4.1f%% of a block at p99)\n", name, sum / 3000.0,
           t[2969], t[2999], t[2969] / 2902.0 * 100.0);
    uint8_t m[3] = {0xB0, 120, 0};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    for (int i = 0; i < 40; i++) api->render_block(h, out, 128);
}

int main(void) {
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    api = move_plugin_init_v2(&host);
    void *h = api->create_instance(".", "");

    run("idle (no notes)", h, 0);
    run("1 voice, init saw", h, 1);
    run("8 voices, init saw", h, 8);

    sp(h, "state", "{\"fmt\":\"crest\"}");
    sp(h, "o1_wave", "Saw");
    sp(h, "o2_wave", "Saw");
    sp(h, "o3_wave", "Pulse");
    spf(h, "mx_o2", 1.0);
    spf(h, "mx_o3", 1.0);
    spf(h, "o1_shape", 0.5);
    spf(h, "flt_res", 0.6);
    spf(h, "flt_drive", 0.5);
    run("8 voices, 3 osc, resonant filter", h, 8);

    spf(h, "dst_lvl", 0.5);
    spf(h, "cho_lvl", 0.5);
    spf(h, "dly_lvl", 0.5);
    spf(h, "rev_lvl", 0.5);
    run("worst case: 3 osc + all 4 FX", h, 8);
    spf(h, "fm_12", 0.6);
    spf(h, "fm_23", 0.4);
    spf(h, "fm_31", 0.3);
    spf(h, "flt_o3", 0.3);
    spf(h, "o1_dens", 0.8);
    sp(h, "o2_wave", "Wavetable");
    spf(h, "o2_vsync", 0.4);
    run("worst case + FM, filter FM, density", h, 8);

    sp(h, "state", "{\"fmt\":\"crest\"}");
    spf(h, "rev_lvl", 0.5);
    run("1 voice + reverb only", h, 1);
    api->destroy_instance(h);
    return 0;
}
