/* Crest: shared types. One instance = one patch (plan/03 section 3). */
#ifndef CREST_H
#define CREST_H

#include <stdatomic.h>
#include <stdint.h>

#include "arp.h"
#include "env.h"
#include "filter.h"
#include "fx.h"
#include "lfo.h"
#include "mod.h"
#include "osc.h"
#include "params_gen.h"
#include "plugin_api.h"

#define CREST_VOICES 8
#define CREST_MAX_FRAMES 256
#define CREST_MAX_INSTANCES 4
#define CREST_STACK 16

#ifdef _WIN32
#define CREST_EXPORT __declspec(dllexport)
#else
#define CREST_EXPORT __attribute__((visibility("default")))
#endif

enum { VM_POLY = 0, VM_POLY2 = 1, VM_MONO = 2, VM_MONOLG = 3, VM_MONO2 = 4 };

/* Values derived from the panel once per block (params_prepare). */
typedef struct {
    struct {
        int wave, table, shsrc, fixed;
        float shape, shamt, dens, ddet, vsync, semis, penv, plfo, level;
    } osc[3];
    float lvl_noise, lvl_ring, vca;
    float fm[4];     /* O1>O2, O2>O3, O3>O1, Noise>O1 */
    float flt_o3, flt_ns, flt_div, v_div;
    int nz_mode; /* 0 white, 1 low-passed, 2 high-passed */
    float nz_a, nz_boost;
    float pre_od, post_dist;
    int ftype;
    float cut, res, drive, key, env, lfo, vel, ae;
    env_par_t env_p[3];
    float env_vel[3]; /* -1..1: positive = louder when played harder, negative = the inverse */
    lfo_par_t lfo_p[4];
    int vmode, gmode, oct, osc_reset, unison;
    float glide_x, bend_range, udet, spread, drift, master;
    mm_slot_t slot[CREST_MM_SLOTS];
    int nslots;
    struct {
        uint8_t dst;
        float amt;
    } mac[8][4];
} bparams_t;

typedef struct {
    uint8_t active;     /* sounding, including release and the steal fade */
    uint8_t gate;       /* key held */
    uint8_t sustained;  /* key released while the pedal is down */
    uint8_t fading;     /* stolen: fading out */
    uint8_t note, vel;
    uint8_t uidx, usize; /* position in its unison group */
    uint8_t pend, pend_note, pend_vel, pend_uidx, pend_usize;
    uint8_t fresh;      /* ramps start at the new targets */
    float pend_glide;
    uint32_t age, group, pend_group;
    float pitch;        /* glided pitch, MIDI semitones including the octave shift */
    float fade;         /* steal fade gain 1 -> 0 */
    float amp_out;      /* amp envelope level (steal priority) */
    float upos;         /* -1..1 position in the unison spread */
    float polyat, rnd;
    osc_t osc[3];
    oscp_t p_osc[3];
    float p_dt[3], p_lvl[5], p_g, p_amp, p_gl, p_gr, p_pod, p_pdist;
    filt_t flt;
    env_t env[3];
    lfo_t lfo[2];
    float src[MS_COUNT], acc[MD_COUNT];
    float drift[4];
    float dvg[3], fdvg;  /* divergence: static random offsets per osc and for the filter */
    float p_fmc[4];      /* FM indices of the previous sub-block (ramped) */
    float o3_prev[CREST_SUB]; /* O3's previous block: the O3>O1 modulator */
    float od_xp, pd_xp, pd_dc;
    uint32_t rng;
    float nz_lp;
} voice_t;

typedef struct crest_inst {
    _Atomic int in_use;
    uint8_t ever_used; /* survives create_instance's reset */
    char err[64];
    /* ---- reset by create_instance ---- */
    float v[P_COUNT];
    char name[24];
    int preset_idx;
    int slot;           /* index in the instance pool (the Peak bank slot, peak_bank.c) */
    int syx_cur;        /* patch of the loaded Peak bank in use, -1 for none */
    int syx_vol_set;    /* its solved loudness has been applied */
    int syx_pend;       /* the Peak Patch knob's patch is waiting for its bank to finish loading */
    int offline;        /* rendering for the loudness solve: ignore the host's tempo and transport */
    bparams_t bp;
    voice_t voice[CREST_VOICES];
    uint32_t age_ctr, group_ctr;
    uint8_t stack[CREST_STACK];
    int nstack;
    int sustain_down;
    float last_pitch;
    int last_valid;
    int last_vmode;
    int last_unison; /* the mono modes lay their voices out by unison count (voice.c mono_U) */
    int pdirty; /* a parameter changed since params_prepare: refresh before starting a note */
    int m2cur; /* Mono2: which voice group is sounding */
    float bend; /* -1..1 */
    float modwheel, chan_at, expression, breath;
    int anim_cc[2], anim_latch[2];
    float s_wheel, s_chanat, s_expr, s_breath, s_sustain, s_bend, s_anim[2], s_mac[8];
    float gsrc[MS_COUNT], gacc[MD_COUNT];
    lfo_t glfo[2];
    lfo_shared_t cshare[2]; /* LFO 1/2 in Common mode: one phase and one set of random values */
    uint32_t crng;
    int lfo_restart;
    double beat;     /* host beat position at the current sub-block, < 0 when unavailable */
    float bpm;
    int nan_resets;
#ifdef CREST_PROFILE
    float cpu_us; /* smoothed render time, debug builds only */
#endif
    float busl[CREST_MAX_FRAMES], busr[CREST_MAX_FRAMES];
    arp_t arp;
    fx_t fx; /* last: ends with the big arena */
} crest_inst_t;

/* crest.c: the symbol the host looks up. */
CREST_EXPORT plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

/* mod.c */
void mod_eval(const bparams_t *bp, const float *src, float *acc);

/* voice.c */
void voice_note_on(crest_inst_t *I, int note, int vel);
void voice_note_off(crest_inst_t *I, int note);
void voice_sustain(crest_inst_t *I, int down);
void voice_all_off(crest_inst_t *I, int hard);
void voice_render(crest_inst_t *I, voice_t *v, float *outl, float *outr, int n);

/* params.c */
void params_defaults(crest_inst_t *I);
void params_prepare(crest_inst_t *I);
void params_set(crest_inst_t *I, const char *key, const char *val);
int params_get(crest_inst_t *I, const char *key, char *buf, int buf_len);
void preset_load(crest_inst_t *I, int idx);
/* Load the Peak Patch knob's patch from the loaded bank. Returns 1 when done or moot (the bank has
 * finished loading), 0 while the bank is still loading. */
int syx_apply(crest_inst_t *I);
/* Load a full set of values (a converted Peak patch, say) as if it were a preset. */
void preset_load_values(crest_inst_t *I, const float *v, const char *name);

#endif
