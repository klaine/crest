#include "level.h"

#include <math.h>
#include <stdlib.h>

#include "crest.h"

#define PROBE_VOL 0.3

static void midi(const plugin_api_v2_t *api, void *h, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}

typedef struct {
    int pk, pos;
    double sum;
} acc_t;

static void play(const plugin_api_v2_t *api, void *h, acc_t *a, int frames) {
    int16_t pcm[2 * 128];
    for (int p = 0; p < frames; p += 128, a->pos += 128) {
        api->render_block(h, pcm, 128);
        for (int k = 0; k < 256; k++) {
            int x = abs(pcm[k]);
            if (x > a->pk) a->pk = x;
            a->sum += (double)pcm[k] * (double)pcm[k];
        }
    }
}

int crest_level_measure(const plugin_api_v2_t *api, const float *v, double vol, double *pk_db, double *rms_db) {
    void *inst = api->create_instance("", "");
    if (!inst) return 0;
    crest_inst_t *I = (crest_inst_t *)inst;
    preset_load_values(I, v, "level");
    I->offline = 1; /* fixed 120 bpm, no transport: the result must not depend on what the host is doing */
    I->v[P_PATCH_VOL] = (float)vol;
    I->pdirty = 1;
    enum { TOTAL = 44100 * 6 };
    acc_t a = {0, 0, 0.0};
    if ((int)I->v[P_V_MODE] >= VM_MONO) {
        static const int notes[3] = {40, 47, 52};
        for (int n = 0; n < 3; n++) {
            midi(api, inst, 0x90, notes[n], 100);
            play(api, inst, &a, 44100 * 3 / 4);
            midi(api, inst, 0x80, notes[n], 0);
        }
    } else {
        midi(api, inst, 0x90, 48, 100);
        midi(api, inst, 0x90, 55, 100);
        midi(api, inst, 0x90, 64, 100);
        play(api, inst, &a, 44100 * 2);
        midi(api, inst, 0x80, 48, 0);
        midi(api, inst, 0x80, 55, 0);
        midi(api, inst, 0x80, 64, 0);
    }
    play(api, inst, &a, TOTAL - a.pos);
    api->destroy_instance(inst);
    *pk_db = 20.0 * log10((a.pk + 1) / 32768.0);
    *rms_db = 20.0 * log10(sqrt(a.sum / (a.pos * 2.0)) / 32768.0 + 1e-9);
    return 1;
}

double crest_level_solve(const plugin_api_v2_t *api, const float *v, int fast) {
    double vol = PROBE_VOL, pk, rms;
    if (!crest_level_measure(api, v, vol, &pk, &rms)) return -1.0;
    /* master gain is 2 * vol^2; rescale and re-measure while the probe was too hot or too quiet */
    for (int it = 0; it < 3; it++) {
        if (pk < -80.0) break; /* silent: nothing to calibrate */
        double d = fmin(-3.0 - pk, -17.0 - rms);
        double v2 = vol * pow(10.0, d / 40.0);
        if (v2 > 1.0) v2 = 1.0;
        if (v2 < 0.02) v2 = 0.02;
        if (fabs(v2 - vol) < 0.004) break;
        int hot = pk > -9.0; /* near the output clip, the gain is no longer linear */
        vol = v2;
        if (fast && !hot) break;
        if (!crest_level_measure(api, v, vol, &pk, &rms)) return -1.0;
    }
    return vol;
}
