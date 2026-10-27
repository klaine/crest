#include <math.h>
#include <string.h>

#include "crest.h"
#include "dsp_util.h"
#include "rhythms_gen.h"

/* Step length in 24 ppqn ticks, slowest first; matches ARP_RATES in tools/gen/params.py. */
static const uint8_t ARP_TICKS[19] = {192, 144, 128, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};
_Static_assert(sizeof ARP_TICKS == 19, "arp rate table");

int arp_enabled(const crest_inst_t *I) { return I->v[P_ARP_ON] >= 0.5f; }

/* Rebuild the play order from the held keys, mode and octave range. */
static void rebuild(crest_inst_t *I) {
    arp_t *a = &I->arp;
    int mode = (int)I->v[P_ARP_MODE], octs = clampi((int)I->v[P_ARP_OCT], 1, 6);
    uint8_t base[ARP_HELD], bvel[ARP_HELD];
    int nb = a->nheld;
    if (nb == 0) {
        a->nseq = 0;
        a->idx = 0;
        a->dir = 1;
        return;
    }
    /* sorted order (with velocities) unless the mode keeps the order played */
    for (int i = 0; i < nb; i++) {
        base[i] = a->held[i];
        bvel[i] = a->held_vel[i];
    }
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
    int sig = (int)I->v[P_ARP_MODE] | (int)I->v[P_ARP_OCT] << 4;
    if (sig != a->sig) { /* mode or octave range changed */
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
