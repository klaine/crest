# 05 — Mixer, Filter & Drive

## 1. Mixer
```
sum = 0.5 · ( L(mx_o1+m)·o1 + L(mx_o2+m)·o2 + L(mx_o3+m)·o3 + L(mx_noise+m)·noise + L(mx_ring+m)·ring )
mix = sat(sum)                  // headroom: linear to |0.6|, then a tanh knee towards 1.1
L(x) = clamp(x,0,1)^1.5         // audio taper (centre = −9 dB); m = matrix contribution
```
Defaults: `mx_o1 = 1.0` and all other levels 0.

**Headroom.** The Peak's mixer "can clip if all sources are turned up to maximum" (manual p.22). One oscillator
at full peaks near 0.5 and passes untouched; two at full saturate lightly (in-phase sines: H3 about −33 dB),
three clearly (about −19 dB). A dense saw at full can touch the knee on its beat peaks. The knee is plain
(no ADAA): it is smooth, and only engages on hot sums. `tests/host/test_peakmod.inc` checks both ends.

**VCA Gain** (`vca_gain`) scales the voice level after the filter, as the manual describes ("sets the output
level of the mixer … this control scales the AHDSR envelope"). It is not a drone floor: 193 of the 529 imported
patches have it at 127 and are ordinary enveloped sounds.

## 2. Pre-filter Overdrive (`pre_od`, 0..1)
- **Gain.** `g = 1 + 15·pre_od²` (0 to about +24 dB).
- **Curve.** `y = trim · tanh(g·x)`. The output trim `trim = 1/(1 + 0.6·pre_od)` makes perceived level rise gently rather than jump.
- **ADAA.** First-order. `F(x) = log(cosh(x))`, and `y = (F(x_n) − F(x_{n−1}))/(x_n − x_{n−1})`, falling back to `tanh((x_n + x_{n−1})/2)` when `|Δx| < 1e-4`.
- **Bypass.** Skipped entirely when `pre_od + matrix == 0`. Re-entering resets the ADAA state to the current input.

## 3. Filter
**Types** (`flt_type`, append-only): `LP24, LP12, BP12, BP6, HP24, HP12`. Indices are stored in presets.

**Structure.** The base is a Cytomic TPT state-variable filter (Andrew Simper, "SvfLinearTrapOptimised2"), per voice:
```
g  = tan(π fc / fs)   (LUT)          k = 2 − 2·res_eff        (k → 0 = self-oscillation)
a1 = 1/(1 + g(g + k)); a2 = g·a1; a3 = g·a2
v3 = x − ic2eq; v1 = a1·ic1eq + a2·v3; v2 = ic2eq + a2·ic1eq + a3·v3
ic1eq = 2v1 − ic1eq; ic2eq = 2v2 − ic2eq
LP = v2, BP = v1, HP = x − k·v1 − v2
```
- **12 dB modes.** One SVF stage, using the LP, BP or HP output.
- **24 dB modes.** Two cascaded SVF stages with the same `g`. Resonance is applied to stage 2 only; stage 1 uses `k = 1.414` (Butterworth). Output is LP→LP or HP→HP. This is the "2-pole + 2-pole" plan from [03 §8](03-dsp-architecture.md).
- **BP6.** One SVF BP output with `k` floored at 1.0 (wide), scaled by `k`, which gives a gentle 6 dB/oct bandpass.

**Resonance & drive in the loop (the "character").**
- `res_eff = clamp(flt_res + matrix, 0, 1)`, with a curve of `res^0.7` so the top of the range is broad.
- **Saturated feedback.** The BP feedback path is saturated: before computing `HP`, apply `v1_sat = fast_tanh(v1·(1 + 3·flt_drive)) / (1 + 3·flt_drive)·(1 + flt_drive)`. This uses the BP state, not the input, so high resonance self-limits musically and drive thickens the low end.
- **Self-oscillation.** Reached at `res_eff ≥ 0.98`, where the amplitude is limited by the saturator at about ±1.
- **Input drive.** `flt_drive` also scales the input into the filter: `x·(1 + 2·flt_drive)`, followed by an output trim.
- **Resonance compensation.** Low frequencies lose level at high resonance. Add `x·0.5·res_eff` to the LP output in LP modes, for a fuller bass at high Q.

**Cutoff.**
```
cut_semis = 20 + 115·flt_cut                       // ≈ 16 Hz … 18.8 kHz (MIDI-note space)
          + flt_key·(note − 60)                    // key tracking 0..1 (1 = 100 %)
          + flt_env·env1·96                        // ModEnv1 depth ±1 → ±96 st (cubic)
          + flt_lfo·lfo1·96                        // LFO1 depth ±1 → ±96 st = 8 octaves (cubic)
          + flt_vel·(vel/127 − 0.5)·48             // velocity depth 0..1
          + matrix[CUTOFF]·96 + drift
fc = 440·2^((cut_semis − 69)/12), clamped to [16 Hz, 0.45·fs]
```
- **Coefficient updates.** `g` is computed at sub-block rate from the LUT and ramped linearly in the `g` domain, which is stable for TPT.
- **Live readout.** `flt_cut:effective` returns the unclamped normalised value for live display.

**Stability.** TPT is unconditionally stable for any `g > 0` and `k ≥ 0`. As a precaution, clamp `k ≥ 0.005` and clamp the states to ±8 after the update.

## 4. Post-filter Distortion (`post_dist`, 0..1)
- **Curve.** Asymmetric soft clipper for a "transistor" flavour: `y = tanh(g·x + 0.15·post_dist) − tanh(0.15·post_dist)`, with `g = 1 + 24·post_dist²`.
- **Level.** Followed by an output trim of `1/(1 + post_dist)`.
- **DC blocker.** A 1-pole HP at 10 Hz after the stage.
- **ADAA and bypass.** First-order ADAA, as in §2. Bypassed at 0.

## 5. VCA
- **Gain.** `amp = AmpEnv · vel_scale · (1 + matrix[AMP_LEVEL])`, clamped to 0..2.
- **Velocity.** `vel_scale = 1 − ae_vel·(1 − vel/127)`.
- **Ramping.** Linear across the sub-block.
- **Silence detection.** A voice is freed when the amp env is idle and the output is below −100 dB for 1 sub-block.

## 6. Pan / stereo
- **Pan position.** `pan = v_spread·unison_pos + matrix[PAN]` (−1..1), with an equal-power law (`cos/sin` LUT).
- **Unison spacing.** Unison voices are spread evenly: `unison_pos ∈ [−1, 1]`.
- **Poly spread.** For non-unison poly, `v_spread` alternates voices L/R by voice index (a gentle "stereo poly" effect; 0 = mono centre).

## 7. Tests
- **Filter response.** Sweep each type with white noise. Check the slope: within ±1.5 dB/oct of nominal at 4·fc for LP/HP. Check the −3 dB point within 5 % of fc at res 0.
- **Self-oscillation.** At res 1.0 with no input, the filter self-oscillates at fc ±2 % for fc in 100 Hz…5 kHz. No NaN for any parameter combination: random fuzz test, 10⁶ samples.
- **ADAA.** Matches the naïve tanh within 0.5 dB at low frequency, with aliasing reduced by ≥ 15 dB at a 5 kHz sine input with drive 1.0.
