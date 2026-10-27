# 04 — Oscillators

Each voice has three identical oscillators (`osc.c`), plus a noise source and a ring modulator.

## 1. Oscillator state
```c
typedef struct {
    uint32_t ph;                 // master phase, 2^32 = one cycle
    uint32_t sph;                // slave (virtual-sync) phase
    uint32_t dph[3];             // density copies' phases (extra saws)
    float    blep_prev;          // polyBLEP carry for the previous sample
    uint8_t  wave, table, mip;   // current selection
    uint8_t  xfade;              // samples left of a wave-change crossfade
    float    last;               // last output (ring mod, xfade)
} osc_t;
```
Phase is a `uint32_t` accumulator, so wrapping is exact. Fractional phase is `t = ph * 0x1p-32f`.

## 2. Pitch
```
semis = note + 12·v_oct + oN_coarse + oN_fine/100
      + bend·v_bend
      + oN_penv³ · ModEnv2 · 96 st (8 octaves, as on the Peak; the cube gives fine control)
      + oN_plfo · LFO2   · 60 st    (depth³; the Peak's ±5 octaves, fine near zero)
      + matrix[OSCn_PITCH]·48 + matrix[ALL_PITCH]·48
      + unison detune offset + drift
f0 = 440 · 2^((semis − 69)/12), clamped to [8 Hz, 18 kHz]
inc = f0 / fs · 2^32
```
- **When pitch is computed.** Once per sub-block, with a linear ramp of `inc` across the sub-block.
- **Glide.** Applied to `note` before this formula ([08 §4](08-voice-management.md)).
- **Phase reset (`osc_reset`).** `Free` (default; phases continue between notes, with a random start for each new voice) or `Reset`: all phases reset to 0 on note-on, for punchy basses.

## 3. Waveforms and the Shape control
`oN_wave`: `Sine, Triangle, Saw, Pulse, Wavetable` (append-only enum).

Effective shape for each sub-block:
```
src   = {Manual: 0, ModEnv1: env1, LFO1: lfo1}[oN_shsrc]
shape = clamp(oN_shape + oN_shamt·src + matrix[OSCn_SHAPE], 0, 1)
```

| Wave | Shape 0 → 1 | Band-limiting |
|---|---|---|
| Sine | Self-phase-modulated sine: `y = sin(2πt + k·sin(2πt))`, `k = 2.2·shape²`. Adds harmonics smoothly, from pure sine towards a bright, saw-like tone. | Sine LUT (4096, linear interp). Harmonics are limited; alias risk only above about 5 kHz f0 at shape 1, where k is attenuated by `min(1, 4000/f0)`. |
| Triangle | Variable-slope triangle: rising portion `w = 0.5 − 0.49·shape`, kept within `[2·dt, 1 − 2·dt]` so each ramp lasts at least two samples (shorter, the two corner corrections overlap and spiked up to 5.5× full scale). At shape 1 it becomes a near-saw ramp. | polyBLAMP at both corners, with slope-change heights `1/w` and `1/(1−w)` |
| Saw | "Double saw": `y = 0.5·(saw(t) + saw(t + 0.5·shape))`. Shape 1 gives the octave-up "hollow" saw. | polyBLEP at both discontinuities (§3.1; 2-point default) |
| Pulse | Pulse width `pw = 0.5 − 0.47·shape` (50 % → 3 %). DC removed by subtracting `(2pw − 1)`. | polyBLEP at both edges (§3.1; 2-point default) |
| Wavetable | Frame position across the 16 frames of `oN_table`, linearly interpolated between adjacent frames | Mip-mapped tables (§5) |

Wave-change crossfade: when `oN_wave` or `oN_table` changes while the voice sounds, fade the old output out and the new one in over 32 samples.

### 3.1 Edge correction: polyBLEP (2-point default, 4-point option)
Saw, Pulse and virtual-sync edges are corrected with a polynomial step residual `r(x)`, where `x = (t_sample − t_edge)/dt` is the sample's distance from the edge in samples (negative before the edge).

**2-point (default).** The integral of a linear B-spline kernel minus the ideal step: `r(x) = (1 + x)²/2` for −1 ≤ x < 0, `r(x) = −(1 − x)²/2` for 0 ≤ x < 1, and 0 elsewhere.

**4-point (option, `CREST_BLEP_POINTS=4`).** The integral of a cubic B-spline kernel minus the ideal step:

| x | r(x) |
|---|---|
| −2 ≤ x < −1 | (2 + x)⁴ / 24 |
| −1 ≤ x < 0 | ½ + 2x/3 − x³/3 − x⁴/8 |
| 0 ≤ x < 1 | −½ + 2x/3 − x³/3 + x⁴/8 |
| 1 ≤ x < 2 | −(2 − x)⁴ / 24 |

Corrected output is `naive + jump · r(x)`, where `jump` is the value after the edge minus the value before it (−2 at a saw wrap). `r` is continuous at ±1 and ±2, and has a jump of exactly 1 at 0.

```c
// t: phase of this sample in [0,1); dt: phase increment; inv_dt = 1/dt, computed once per sub-block
static inline float blep4(float t, float dt, float inv_dt) {
    if (t < 2.f * dt) {                           // up to 2 samples after the wrap
        float x = t * inv_dt;
        if (x < 1.f) return x * (2.f/3.f + x*x * (-1.f/3.f + x * 0.125f)) - 0.5f;
        float u = 2.f - x;  return -(u*u*u*u) * (1.f/24.f);
    }
    if (t > 1.f - 2.f * dt) {                     // up to 2 samples before the wrap
        float x = (t - 1.f) * inv_dt;
        if (x > -1.f) return x * (2.f/3.f + x*x * (-1.f/3.f - x * 0.125f)) + 0.5f;
        float u = 2.f + x;  return (u*u*u*u) * (1.f/24.f);
    }
    return 0.f;
}
// saw = (2t − 1) − 2·blep4(t, dt, inv_dt)
```

Rules:
- **Range.** The two windows are disjoint while `dt < 0.25`, i.e. f0 < fs/4 (11.0 kHz). Between 8.8 kHz and 11 kHz, crossfade the classic waves to a sine; a band-limited saw or pulse above that holds only the fundamental. Pulse edges closer than 4 samples are corrected independently and summed.
- **Cost.** `inv_dt` is ramped with `inc` per sub-block, so there is no per-sample divide. The window test is the only per-sample work away from an edge.
- **BLAMP.** Triangle corners stay 2-point; their harmonics fall as 1/n², so extra order buys little.
- **Build switch.** `CREST_BLEP_POINTS` (2 or 4) selects the kernel at compile time. The default is **2**, chosen by the project owner after the measurements below showed that 4-point buys only 3–9 dB at the cost of high-frequency droop. The M2 A/B can revisit this.

**Measured** (double precision, linear chain, saw; reference code in `plan/reference/`). Alias figure = energy more than 3 bins from any harmonic of any sounding note, relative to total energy:

| Case | naive | 2-pt | 4-pt |
|---|---|---|---|
| single note 262 Hz | −21.5 | −37.1 | −44.3 |
| single note 1.05 kHz | −15.5 | −31.7 | −40.9 |
| single note 2.09 kHz | −12.4 | −27.8 | −37.3 |
| single note 4.19 kHz | −9.6 | −25.8 | −36.2 |
| single note 8.0 kHz | −6.2 | −18.4 | −26.8 |
| 5-note chord, C3–D5 | −21.0 | −36.0 | −41.4 |
| 8-note chord, C3–E5 | −21.8 | −36.4 | −40.8 |
| 4-note chord, C6–C7 | −13.7 | −29.7 | −38.2 |

Level of upper harmonics against the ideal `1/h` spectrum, f0 = 1000.3 Hz:

| Harmonic | 5 kHz | 8 kHz | 10 kHz | 15 kHz | 18 kHz |
|---|---|---|---|---|---|
| 2-pt | −0.5 dB | −0.6 | −1.9 | −3.1 | −5.1 |
| 4-pt | −0.9 dB | −1.5 | −3.4 | −6.5 | −10.2 |

What this means:
- **Alias reduction is modest.** Going from 2-point to 4-point lowers total alias energy by about 3–9 dB, not an order of magnitude.
- **It costs brightness.** The kernel acts as a `sinc⁴` low-pass on the saw's harmonics (2-point is `sinc²`). The droop matches theory within 0.5 dB.
- **Chords do not change the picture.** Independent oscillators add linearly, so each note's aliases stay at that note's own floor. Chords measure within 1–3 dB of single notes, and the same holds for unison and density stacks.
- **A mip-mapped table is the alternative.** A table saw (§5 levels, linear read) measured −39 to −53 dB with flat harmonics and no droop: better than 4-point above about 2 kHz, about equal below. It costs more per copy, so it is an M2 A/B candidate for plain Saw and Pulse, not the default.

## 4. Density (stacked copies)
`oN_dens` (0..1) and `oN_ddet` (0..1). These apply to Triangle, Saw and Pulse. Sine and Wavetable ignore them to save CPU (show "--" on the page when inactive).
- **Copies.** Up to 3 extra copies of the current waveform, running on phases `dph[0..2]`.
- **Detune.** The copies are offset by `{−1, +1, −0.5}·ddet·35 cents` (spread is cubic in `ddet`).
- **Levels.** Copy *i* fades in over the density range `[i/3, (i+1)/3]`, so density 1.0 gives all 3 copies at full level.
- **Normalisation.** Output is `(main + Σ level_i·copy_i) / sqrt(1 + Σ level_i²)`, which keeps loudness roughly constant.
- **CPU saving.** A copy with level < 0.001 is not computed. Copies use the same polyBLEP code as the main wave.
- **Phases.** Copies get random start phases at voice start, which gives a natural chorus.

## 5. Wavetables
- **17 factory tables × 16 frames**, generated offline by `tools/wavegen/wavegen.py` into `src/dsp/wavetables.c` as `const int16_t`.
- **Content.** Original designs from additive and spectral morphing. Working names: `Glass, Reed, Vowel, Digital, Bell, Organ, PWM Sweep, Sync Sweep, Formant, Metal, Additive, Sub Bass, Harsh, Pluck, Choir, Grit, Morph`.
- **Mip-map levels.** 10 levels. Level *k* holds up to `H_k = 512 >> k` harmonics (minimum 1) and has length `L_k = max(4·H_k, 64)`. The lengths are 2048, 1024, 512, 256, 128, 64, 64, 64, 64, 64, about 4.3 k samples per frame.
- **Size.** 17 × 16 × 4.3 k × 2 B ≈ **2.3 MB** of `.rodata`. Fallback: 8 frames → 1.2 MB if the `.so` size becomes a problem. **VERIFY** whether the Schwung loader has a size limit.
- **Level selection.** Per sub-block, `k = clamp(ceil(log2(f0_max / 43.07 Hz)), 0, 9)`, where `f0_max` is the highest pitch in the ramp. Level *k* is alias-free up to 22.05 kHz. No crossfade between levels; revisit if switching is audible during pitch sweeps.
- **Reading.** Read position `t·L_k`. Interpolate linearly between samples, and linearly between frames `floor(pos·15)` and the next.
- **Virtual sync.** Uses the slave phase as the read phase (§6). Accept the slight aliasing at the reset; the polyBLEP correction uses the jump height `y(sph_before) − y(0)`.
- **Table names.** Shown via `oN_table` enum options, with `short_options` of 4 characters.

## 6. Virtual Sync
`oN_vsync` (0..1) sets the slave ratio `r = 2^(vsync·4)`, from 1 to 16 (four octaves above). It is modulatable via `matrix[OSCn_VSYNC]`.
- **Phases.** The master phase `ph` runs at f0. The slave phase `sph` runs at r·f0 and resets to its fractional overshoot when `ph` wraps.
- **Discontinuity.** The waveform is evaluated on `sph`. At each reset, apply a polyBLEP with the height of the output jump.
- **No sync at 0.** At `vsync = 0` the slave equals the master, so no resets happen and the oscillator behaves normally.
- **Shape and density.** Shape applies to the slave waveform. Density copies are synced to their own masters.

## 7. Noise
- **Source.** White noise from xorshift32, in `[-1, 1)`.
- **Colour.** `noise_col` (−1..1) is a 1-pole tilt with 0 = white. Negative values low-pass, with the cutoff falling from 20 kHz to 200 Hz (−0.5 sounds close to pink). Positive values high-pass, with the cutoff rising from 20 Hz to 8 kHz.
- **No separate type.** There is no `noise_type` key, which saves a knob on the Mixer page.
- **Generation.** Noise is generated per voice, so stereo unison voices decorrelate.

## 8. Ring modulator
`ring = osc1.out × osc2.out`, taken before the mixer levels and after shape and density. It goes into the mixer at `mx_ring`.

When `mx_o1` or `mx_o2` is 0, those oscillators must still be computed if `mx_ring > 0` (see [03 §8](03-dsp-architecture.md), lever 2).

## 9. Analog drift
`v_drift` (0..1) adds a per-voice, per-oscillator slow random walk:
- **Source.** White noise is low-passed at 0.3 Hz and scaled to `±v_drift·10 cents` on pitch.
- **Cutoff.** A separate walk modulates cutoff by `±v_drift·0.3 semitones`.
- **Character.** The walk is independent per voice and osc, which gives the "alive" chorus of analog polys.

## 10. Implementation notes
- **One function per wave.** Each wave has its own render function, `osc_render_<wave>(osc, out, n, inc0, dinc, shape0, dshape)`, so the inner loops stay branch-free. The density and vsync variants are separate functions selected per sub-block.
- **Output.** Oscillators write to a per-voice `float tmp[3][16]` scratch buffer. The mixer then combines the buffers ([05](05-mixer-filter-drive.md)).
- **Unit tests** ([14](14-testing.md)):
  - DC offset < 1e-3 for every wave and shape.
  - Peak within ±1.05.
  - Alias floor, single saw note, 32768-point Hann FFT, off-harmonic energy (more than 3 bins from a harmonic) relative to total: for the 4-point kernel ≤ −40 dB at 523 Hz, −38 at 1.05 kHz, −34 at 2.1 kHz, −33 at 4.2 kHz and −24 at 8 kHz; for the default 2-point kernel ≤ −31, −28, −24, −22 and −15 dB at the same pitches. Pulse is recorded on its first run and then guarded against regression.
  - Droop: the 10 kHz and 15 kHz harmonics sit within ±1 dB of the kernel's theoretical `sinc^N` response (4-point: −3.0 and −6.9 dB).
  - Chord: a 5-note chord's alias floor is within 3 dB of the single-note floors (superposition check).
  - Chord through a drive stage: render the chain at 4× with decimation as the reference. This is a regression and listening check only. Intermodulation and the kernel droop both add error, so absolute dB figures against the reference are not a pass/fail limit.
  - Correct frequency: zero-crossing count within 0.1 %.
