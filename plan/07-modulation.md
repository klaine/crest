# 07 — Modulation Matrix, Animate & Macros

## 1. Matrix model
There are 16 slots. Each slot is `{src1, src2, dst, depth}`, and its contribution is:
```
c = depth · S(src1) · (src2 == Off ? 1 : S(src2))
dst_acc[dst] += c
```
- `depth` ranges −1..1 and is displayed as −100…+100 %.
- With two sources the slot multiplies them, e.g. "LFO 1 × Mod Wheel → Pitch" gives wheel-controlled vibrato.
- Slots are evaluated at control rate (every 16 samples), per voice.
- `dst_acc` values are in **normalised destination units**: 1.0 means the full range of the destination as defined in the tables below.
- The hard-wired depths (e.g. `flt_env`, `oN_penv`) are separate from the matrix and add to it.

## 2. Sources (`MOD_SRC_*`, append-only enum `mm_src`)
| # | Name (short) | Scope | Range | Notes |
|---|---|---|---|---|
| 0 | Off (`--`) | – | – | As `src2`, Off means ×1 |
| 1 | Direct (`Dir`) | global | 1 | Constant; use for static offsets |
| 2 | Mod Wheel (`Whl`) | global | 0..1 | CC 1 |
| 3 | Chan AT (`AT`) | global | 0..1 | 0xD0 |
| 4 | Poly AT (`PAT`) | voice | 0..1 | 0xA0, matched to the voice's note |
| 5 | Expression (`Exp`) | global | 0..1 | CC 11 |
| 6 | Breath (`Brth`) | global | 0..1 | CC 2 |
| 7 | Sustain (`Sus`) | global | 0/1 | CC 64 |
| 8 | Pitch Bend (`PB`) | global | −1..1 | |
| 9 | Velocity (`Vel`) | voice | 0..1 | Note-on velocity |
| 10 | Key (`Key`) | voice | −1..1 | `(note − 60)/60` |
| 11 | Random (`Rnd`) | voice | 0..1 | New per note-on |
| 12 | Amp Env (`AEnv`) | voice | 0..1 | |
| 13 | Mod Env 1 (`ME1`) | voice | 0..1 | |
| 14 | Mod Env 2 (`ME2`) | voice | 0..1 | |
| 15 | LFO 1 (`LFO1`) | voice | −1..1 | |
| 16 | LFO 2 (`LFO2`) | voice | −1..1 | |
| 17 | LFO 3 (`LFO3`) | global | −1..1 | |
| 18 | LFO 4 (`LFO4`) | global | −1..1 | |
| 19 | Animate 1 (`An1`) | global | 0..1 | §4 |
| 20 | Animate 2 (`An2`) | global | 0..1 | §4 |
| 21–28 | Macro 1–8 (`M1`…`M8`) | global | 0..1 | §5 |

That is 29 options including Off. Global performance sources are smoothed over 5 ms (one-pole) to avoid zipper noise from 7-bit CCs.

## 3. Destinations (`MOD_DST_*`, append-only enum `mm_dst`)
| # | Name | Scale of 1.0 | Scope |
|---|---|---|---|
| 0 | Off | – | – |
| 1 | Pitch (all osc) | 48 semitones | voice |
| 2–4 | Osc 1/2/3 Pitch | 48 semitones | voice |
| 5–7 | Osc 1/2/3 Shape | full shape range | voice |
| 8–10 | Osc 1/2/3 VSync | full vsync range | voice |
| 11–13 | Osc 1/2/3 Density | full density range | voice |
| 14–16 | Osc 1/2/3 Level | full level range | voice |
| 17 | Noise Level | full | voice |
| 18 | Ring Level | full | voice |
| 19 | Pre-OD | full | voice |
| 20 | Cutoff | 96 semitones | voice |
| 21 | Resonance | full | voice |
| 22 | Filter Drive | full | voice |
| 23 | Post Dist | full | voice |
| 24 | Amp Level | ×(1 + m), clamped 0..2 | voice |
| 25 | Pan | full (−1..1) | voice |
| 26–28 | AmpEnv Attack / Decay / Release | full normalised time | voice |
| 29–31 | ModEnv1 Attack / Decay / Release | full | voice |
| 32–34 | ModEnv2 Attack / Decay / Release | full | voice |
| 35–36 | LFO 1/2 Rate | full (sync: ×2^(4m)) | voice |
| 37–38 | LFO 3/4 Rate | full | global |
| 39 | Glide Time | full | voice |
| 40 | Unison Detune | full | voice |
| 41 | Distortion Level (FX) | full | global |
| 42 | Chorus Level | full | global |
| 43 | Delay Level | full | global |
| 44 | Reverb Level | full | global |
| 45 | Delay Feedback | full | global |

That is 46 options including Off.

### 3.1 Evaluation order (per sub-block)
1. **Global sources.** Update LFO 3/4, wheel, AT, bend, expression, breath, sustain, Animate and macros.
2. **Per voice.**
   - Update the envelope and LFO 1/2 sources using **last sub-block's** `dst_acc` for times and rates; this avoids an ordering cycle.
   - Fill `mod_val[]`.
   - Clear `dst_acc`.
   - Run the 16 slots, skipping slots with `dst == Off` or `depth == 0`. The active slot list is precomputed in `params_apply_dirty` so this costs almost nothing.
   - Add macro direct assignments (§5).
3. **Global destinations** (FX levels, LFO 3/4 rate, delay feedback): evaluate only the matching slots once, using the **most recently triggered voice's** sources for per-voice sources. Use the global sources directly when every source is global.

### 3.2 Exposing modulated values
- `cutoff`, the three osc shapes and `pre_od` are declared `live:true`.
- `get_param("<key>:effective")` returns the base value plus the last voice's modulation, normalised and clamped. The host uses it to draw moving knobs. **VERIFY** the polling rate and cost; keep it to a `snprintf` of one cached float.

## 4. Animate 1 / 2
- **Parameters.**
  - `anim1`, `anim2`: enum `Off, On`. They can be automated and stored in Scenes, so a Move Scene can "press" Animate.
  - `anim1_mode`, `anim2_mode`: enum `Hold, Latch`. This only affects the MIDI CC input.
- **MIDI.** CC 80 drives Animate 1 and CC 81 drives Animate 2. In Hold mode, value ≥ 64 is on. In Latch mode, each rising edge toggles. **VERIFY** whether Move or Schwung delivers these CCs to the slot.
- **Output.** The value is smoothed with a 10 ms one-pole ramp. It is a 0..1 matrix source.
- **Live readout.** `anim1:effective` shows the live state, including CC-driven changes.
- **Main page.** Animate 1/2 also appear on the Main page as switches (viz `switch`), giving one-knob "performance" toggles.

## 5. Macros
- **Values.** 8 macro knobs, `mac1`…`mac8` (0..1, default 0), on the Macros page. Each is a matrix source.
- **Direct assignments.** Each macro also has 4 assignments:
  ```
  macN_dK : destination (same enum as mm_dst)    K = 1..4
  macN_aK : depth −1..1
  contribution = macN · macN_aK → dst_acc[macN_dK]   (added at step 2 above, for every voice)
  ```
- **Why both.** The direct assignments let one Move knob morph several parameters without spending matrix slots. They also make macros useful in Scenes and automation, since `mac1`…`mac8` are ordinary keys.
- **Smoothing.** Macro values use the standard parameter smoother.

## 6. Storage & UI keys
The matrix and macro routing keys are stored in `state` but **not listed in `chain_params`**, to keep the contract small. The UI edits them through proxy keys ([12 §5](12-ui-pages.md)), following SchwungFreak's `mod_edit_*` pattern (`SchwungFreak/src/schwungfreak.c:1519`, `format_mod_matrix_items` at `:2979`).

| Key | Kind | In `chain_params`? |
|---|---|---|
| `mmNN_s1`, `mmNN_s2`, `mmNN_dst`, `mmNN_dep` (NN = 01..16) | raw slot | no (state only) |
| `macN_dK`, `macN_aK` | raw macro routing | no |
| `mm_sel` | int 0..15, selected slot | yes (select_param) |
| `mm_items` | list JSON `[{"index":i,"label":"1: LFO1×Whl>Pitch"},…]` | get_param only |
| `mm_s1`, `mm_s2`, `mm_dst`, `mm_dep` | proxies for the selected slot | yes |
| `mm_slot` | int 1..16 knob alias of `mm_sel` | yes |
| `mm_clear` | write-only trigger `-, Clear` | yes |
| `mac_sel`, `mac_items`, `mac_d1..4`, `mac_a1..4` | same pattern for macro routing | yes / get_param |

- **Label format.** `mm_items` labels are at most 20 characters, in the form `"<n>: <S1>[×<S2>]><Dst>"`, using the short names. An empty slot reads `"<n>: --"`.
- **Proxy reads.** A proxy read returns the selected slot's raw value. A proxy write writes the raw slot, re-precomputes the active slot list, and bumps `contract_rev` so the host refreshes the list labels. **VERIFY** what SchwungFreak does to make the host re-read item labels (`is_loading`/`contract_dirty_blocks`).

## 7. Tests
- **Slot maths.** A known LFO with depth 0.5 to Cutoff gives the expected `cut_semis` delta (±0.01).
- **Dual source.** Wheel at 0 mutes an LFO1×Wheel route.
- **Proxies.** Changing `mm_sel` changes what the proxy keys read. Proxy writes land in the right raw slot, and `state` round-trips all 16 slots and 32 macro assignments.
- **Global destinations.** Delay Level modulated by LFO 3 changes the FX send, and the change is audible in a golden render.
