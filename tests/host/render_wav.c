/* Render every factory preset to a WAV file and print level statistics.
 *   build/crest_render <out_dir> [preset_index]     (CREST_NO_WAV=1 prints the statistics only)
 * Poly presets play a held chord; mono presets play a short three-note phrase. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crest.h"

static plugin_api_v2_t *api;

static void midi(void *h, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    api->on_midi(h, m, 3, MOVE_MIDI_SOURCE_EXTERNAL);
}

static void put32(FILE *f, unsigned v) {
    fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f);
}
static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static void write_wav(const char *path, const int16_t *s, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + (unsigned)frames * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (unsigned)frames * 4);
    fwrite(s, 2, (size_t)frames * 2, f);
    fclose(f);
}

static void play(void *h, int frames, int16_t *dst, int *pos) {
    for (int p = 0; p < frames; p += 128) {
        api->render_block(h, dst + 2 * (*pos), 128);
        *pos += 128;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <out_dir> [preset]\n", argv[0]);
        return 2;
    }
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    api = move_plugin_init_v2(&host);
    int total = 44100 * 6;
    int16_t *pcm = (int16_t *)calloc((size_t)total * 2, sizeof(int16_t));
    int only = argc > 2 ? atoi(argv[2]) : -1;
    printf("%-3s %-20s %8s %8s %8s\n", "#", "preset", "peak dB", "rms dB", "dc");
    for (int i = 0; i < CREST_FACTORY_COUNT; i++) {
        if (only >= 0 && i != only) continue;
        void *h = api->create_instance(".", "");
        char v[16];
        snprintf(v, sizeof v, "%d", i);
        api->set_param(h, "preset", v);
        char b[32];
        api->get_param(h, "v_mode", b, sizeof b);
        int mono = atoi(b) >= 2 || !strncmp(b, "Mono", 4);
        int pos = 0;
        memset(pcm, 0, (size_t)total * 2 * sizeof(int16_t));
        if (mono) {
            int notes[3] = {40, 47, 52};
            for (int n = 0; n < 3; n++) {
                midi(h, 0x90, notes[n], 100);
                play(h, 44100 * 3 / 4, pcm, &pos);
                midi(h, 0x80, notes[n], 0);
            }
        } else {
            midi(h, 0x90, 48, 100); midi(h, 0x90, 55, 100); midi(h, 0x90, 64, 100);
            play(h, 44100 * 2, pcm, &pos);
            midi(h, 0x80, 48, 0); midi(h, 0x80, 55, 0); midi(h, 0x80, 64, 0);
        }
        play(h, total - pos, pcm, &pos);
        double sum = 0, dc = 0;
        int pk = 0;
        for (int k = 0; k < total * 2; k++) {
            int a = abs(pcm[k]);
            if (a > pk) pk = a;
            sum += (double)pcm[k] * (double)pcm[k];
            dc += pcm[k];
        }
        char name[64], path[512];
        api->get_param(h, "preset_name", name, sizeof name);
        double rmsv = sqrt(sum / (total * 2.0)) / 32768.0;
        printf("%-3d %-20s %8.1f %8.1f %8.4f\n", i, name, 20 * log10((pk + 1) / 32768.0),
               20 * log10(rmsv + 1e-9), dc / (total * 2.0) / 32768.0);
        char fn[64];
        int j = 0;
        for (const char *c = name; *c && j < 40; c++) fn[j++] = (*c == ' ') ? '_' : *c;
        fn[j] = 0;
        snprintf(path, sizeof path, "%s/%02d_%s.wav", argv[1], i, fn);
        if (!getenv("CREST_NO_WAV")) write_wav(path, pcm, total);
        api->destroy_instance(h);
    }
    free(pcm);
    return 0;
}
