# 10 — Effects (`fx.c`)

The global stereo chain runs per 128-frame block after the voice sum: **Distortion → Chorus → Delay → Reverb**.
- **Distortion** is an insert with a wet/dry crossfade. **Chorus**, like Delay and Reverb, adds its output to the dry signal, which stays at full (the Peak: "the amount of Chorus effect added to the dry signal", manual p.31).
- **Delay and Reverb** add their wet signal on top of the unity dry signal. Their `level` is the wet return gain, which behaves like a send.
- **Bypass.** An effect whose level is 0, after its tail has decayed, is bypassed with a 1-block fade, and its CPU is skipped.
  - Delay counts as silent when its buffer RMS falls below −90 dB; it is checked every 32 blocks.
  - Reverb counts as silent after `RT60 + predelay`.

## 1. Distortion
| Key | Range | Notes |
|---|---|---|
| `dst_lvl` | 0..1 | Drive amount `g = 1 + 30·x²`; 0 = bypass |
| `dst_type` | enum `Soft, Hard, Fold, Crush` | Append-only |
| `dst_tone` | 0..1 | Post tilt: 1-pole LP from 20 kHz (1.0) down to 1.5 kHz (0.0); default 0.7 |
| `dst_mix` | 0..1 | Wet/dry, default 1 |

**Curves:**
- Soft: tanh.
- Hard: `clamp(x, −1, 1)`.
- Fold: `sin(π/2·x)`, folding periodically.
- Crush: 4–12-bit quantise (bits from the drive amount) plus a sample-rate reduction of `÷1…÷8`.
- Soft, Hard and Fold use first-order ADAA. Crush is intentionally aliased.

**Gain.** Output is compensated with `1/sqrt(g)`.

## 2. Chorus
| Key | Range | Notes |
|---|---|---|
| `cho_lvl` | 0..1 | Return level added to the dry; 0 = bypass, 1 = wet as loud as the dry |
| `cho_type` | enum `Chorus 1, Chorus 2, Ensemble` | Number of taps per channel: 1, 2 or 3 |
| `cho_rate` | 0..1 | 0.05 Hz…8 Hz (exponential) |
| `cho_depth` | 0..1 | Modulation 0…6 ms |
| `cho_dly` | 0..1 | Base delay 3…30 ms |
| `cho_fb` | −1..1 | Feedback ×0.7 (negative = flanger-ish) |
| `cho_lp`, `cho_hp` | 0..1 | Output EQ: one-pole low-pass 500 Hz…20 kHz, high-pass 20 Hz…1 kHz (the Peak's LoPass / HiPass). Web UI only |

**Taps.** The taps' LFO phases are offset by 360°/taps within a channel. The right channel is offset by 90° from the left.

**Ensemble.** Adds a second, faster LFO at 6.3 × rate with 1/5 of the depth, giving a classic string-machine shimmer.

**Implementation.** The buffer is 2048 samples per channel (46 ms). Reads use cubic Hermite interpolation. The tap LFOs are triangle waves with parabolic smoothing.

## 3. Delay
| Key | Range | Notes |
|---|---|---|
| `dly_lvl` | 0..1 | Wet return; 0 = bypass (after tail) |
| `dly_time` | 0..1 | Free time 10 ms…2000 ms (exponential); ignored when synced. The Peak's maximum is 1.4 s, at x ≈ 0.93 |
| `dly_sync` | enum `Off, 1/32T, 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2T, 1/2, 1/1T, 1/2D, 1 bar` (the Peak's 16) | Synced time from `get_bpm`, clamped to 2 s |
| `dly_fb` | 0..1 | Feedback gain `0.98·x^1.5`, with tanh in the loop for analog-style runaway. At 0.5 (the Peak's 64) each repeat is −9 dB, about 5 or 6 audible echoes (manual p.31); at 1 the repeats barely decay |
| `dly_mode` | enum `Stereo, PingPong, Mono` | Ping-pong feeds the sum into L and alternates the feedback L→R→L |
| `dly_lp` | 0..1 | Loop low-pass, 1 kHz…20 kHz |
| `dly_hp` | 0..1 | Loop high-pass, 20 Hz…2 kHz |
| `dly_slew` | 0..1 | The Peak's Slew Rate: a free delay time follows its knob through a one-pole slew of the read heads, 10 ms…3 s, so moving Time bends the echoes' pitch (strongly at 0, barely at 1). Synced times and tempo changes crossfade between fixed taps instead (no pitch artefact). Web UI only |
| `dly_ratio` | enum `1/1, 4/3, 3/4, 3/2, 2/3, 2/1, 1/2, 3/1, 1/3, 4/1, 1/4` | The Peak's L/R Ratio: left : right echo times. The longer side is halved until it fits 2 s. Web UI only |
| `dly_width` | 0..1 | Stereo width of the echoes (mid/side); 0 = mono. Web UI only |

**Buffer.** A static `float buf[2][88200 + 4]` (2 s). The read position is fractional with cubic Hermite interpolation.

**Changing time.** A change of `dly_sync` or BPM re-targets the time through the slew logic.

## 4. Reverb
An 8-line feedback delay network:

| Key | Range | Notes |
|---|---|---|
| `rev_lvl` | 0..1 | Wet return; 0 = bypass |
| `rev_type` | enum `Room, Hall, Plate` | Selects base line lengths, diffusion and modulation |
| `rev_size` | 0..1 | Scales line lengths 0.3…1.0 |
| `rev_decay` | 0..1 | RT60 0.2 s…20 s (exponential); per-line feedback gain `g_i = 10^(−3·len_i/(RT60·fs))` |
| `rev_damp` | 0..1 | 1-pole LP in each line, 20 kHz…1.5 kHz |
| `rev_hp` | 0..1 | Input low-cut, 20 Hz…1 kHz |
| `rev_lpf`, `rev_hpf` | 0..1 | Output EQ: low-pass 500 Hz…20 kHz, high-pass 20 Hz…1 kHz (the Peak's LoPass / HiPass). Web UI only |
| `rev_mdep`, `rev_mrate` | 0..1 | Line modulation depth (0.5 = the type's own) and rate (×1…×32). Web UI only |
| `rev_pre` | 0..1 | Pre-delay 0…500 ms, linear (the Peak: about half a second at 127, manual p.32; 22 320-sample buffer) |

**Structure.**
1. Mono sum input → pre-delay → HP.
2. 4 series allpasses for input diffusion. Lengths are about 142, 107, 379 and 277 samples × size, with g = 0.7 (0.75 for Plate).
3. FDN: 8 lines with mutually prime base lengths. The Hall set is 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 samples × 2.2; Room and Plate use smaller sets. Each line is at most 8 192 samples.
4. Feedback matrix: an 8×8 Hadamard applied with a fast Walsh–Hadamard butterfly (24 add/sub) and scaled by `1/√8`.
5. All eight lines get slow (0.5–1 Hz) sine modulation (±6 / 12 / 18 samples for Room / Hall / Plate at the default depth) to break up metallic ringing. The line-length scales are 0.9 / 1.5 / 1.2 (lines up to 5 200 samples), which raises the modal density; RT60 is unchanged because the feedback gains follow the lengths.
6. Stereo output: L = lines 0, 2, 4, 6 and R = lines 1, 3, 5, 7 (alternating signs).

**Cost.** About 150 µs per block. Fallback lever: an "Eco" build using 4 lines, a compile-time option.

## 5. Memory & clearing
| Buffer | Size |
|---|---|
| Delay | 706 KB |
| Reverb lines + allpasses + pre-delay | ≈ 300 KB |
| Chorus | 16 KB |

**Clearing.** On instance claim, preset load or `state` load, `clear_pending` is set. Each block clears at most 64 KB, which takes about 16 blocks (≈ 46 ms). FX output stays muted until the clear finishes; the dry signal passes through.

## 6. Modulation hooks
Matrix destinations 41–45 ([07 §3](07-modulation.md)) add to `dst_lvl`, `cho_lvl`, `dly_lvl`, `rev_lvl` and `dly_fb` once per sub-block. Those parameters are smoothed per sample inside the FX.

## 7. Tests
- **Delay timing.** At 1/4 and 120 BPM the delay is 22 050 samples ±1. Ping-pong alternates channels.
- **Reverb decay.** Measured RT60 is within ±15 % of setting for 1 s, 3 s and 10 s.
- **Stability.** No NaN or runaway with feedback at 1.0 for 60 s (tanh-limited, peak < 2.0).
- **Bypass.** Bypass at level 0 costs < 2 µs per effect (bench).
- **Clearing.** The incremental clear never touches more than 64 KB per block (instrumented test).
