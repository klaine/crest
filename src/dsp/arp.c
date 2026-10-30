#include <math.h>
#include <string.h>

#include "crest.h"
#include "dsp_util.h"
#include "rhythms_gen.h"

/* Step length in 24 ppqn ticks, slowest first; matches ARP_RATES in tools/gen/params.py. */
static const uint8_t ARP_TICKS[19] = {192, 144, 128, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};
_Static_assert(sizeof ARP_TICKS == 19, "arp rate table");

/* Pitch classes above the root; matches ARP_SCALES in tools/gen/params.py (Off and Set are not looked up). */
static const uint16_t SCALE_MASK[11] = {
    0x0FFF, 0x05AD,
    0x0AB5, /* Major */
    0x05AD, /* Minor */
    0x06AD, /* Dorian */
    0x05AB, /* Phrygian */
    0x0AD5, /* Lydian */
    0x06B5, /* Mixolydian */
    0x0295, /* Major pentatonic */
    0x04A9, /* Minor pentatonic */
    0x04E9, /* Blues */
};
#define SCALE_SET 1
#define SCALE_MINOR 3

int arp_enabled(const crest_inst_t *I) { return I->v[P_ARP_ON] >= 0.5f; }

static int name_is(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
        if (ca != *b) return 0;
    }
    return *a == 0 && *b == 0;
}

void arp_set_scale(crest_inst_t *I, int root_note, const char *scale) {
    static const struct {
        const char *name;
        uint16_t mask;
    } NAMES[] = {
        {"major", 0x0AB5},  {"minor", 0x05AD},  {"dorian", 0x06AD},
        {"phrygian", 0x05AB}, {"phryg", 0x05AB}, {"lydian", 0x0AD5},
        {"mixolydian", 0x06B5}, {"mixo", 0x06B5}, {"major pentatonic", 0x0295},
        {"pentatonic", 0x0295}, {"minor pentatonic", 0x04A9}, {"min pentatonic", 0x04A9},
        {"blues", 0x04E9},  {"chromatic", 0x0FFF},
    };
    arp_t *a = &I->arp;
    if (root_note >= 0 && root_note < 12) a->set_root = (int8_t)root_note;
    if (!scale) return;
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (name_is(scale, NAMES[i].name)) a->set_mask = NAMES[i].mask;
}

/* The nearest scale note at or below the key (above it at the very bottom of the range). */
static int quantize(int note, unsigned mask, int root) {
    for (int n = note; n >= 0; n--)
        if (mask >> ((n + 12 - root) % 12) & 1u) return n;
    for (int n = note + 1; n <= 127; n++)
        if (mask >> ((n + 12 - root) % 12) & 1u) return n;
    return note;
}

/* Rebuild the play order from the held keys, mode, octave range and scale. */
static void rebuild(crest_inst_t *I) {
    arp_t *a = &I->arp;
    int mode = (int)I->v[P_ARP_MODE], octs = clampi((int)I->v[P_ARP_OCT], 1, 6);
    uint8_t base[ARP_HELD], bvel[ARP_HELD];
    int nb = 0;
    if (a->nheld == 0) {
        a->nseq = 0;
        a->idx = 0;
        a->dir = 1;
        return;
    }
    int scale = clampi((int)I->v[P_ARP_SCALE], 0, 10);
    unsigned mask = SCALE_MASK[scale];
    int root = 0;
    if (scale == SCALE_SET) {
        mask = a->set_mask ? a->set_mask : SCALE_MASK[SCALE_MINOR];
        root = a->set_root;
    }
    /* keys that land on the same scale note play once; octaves stay in the scale */
    for (int i = 0; i < a->nheld; i++) {
        int n = scale ? quantize(a->held[i], mask, root) : a->held[i], dup = 0;
        for (int j = 0; j < nb; j++) dup |= base[j] == n;
        if (dup) continue;
        base[nb] = (uint8_t)n;
        bvel[nb++] = a->held_vel[i];
    }
    /* sorted order (with velocities) unless the mode keeps the order played */
    if (mode != 4) {
        for (int i = 1; i < nb; i++) { /* insertion sort, velocities follow */
            uint8_t n = base[i], v = bvel[i];
            int j = i - 1;
            while (j >= 0 && base[j] > n) {
                base[j + 1] = base[j];
                bvel[j + 1] = bvel[j];
                j--;
            }
            base[j + 1] = n;
            bvel[j + 1] = v;
        }
    }
    uint8_t up[ARP_SEQ], uv[ARP_SEQ];
    int nu = 0;
    if (mode == 6) octs = 1; /* chord: octaves shift the whole chord step by step */
    for (int o = 0; o < octs; o++)
        for (int i = 0; i < nb && nu < ARP_SEQ; i++) {
            int n = base[i] + 12 * o;
            if (n > 127) continue;
            up[nu] = (uint8_t)n;
            uv[nu++] = bvel[i];
        }
    int ns = 0;
    switch (mode) {
    case 1: /* down */
        for (int i = nu - 1; i >= 0; i--) {
            a->seq[ns] = up[i];
            a->seq_vel[ns++] = uv[i];
        }
        break;
    case 2: /* up/down, end notes repeated */
        for (int i = 0; i < nu && ns < ARP_SEQ; i++) {
            a->seq[ns] = up[i];
            a->seq_vel[ns++] = uv[i];
        }
        for (int i = nu - 1; i >= 0 && ns < ARP_SEQ; i--) {
            a->seq[ns] = up[i];
            a->seq_vel[ns++] = uv[i];
        }
        break;
    case 3: /* up/down, end notes once */
        for (int i = 0; i < nu && ns < ARP_SEQ; i++) {
            a->seq[ns] = up[i];
            a->seq_vel[ns++] = uv[i];
        }
        for (int i = nu - 2; i >= 1 && ns < ARP_SEQ; i--) {
            a->seq[ns] = up[i];
            a->seq_vel[ns++] = uv[i];
        }
        break;
    default: /* up, played, random (picks from this list), chord (the held keys) */
        for (int i = 0; i < nu; i++) {
            a->seq[ns] = up[i];
            a->seq_vel[ns++] = uv[i];
        }
        break;
    }
    a->nseq = ns;
    if (a->idx >= ns) a->idx = 0;
}

static void release_sounding(crest_inst_t *I) {
    arp_t *a = &I->arp;
    for (int i = 0; i < a->nsounding; i++) voice_note_off(I, a->sounding[i]);
    a->nsounding = 0;
}

static void play(crest_inst_t *I, int note, int vel) {
    arp_t *a = &I->arp;
    if (a->nsounding >= ARP_HELD) return;
    a->sounding[a->nsounding++] = (uint8_t)note;
    voice_note_on(I, note, vel);
}

void arp_all_off(crest_inst_t *I);

/* Detect the arp being switched on or off since the last call; returns whether it is on. */
static int toggled(crest_inst_t *I) {
    arp_t *a = &I->arp;
    int on = arp_enabled(I);
    if (on != a->was_on) {
        a->was_on = on;
        if (on) voice_all_off(I, 0); /* keys held before the switch would never get their note-off */
        arp_all_off(I);
        a->pos = 0.0;
    }
    return on;
}

void arp_all_off(crest_inst_t *I) {
    arp_t *a = &I->arp;
    release_sounding(I);
    a->nheld = a->nseq = a->phys_down = 0;
    a->latched = 0;
    a->idx = 0;
    a->dir = 1;
    a->last_step = -1;
    a->step_played = 0;
}

void arp_note_on(crest_inst_t *I, int note, int vel) {
    arp_t *a = &I->arp;
    toggled(I);
    if (a->latched) { /* a new phrase replaces the latched keys */
        a->nheld = 0;
        a->latched = 0;
    }
    int first = a->nheld == 0;
    int found = -1;
    for (int i = 0; i < a->nheld; i++)
        if (a->held[i] == note) found = i;
    if (found >= 0) {
        a->held_vel[found] = (uint8_t)vel;
    } else if (a->nheld < ARP_HELD) {
        a->held[a->nheld] = (uint8_t)note;
        a->held_vel[a->nheld++] = (uint8_t)vel;
    }
    a->phys_down++;
    if (first && I->v[P_ARP_KSYNC] >= 0.5f) {
        a->pos = 0.0;
        a->last_step = -1;
        a->idx = 0;
        a->dir = 1;
        a->step_played = 0;
        release_sounding(I);
    }
    rebuild(I);
}

void arp_note_off(crest_inst_t *I, int note) {
    arp_t *a = &I->arp;
    toggled(I);
    if (a->phys_down > 0) a->phys_down--;
    if (I->v[P_ARP_LATCH] >= 0.5f) {
        if (a->phys_down == 0) a->latched = 1;
        return;
    }
    for (int i = 0; i < a->nheld; i++)
        if (a->held[i] == note) {
            memmove(&a->held[i], &a->held[i + 1], (size_t)(a->nheld - i - 1));
            memmove(&a->held_vel[i], &a->held_vel[i + 1], (size_t)(a->nheld - i - 1));
            a->nheld--;
            break;
        }
    rebuild(I);
}

static int step_at(double pos, double len, float swing) {
    double pair = 2.0 * len;
    double pi = floor(pos / pair);
    double w = pos - pi * pair;
    return (int)pi * 2 + (w >= pair * (double)swing ? 1 : 0);
}

static void trigger(crest_inst_t *I, int step) {
    arp_t *a = &I->arp;
    int rh = clampi((int)I->v[P_ARP_RHY], 0, ARP_RHYTHMS - 1);
    int len = ARP_RHYTHM[rh].len;
    a->step_played = 0;
    if (a->nseq == 0 || !((ARP_RHYTHM[rh].mask >> (step % len)) & 1)) return;
    int mode = (int)I->v[P_ARP_MODE];
    if (mode == 6) { /* chord: every held key, shifted up an octave per step across the range */
        int octs = clampi((int)I->v[P_ARP_OCT], 1, 6);
        int shift = 12 * (a->idx % octs);
        for (int i = 0; i < a->nseq; i++)
            if (a->seq[i] + shift <= 127) play(I, a->seq[i] + shift, a->seq_vel[i]);
        a->idx++;
    } else {
        int k = a->idx % a->nseq;
        if (mode == 5) k = (int)(rng_u32(&a->rng) % (uint32_t)a->nseq);
        play(I, a->seq[k], a->seq_vel[k]);
        a->idx = (a->idx + 1) % a->nseq;
    }
    a->step_played = 1;
}

void arp_tick(crest_inst_t *I, int n) {
    arp_t *a = &I->arp;
    int on = toggled(I);
    if (!on) return;
    if (!a->rng) a->rng = 0x2545F491u;
    int sig = (int)I->v[P_ARP_MODE] | (int)I->v[P_ARP_OCT] << 4 | (int)I->v[P_ARP_SCALE] << 8 | a->set_root << 12 |
              a->set_mask << 16;
    if (sig != a->sig) { /* mode, octave range or scale changed */
        a->sig = sig;
        rebuild(I);
    }
    double bpm = (double)I->bpm;
    double len = (double)ARP_TICKS[clampi((int)I->v[P_ARP_RATE], 0, 18)] / 24.0;
    float swing = clampf(I->v[P_ARP_SWING], 0.2f, 0.8f);
    double pos;
    if (I->beat >= 0.0) {
        pos = I->beat;
        a->pos = pos;
    } else {
        pos = a->pos;
        a->pos += (double)n * bpm / (60.0 * (double)CREST_FS);
    }
    if (a->nseq == 0 && a->nsounding) release_sounding(I);
    int step = step_at(pos, len, swing);
    if (step != a->last_step) {
        release_sounding(I);
        a->last_step = step;
        trigger(I, step);
    } else if (a->nsounding) {
        double pair = 2.0 * len;
        double start = (double)(step / 2) * pair + ((step & 1) ? pair * (double)swing : 0.0);
        double slen = (step & 1) ? pair * (1.0 - (double)swing) : pair * (double)swing;
        if (pos - start >= (double)I->v[P_ARP_GATE] * slen) release_sounding(I);
    }
}
