# 06 — Envelopes & LFOs

## 1. Envelopes (`env.c`)
There are three per voice:

| Env | Prefix | Hard-wired use | Matrix source |
|---|---|---|---|
| Amp Env | `ae_` | VCA | `AmpEnv` |
| Mod Env 1 | `m1_` | Filter cutoff (`flt_env`), osc shape (`oN_shsrc = ModEnv1`) | `ModEnv1` |
| Mod Env 2 | `m2_` | Osc pitch (`oN_penv`) | `ModEnv2` |

### 1.1 Parameters (per env)
| Key suffix | Range | Mapping |
|---|---|---|
| `_dly` | 0..1 | Delay before attack: `0` → off, else `1 ms·10000^x` (up to 10 s) |
| `_a` | 0..1 | Attack time `0.5 ms·(tmax/0.5 ms)^x`, tmax 20 s (the Peak: "over 18 s") |
| `_hold` | 0..1 | Hold at the peak after the attack, linear 0 … 500 ms (the Peak's HoldTime, 127 = 500 ms) |
| `_d` | 0..1 | Decay time, same curve with tmax 22 s (the Peak: "approx. 22 s") |
| `_s` | 0..1 | Sustain level (linear) |
| `_r` | 0..1 | Release time, same curve with tmax 25 s (the Peak: "over 24 s"), but bent by `x^0.6` so the lower half of the knob gives longer tails (`env_release_time`); the ends are unchanged |
| `_vel` | −1..1 | Velocity: `> 0` gives `level = 1 − v·(1 − vel/127)`; `< 0` gives `level = 1 + v·vel/127` (harder = softer, as the Peak's negative values) |
| `_rep` | int 0..127 | Repeats of the A→H→D cycle before sustain; 127 loops until note-off (the Peak's Off, 1–126, On) |
| `_trig` | enum `Multi, Single, Reset` | See below. Defaults as the Peak: Amp `Single` (Legato), Mod `Multi` (Re-Trig) |

### 1.2 Stage behaviour
- **Shape model.** Analog-style one-pole segments, evaluated at control rate (2 756 Hz) and ramped linearly per sample by the consumer.
  - **Attack.** Approach target `3.0` with coefficient `c = exp(−0.4055/(t_a·fs_ctl))` (`ln(3/2)`, so 0 → 1.0 takes `t_a`). Switch to Hold (or Decay when hold is 0) when `y ≥ 1.0`, giving the convex "capacitor" attack.
  - **Hold.** Stay at 1.0 for the hold time.
  - **Decay.** Approach `S − 0.01` (clamped at S), with the time constant chosen so the curve is within 1 % of S after `t_d`.
  - **Release.** Approach `−0.01` from the current level, and go idle at `y ≤ 0`.
- **Repeat.** When `_rep > 0` and the gate is still on, the end of Decay restarts Attack (then Hold) from the current level. The count decrements each cycle unless it is ∞. After the repeats, the envelope sits at Sustain. Note-off during repeats goes straight to Release.
- **Trigger modes.**
  - `Multi`: every note-on retriggers Attack from the **current level**, which is click-free.
  - `Single`: in Mono and MonoLG, a note-on while another note is held does not retrigger (legato). In Poly modes and Mono2 it behaves like Multi.
  - MonoLG differs from Mono only in glide (manual p.29). It used to force every envelope to legato; it now follows each envelope's `_trig` like Mono.
  - `Reset`: every note-on restarts from 0, which is punchy and may click when attack is slow.
- **Delay stage.** Holds the envelope at 0 (or at the current level on retrigger) for `_dly`, then runs Attack.
- **Matrix modulation of times.** Matrix destinations `AmpEnv A/D/R` and `ModEnv1/2 A/D/R` add to the normalised time value before mapping.

### 1.3 Output
`out = level · y`, in 0..1, where `level` comes from velocity (Amp env velocity is applied at the VCA instead; see [05 §5](05-mixer-filter-drive.md)).

`env_t` holds `{stage, reps, y, rel_ln, dly_left, hold_left}`, about 24 bytes.

## 2. LFOs (`lfo.c`)
| LFO | Scope | Hard-wired use | Prefix |
|---|---|---|---|
| LFO 1 | per voice | Filter (`flt_lfo`), shape (`oN_shsrc = LFO1`) | `l1_` |
| LFO 2 | per voice | Pitch (`oN_plfo`) | `l2_` |
| LFO 3 | global | matrix only | `l3_` |
| LFO 4 | global | matrix only | `l4_` |

### 2.1 Parameters (per LFO)
| Key suffix | Range | Mapping |
|---|---|---|
| `_wave` | enum `Sine, Tri, SawUp, SawDn, Square, S&H, Rnd, Exp` | Append-only |
| `_range` | enum `Low, High` | LFO 1/2 only (the Peak's Range button). LFO 3/4 use one wide range |
| `_rate` | 0..1 | Free rate. Low `0.02 Hz·10000^x` (0.02 … 200 Hz), High 8× that (0.16 … 1600 Hz), LFO 3/4 `0.02 Hz·80000^x` (0.02 … 1600 Hz). Ignored when synced |
| `_sync` | enum `Off` + the Peak's 35 divisions, `64b … 32T` (manual p.38) | Tempo-synced period |
| `_phase` | 0..1 | Start phase on key trigger, or offset from the beat grid |
| `_dly` | 0..1 | Delay before the LFO starts after note-on: `0`, else `5 ms·2000^x` (up to 10 s). A Crest extra |
| `_fade` | 0..1 | Fade Time, `x·10 s`; 0 = full depth whatever the mode |
| `_fmode` | enum `FadeIn, FadeOut, GateIn, GateOut` | What happens over the Fade Time (manual p.21) |
| `_slew` | 0..1 | One-pole smoothing of the output, cutoff `f = 200 Hz·0.0025^x` (200 Hz … 0.5 Hz) |
| `_trig` | enum `Free, Key, Common, CommonKey` | See below |
| `_rep` | int 0..127 | Cycles per trigger, then the LFO stops and holds; 0 = Off (run forever), 1 = one-shot |
| `_mtrig` | enum `Legato, Re-Trig` | LFO 1/2: does a legato note in Mono/MonoLG restart a Key LFO's wave |
| `_fsync` | enum `Off, On` | LFO 1/2: does a legato note restart the delay, fade and repeats. Web UI only (no room in chain_params) |

**Rate cap.** LFOs run at the 2 756 Hz control rate, so a free rate is capped at 1 240 Hz (below that rate's Nyquist limit). The knob and its reading still span the Peak's ranges. Real audio-rate LFOs (per sample) are not implemented.

### 2.2 Behaviour
- **Phase.** A float phase in [0, 1), advanced per sub-block by `rate·CREST_SUB/fs`.
- **Waves** (bipolar output −1..1):
  - Sine: LUT.
  - Tri, SawUp, SawDn and Square: computed directly.
  - S&H: new uniform random value at each cycle start.
  - Rnd: S&H with the transition cosine-interpolated over the cycle (smooth random).
  - Exp: `2·(1 − t)^3 − 1`, a decaying ramp useful for "pluck" wobble.
- **Trigger modes.**
  - `Free`: the phase runs continuously. Per-voice LFOs start at a random phase when the voice is allocated.
  - `Key`: the phase resets to `_phase` on note-on. For global LFOs this happens on the first note-on after all notes were released.
  - `Common`: per-voice LFOs share one instance-level phase and one set of random values (S&H, Rnd), so all voices move together like a global LFO but keep their own delay, fade and repeats. For LFO 3/4 it is the same as Free.
  - `CommonKey`: Common, and the shared phase restarts at `_phase` on every new note (the Peak's Common with a fixed Phase). For LFO 3/4 it is the same as Key.
- **New notes.** Every new note restarts the voice LFO's delay, fade and repeat count, also on a voice that is reused while it is still sounding (the same key again in Poly, a detached note in Mono). A Free LFO keeps its phase; a Key LFO restarts its wave. A legato note in Mono/MonoLG restarts the wave only with `_mtrig = Re-Trig`, and the timing only with `_fsync = On`.
- **Repeats.** The LFO counts the cycles it has run since the trigger. After `_rep` cycles it stops at the point where the last cycle ends and holds that value (the end of a falling saw stays at −1).
- **Tempo sync.** When `_sync ≠ Off`:
  - The period is the division in beats at `host->get_bpm()`, defaulting to 120 if unavailable.
  - When the clock status is RUNNING and `get_beat_position()` ≥ 0, Free/Common LFOs lock their phase to `frac(beat_pos / div_beats + _phase)`. This makes LFOs land on the grid in Move's transport.
  - Key and CommonKey reset on note-on and then free-run at the synced rate.
- **Delay and fade.** Per voice, a timer from the last trigger. Delay outputs 0. Then, over the Fade Time: FadeIn ramps the depth 0 → 1, FadeOut 1 → 0, GateIn holds 0 and then jumps to full, GateOut runs at full and then cuts to 0.
- **Rate modulation.** The matrix destination `LFOn Rate` adds to the normalised `_rate` (free mode), or scales the synced rate by `2^(4·m)`.
- **Output.** `out = fade_gain · slew(wave(phase))`, bipolar. Unipolar consumers use `(out + 1)/2` where documented in the matrix.

`lfo_t` holds `{phase, p0, sh_prev, sh_next, slew_y, t, trav, done, rng}`, about 36 bytes.

## 3. Tests
- **Envelope timing.** Measure the time to reach 1.0 and the time to fall within 1 % of S: within ±5 % of spec over 1 ms, 100 ms and 5 s settings.
- **Repeat.** With `_rep = 3`, exactly 3 extra A/H/D cycles. With Loop (127), it loops until note-off. Hold lasts its time.
- **Envelope clicks.** No discontinuity > 0.02 per sample on retrigger in Multi mode.
- **LFO rate.** Free rates within 0.5 %.
- **LFO sync.** Synced phase matches the beat position within 1 sub-block in the simulated host clock test.
- **LFO behaviour.** Repeats stop after the set number of cycles and hold. The four fade modes and the delay are within ±1 sub-block. A reused voice restarts its fade; MonoTrig and FadeSync act on legato notes; Common voices share S&H values (`tests/host/test_peakmod.inc`).
