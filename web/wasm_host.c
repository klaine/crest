/* WebAssembly shell around the Crest engine (not shipped to the device). Adapted from Amino's web/wasm_host.c.
 *
 * The same src/dsp sources that build dsp.so for Move are compiled to wasm32;
 * this file is the whole host: a one-instance Schwung stand-in with fixed
 * scratch buffers so JavaScript needs no malloc.  web/worklet.js drives it
 * from an AudioWorklet.  Mirrors the native test host (tests/host).
 */
#include <stdint.h>
#include <string.h>

#include "level.h"
#include "peak_import.h"
#include "plugin_api.h"

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

#define EXPORT __attribute__((used, visibility("default")))
#define KEY_CAP 256
#define VAL_CAP 65536
#define OUT_CAP 262144

static host_api_v1_t g_host;
static plugin_api_v2_t *g_api;
static void *g_inst;
static float g_bpm = 120.0f;
static int g_sr = 44100;
static int g_running;
static double g_beats;

static char g_key[KEY_CAP];
static char g_val[VAL_CAP];
static char g_out[OUT_CAP];
static int16_t g_pcm[128 * 2];

static float host_bpm(void) { return g_bpm; }
static int host_clock(void) {
    return g_running ? MOVE_CLOCK_STATUS_RUNNING : MOVE_CLOCK_STATUS_STOPPED;
}
static double host_beats(void) { return g_running ? g_beats : -1.0; }

EXPORT char *wa_key(void) { return g_key; }
EXPORT char *wa_val(void) { return g_val; }
EXPORT char *wa_out(void) { return g_out; }
EXPORT int16_t *wa_pcm(void) { return g_pcm; }
EXPORT int wa_key_cap(void) { return KEY_CAP; }
EXPORT int wa_val_cap(void) { return VAL_CAP; }
EXPORT int wa_out_cap(void) { return OUT_CAP; }

/* Create the single instance at the AudioContext's sample rate. */
EXPORT int wa_init(int sample_rate) {
    if (g_inst) return 1;
    g_sr = sample_rate > 0 ? sample_rate : 44100;
    memset(&g_host, 0, sizeof g_host);
    g_host.api_version = MOVE_PLUGIN_API_VERSION;
    g_host.sample_rate = g_sr;
    g_host.frames_per_block = 128;
    g_host.get_bpm = host_bpm;
    g_host.get_clock_status = host_clock;
    g_host.get_beat_position = host_beats;
    g_api = move_plugin_init_v2(&g_host);
    if (!g_api) return 0;
    g_inst = g_api->create_instance("", 0);
    return g_inst ? 1 : 0;
}

EXPORT void wa_set_tempo(float bpm, int running) {
    g_bpm = bpm > 20.0f ? bpm : 120.0f;
    if (running && !g_running) g_beats = 0.0;
    g_running = running ? 1 : 0;
}

/* set_param(key, val) with both strings already in wa_key()/wa_val(). */
EXPORT void wa_set(void) {
    g_key[KEY_CAP - 1] = 0;
    g_val[VAL_CAP - 1] = 0;
    if (g_inst) g_api->set_param(g_inst, g_key, g_val);
}

/* get_param(key) -> wa_out(); returns the byte length, or -1 when unknown. */
EXPORT int wa_get(void) {
    g_key[KEY_CAP - 1] = 0;
    if (!g_inst) return -1;
    g_out[0] = 0;
    return g_api->get_param(g_inst, g_key, g_out, OUT_CAP);
}

EXPORT void wa_midi(int status, int d1, int d2) {
    if (!g_inst) return;
    uint8_t msg[3] = { (uint8_t)status, (uint8_t)d1, (uint8_t)d2 };
    int len = (status & 0xF0) == 0xD0 || (status & 0xF0) == 0xC0 ? 2 : 3;
    g_api->on_midi(g_inst, msg, len, 0);
}

/* ---- Peak SysEx import (the browser's import worker runs its own copy of this module) ----
 * JS copies a .syx file into wa_syx(), wa_syx_scan() finds the patches, wa_syx_patch(i) converts one
 * into wa_vals() (P_COUNT floats, parameter order) with its name in wa_out(), and wa_level() solves the
 * loudness of the values in wa_vals() on a scratch instance (src/dsp/level.c). */
#define SYX_CAP (PEAK_BANK_MAX * PEAK_MSG_LEN * 2)
static uint8_t g_syx[SYX_CAP];
static int g_offs[PEAK_BANK_MAX];
static int g_nsyx;
static float g_vals[P_COUNT];

EXPORT uint8_t *wa_syx(void) { return g_syx; }
EXPORT int wa_syx_cap(void) { return SYX_CAP; }
EXPORT float *wa_vals(void) { return g_vals; }
EXPORT int wa_nparams(void) { return P_COUNT; }

EXPORT int wa_syx_scan(int len) {
    if (len < 0 || len > SYX_CAP) len = SYX_CAP;
    g_nsyx = peak_scan(g_syx, len, g_offs, PEAK_BANK_MAX);
    return g_nsyx;
}

/* Convert patch i into wa_vals(); returns the length of its name in wa_out(), or -1. */
EXPORT int wa_syx_patch(int i) {
    if (i < 0 || i >= g_nsyx) return -1;
    char name[PEAK_NAME_MAX];
    peak_convert(g_syx + g_offs[i], g_vals, name);
    int n = (int)strlen(name);
    memcpy(g_out, name, (size_t)n + 1);
    return n;
}

/* patch_vol for the values in wa_vals() (needs wa_init first); < 0 when no instance was free. */
EXPORT double wa_level(int fast) {
    if (!g_api) return -1.0;
    return crest_level_solve(g_api, g_vals, fast);
}

/* Render up to 128 stereo frames into wa_pcm() (interleaved int16). */
EXPORT void wa_render(int frames) {
    if (!g_inst) return;
    if (frames > 128) frames = 128;
    g_api->render_block(g_inst, g_pcm, frames);
    if (g_running) g_beats += (double)frames * (double)g_bpm / (60.0 * g_sr);
}
