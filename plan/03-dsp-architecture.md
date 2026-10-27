# 03 — DSP Architecture

## 1. Signal flow
```
                   ┌───────────────────────── per voice (×8) ─────────────────────────────┐
 Osc1 ─┐           │                                                                       │
 Osc2 ─┼─► Mixer ──┼─► Pre-OD ─► Filter (SVF, drive in loop) ─► Post-Dist ─► VCA ─► Pan ──┼─► Σ voice bus (stereo)
 Osc3 ─┤   +Noise  │   (ADAA)      LP/BP/HP 6/12/24 dB          (ADAA)      AmpEnv       │
 Ring ─┘   +Ring   └───────────────────────────────────────────────────────────────────────┘
                                                                                        │
 Σ voice bus ─► Distortion ─► Chorus ─► Delay ─► Reverb ─► Patch Volume ─► Soft clip ─► int16
```
- **Voice path is mono up to the VCA.** Unison and spread place each voice in stereo with an equal-power pan.
- **FX are global** and stereo, processed per 128-frame block.

## 2. Rates and block structure
| Rate | Value | Used for |
|---|---|---|
| Audio | 44 100 Hz | Oscillators, filter, drive, VCA, FX |
| Control (sub-block) | every 16 samples = 2 756 Hz | Envelopes, LFOs, matrix, filter coeffs, pitch |
| Block | 128 samples = 344.5 Hz | Param smoothing targets, arp clock, global LFOs (rendered at control rate) |

Render loop (in `crest.c`):
```c
void render_block(inst, out, frames) {           // frames == 128 (assert ≤ 256)
    ftz_enter(&saved);                            // FPCR bit 24 on aarch64, MXCSR on x86, no-op on wasm
    params_apply_dirty(inst);                     // targets → smoothers, recompute derived values
    for (sb = 0; sb < frames; sb += CREST_SUB) {  // CREST_SUB = 16
        events_dispatch(inst, sb);                // note on/off queued by on_midi (sub-block accurate)
        arp_tick(inst, CREST_SUB);                // may generate notes
        global_mod_tick(inst);                    // LFO3/4, macros, animate, wheel, AT, bend
        for each active voice v:
            voice_control_tick(v);                // envs, LFO1/2, matrix → per-voice targets
            voice_render(v, bus + 2*sb, CREST_SUB);  // audio-rate, linear ramps of control values
    }
    fx_process(inst, bus, frames);                // dist → chorus → delay → reverb
    master_out(inst, bus, out, frames);           // volume, soft clip, NaN guard, float→int16
    ftz_exit(saved);
}
```
- **Note timing.** MIDI events have no timestamps, so all events received before `render_block` apply in the first sub-block. Arp-generated events land on sub-block boundaries, which is 0.36 ms resolution.
- **Control ramps.** Control values computed at sub-block rate are linearly interpolated across the 16 samples: pitch increment, cutoff coefficient (`g`), amp gain, pan, shape, drive. Filter `g` is ramped in the `g` domain (as a precomputed per-sample delta), never by calling `tan()` per sample.

## 3. Data layout
```c
#define CREST_VOICES      8
#define CREST_OSCS        3
#define CREST_SUB         16
#define CREST_MAX_INSTANCES 4

typedef struct { float v, target, step; } smooth_t;       // linear-ramp smoother

typedef struct crest_voice {
    uint8_t  active, gate, note, vel, rvel;   uint32_t age;  int unison_idx;
    float    pitch_cur, pitch_tgt, glide_coef, drift_state[2];
    osc_t    osc[CREST_OSCS];  noise_t noise;
    svf_t    flt;              adaa_t pre_od, post_dist;
    env_t    env[3];           lfo_t lfo[2];
    float    mod_val[MOD_SRC_COUNT];        // per-voice source values (this sub-block)
    float    dst_acc[MOD_DST_COUNT];        // summed modulation per destination
    float    polyat, rnd_note;  float pan_l, pan_r;
    ramp_t   r_gain, r_cut, r_pan, r_shape[3], r_inc[3];
} crest_voice_t;

typedef struct crest_inst {
    _Atomic int in_use;
    patch_t  patch;               // base (unmodulated) values: generated struct from params.py
    smooth_t sm[PARAM_SMOOTH_COUNT]; uint64_t dirty[PARAM_WORDS];
    crest_voice_t voice[CREST_VOICES];
    lfo_t    glfo[2];             // LFO 3/4
    mod_global_t gmod;            // wheel, AT, bend, animate, macros, expression, breath, sustain
    arp_t    arp;  notestack_t held;
    fx_t     fx;                  // includes static delay/reverb/chorus buffers
    float    bus[2 * 256];
    uint32_t rng;  int preset_idx;  char name[24];
} crest_inst_t;

static crest_inst_t g_pool[CREST_MAX_INSTANCES];         // in .bss; no malloc
```
Size estimate per instance:
| Item | Size |
|---|---|
| Delay | 2 × 2.0 s × 44100 × float ≈ 706 KB |
| Reverb FDN | 8 lines, ≤ 8192 samples each ≈ 262 KB |
| Chorus | 2 × 2048 floats ≈ 16 KB |
| Voices | ≈ 20 KB |
| **Total per instance** | **≈ 1.0 MB** |
| **Pool (×4 instances, `.bss`)** | **≈ 4 MB** |

`.bss` is zero-initialised and costs nothing in the `.so` file. **VERIFY** that 4 instances is enough (one per track). Amino uses 4.

## 4. Parameter smoothing & dirty handling
- **`set_param`.** Writes `patch.<field>` and sets `dirty[bit]`; it does nothing else.
- **`params_apply_dirty`**, once per block:
  - Continuous params: set the smoother target to the new value. Ramp length is 1 block (2.9 ms) for most params and 4 blocks for `flt_cut` and FX times.
  - Discrete params (enums, wave select): applied immediately. Wave changes crossfade over 1 sub-block to avoid clicks (osc `xfade` counter).
  - Derived values are recomputed, e.g. LFO increments from rate/sync and envelope coefficients from times.
- **Avoiding wasted work.** Scenes call `set_param` every block with unchanged values, so a write whose value equals the current base value does not set the dirty bit.

## 5. Gain staging
- Each oscillator outputs ±1. Mixer levels are 0..1 with a level curve `x²`.
- The mixer sum is scaled by `1/2` before Pre-OD, so three full oscillators give a ±1.5 peak. That drives the OD gently even at minimum OD setting, which is intended: it is "hot".
- **Fixed per-voice gain `0.30`.** Never normalise by the active voice count; SchwungFreak learned that makes held notes duck.
- **Master stage.** `patch_vol` (0..1, default 0.7, curve `x²`, then ×2), then a soft clipper `y = x / (1 + |x|)`, blended to linear below 0.5 (a cubic knee), then ×32767 with saturation to int16.
- **NaN guard.** At the end of each block, if any bus sample is not finite: zero the bus, reset all filter, ADAA and FX states, and increment `inst->nan_resets`, which test builds expose. Only one check per block is needed, since the sum of `fabsf` would be NaN if any sample were.

## 6. Shared DSP utilities (`dsp_util.h`)
| Function | Implementation |
|---|---|
| `fast_tanh(x)` | Padé 7/6 clamped at ±3 (error < 1e-4) |
| `fast_exp2(x)` | Split int/frac, 5th-order polynomial; used for pitch → increment |
| `pitch_to_inc(semis)` | `440/44100 · exp2((semis − 69)/12)` |
| `cut_to_g(hz)` | `tanf(π·hz/fs)` through a 1024-entry LUT over log-frequency (20 Hz–20 kHz) with linear interpolation; clamps at 0.49·fs |
| `blep(t, dt, inv_dt)` | polyBLEP step residual ([04 §3.1](04-oscillators.md)): 2-point by default, 4-point when built with `CREST_BLEP_POINTS=4`. `blamp` (2-point) for triangle corners |
| RNG | xorshift32 per instance; `rand_bi()`, `rand_uni()` |
| `ftz_enter`/`ftz_exit` | `__asm__("mrs %0, fpcr")`, bit 24 on aarch64; empty on wasm |

Prefer floats everywhere (`-Wdouble-promotion` enforces this). Write hot loops so clang can auto-vectorise them. Explicit NEON is allowed later behind `#if defined(__ARM_NEON)` with a scalar fallback.

## 7. Anti-aliasing strategy
| Stage | Method |
|---|---|
| Saw / Pulse / sync edges | 2-point polyBLEP (step) by default; the 4-point option removes 3–9 dB more alias energy at the price of high-frequency droop ([04 §3.1](04-oscillators.md)) |
| Triangle corners, sine fold | polyBLAMP (slope) where cheap; fold uses a band-limited table approach ([04 §3](04-oscillators.md)) |
| Wavetables | Per-octave mip-maps (11 levels), each band-limited to Nyquist at that octave's top note |
| Pre-OD, Post-Dist, FX distortion | 1st-order ADAA on `tanh` / hard clip / fold (antiderivative anti-aliasing) |
| Filter feedback saturation | Implicit at 1× using the TPT structure; resonance saturation is soft enough at 44.1 kHz |
| Ring mod | Not oversampled; a product of band-limited oscillators can alias, but this matches the hardware character |

Fallback (M2, if listening tests reveal aliasing): render the drive+filter section at 2× with a 31-tap halfband up/down (shared polyphase coefficients). This costs about 60 µs extra for 8 voices, and the budget below includes headroom for it.

## 8. CPU budget
The block period is 2 902 µs, and the device slack is about 2 370 µs, shared with other modules. The target for Crest's worst case (8 voices, 3 saw osc with density 1.0, all drives on, 24 dB filter, matrix 16 slots in use, all 4 FX on) is **≤ 900 µs**. The typical patch target is **≤ 500 µs**.

| Stage | Budget (worst) | Notes |
|---|---|---|
| Oscillators (8 v × 3 osc, density up to 4 saws) | 320 µs | about 0.9 ns/sample/saw-unit; density is the dominant cost |
| Mixer + noise + ring | 30 µs | |
| Pre-OD + post-dist ADAA | 50 µs | Skipped when amount = 0 |
| Filter (24 dB = 2 SVF) | 120 µs | |
| VCA + pan | 15 µs | |
| Control (envs, LFOs, matrix, coeffs) | 60 µs | 8 sub-blocks × 8 voices × 16 slots |
| FX (dist + chorus + delay + reverb) | 250 µs | Reverb about 150 µs |
| Master | 10 µs | |
| **Total** | **≈ 855 µs** | |

Optimisation levers, in priority order:
1. Skip silent voices: amp env idle and `|out| < 1e-5` for 1 sub-block marks the voice inactive.
2. Skip unused oscillators when their level is 0 and they are not the ring source.
3. Lower density cost by using 2 extra saws instead of 3 at `density < 0.34`, and so on.
4. Skip disabled FX entirely (level = 0 → bypass with a 1-block tail fade).
5. Reverb "Eco" quality (4 lines). Halve the filter cost by running 24 dB modes as 2-pole + 2-pole with shared coefficients (already the plan).

Measure on the host with `scripts/build.sh --bench` (per-stage timers in `bench_main.c`). Measure on the device with `--profile` (`cpu_us`). See [14](14-testing.md).
