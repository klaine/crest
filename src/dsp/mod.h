/* Modulation sources and destinations (plan/07). The order must match MOD_SOURCES / MOD_DESTS in
 * tools/gen/params.py: indices are stored in patches, so entries are only ever appended. */
#ifndef CREST_MOD_H
#define CREST_MOD_H

#include <stdint.h>

#include "params_gen.h"

enum {
    MS_OFF = 0, MS_DIRECT, MS_WHEEL, MS_CHANAT, MS_POLYAT, MS_EXPR, MS_BREATH, MS_SUSTAIN, MS_BEND, MS_VEL,
    MS_KEY, MS_RANDOM, MS_AENV, MS_ME1, MS_ME2, MS_LFO1, MS_LFO2, MS_LFO3, MS_LFO4, MS_ANIM1, MS_ANIM2,
    MS_MAC1, /* ... */ MS_MAC8 = MS_MAC1 + 7, MS_LFO1P, MS_LFO2P, MS_LFO3P, MS_LFO4P, /* unipolar 0..1 */ MS_COUNT
};

enum {
    MD_OFF = 0, MD_PITCH,
    MD_O1_PITCH, MD_O2_PITCH, MD_O3_PITCH,
    MD_O1_SHAPE, MD_O2_SHAPE, MD_O3_SHAPE,
    MD_O1_VSYNC, MD_O2_VSYNC, MD_O3_VSYNC,
    MD_O1_DENS, MD_O2_DENS, MD_O3_DENS,
    MD_O1_LEVEL, MD_O2_LEVEL, MD_O3_LEVEL,
    MD_NOISE, MD_RING, MD_PREOD, MD_CUTOFF, MD_RES, MD_FDRIVE, MD_POSTDIST, MD_AMP, MD_PAN,
    MD_AE_A, MD_AE_D, MD_AE_R, MD_M1_A, MD_M1_D, MD_M1_R, MD_M2_A, MD_M2_D, MD_M2_R,
    MD_LFO1_RATE, MD_LFO2_RATE, MD_LFO3_RATE, MD_LFO4_RATE, MD_GLIDE, MD_UDET,
    MD_FX_DIST, MD_FX_CHO, MD_FX_DLY, MD_FX_REV, MD_DLY_FB,
    MD_FM12, MD_FM23, MD_FM31, MD_FMN1, MD_O3_FLT, MD_NS_FLT,
    MD_DLY_TIME, MD_REV_DECAY, MD_CHO_RATE, MD_CHO_DEPTH, MD_CHO_FB, MD_REV_DAMP, MD_REV_HP, MD_COUNT
};

_Static_assert(MS_COUNT == CREST_MOD_SRC_COUNT, "modulation source list is out of sync with params.py");
_Static_assert(MD_COUNT == CREST_MOD_DST_COUNT, "modulation destination list is out of sync with params.py");

#define CREST_MM_SLOTS 20

typedef struct {
    uint8_t s1, s2, dst;
    float dep;
} mm_slot_t;

#endif
