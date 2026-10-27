# 15 — Roadmap

> **Status:** M0, M1 and the effects part of M4 are implemented; see [17](17-implementation-status.md) for what is done, measured and still to verify on a Move.

Each milestone ends with a device test ([14 §7](14-testing.md)) and a tagged build (`v0.M.0`). A box is ticked only when its acceptance criteria pass.

## M0 — Skeleton & measurements (de-risk)
**Goal:** prove the toolchain, ABI, deploy loop and CPU calibration.

- [ ] `git init`. Add `.gitignore` (`build/`, `modules/**/dsp.so`, `*.tar.gz`), `LICENSE` (MIT) and `README.md`.
- [ ] Copy the vendored refs from Amino: `src/dsp/plugin_api.h`, `move_info.h`, and the `docs/_schwung_*.md` files.
- [ ] `src/module.json` ([01 §4](01-platform-and-abi.md)) and a minimal `help.json`.
- [ ] `crest.c`:
  - static pool of 4 instances
  - `move_plugin_init_v2`
  - a single sine voice on note-on/off
  - FTZ
  - master soft clip
  - `state` returning `{"fmt":"crest","v":"1"}`
  - a tiny hand-written `chain_params` with `patch_vol`
- [ ] `scripts/build.sh` using zig ([02](02-build-toolchain.md)), `scripts/deploy.sh` and `scripts/logs.sh`.
- [ ] `tests/host/test_main.c` covering the ABI and pool suites.
- [ ] `--profile` build with `cpu_us`. `bench_main.c` sine benchmark to calibrate the host-to-device factor.
- [ ] **VERIFY on device:**
  - glibc version
  - `state` buffer size
  - knob arrays shorter than 8
  - CC 80/81 delivery
  - whether a CLI rescan exists
  - `debug.log` path
  - whether Poly AT arrives as 0xA0

  Record the answers in [16](16-risks-and-open-questions.md).

**Accept:** Crest loads on Move, plays a sine from pads, survives a Set reload, and the measured `cpu_us` is recorded.

## M1 — Codegen & core voice
- [ ] `tools/gen/params.py` with every key from [11](11-parameter-reference.md). The values can be inert at first. Adapt `gen.py` from Amino to emit:
  - `params_gen.c/h`
  - `contract_gen.c`
  - `build/*.json`
  - the `--check` mode
- [ ] `params.c`: get/set, dirty bits, smoothers, `state` serialise/parse, and the migration hook.
- [ ] `osc.c`: Sine, Tri, Saw and Pulse with 2-point polyBLEP and a `CREST_BLEP_POINTS=4` option ([04 §3.1](04-oscillators.md); start from `plan/reference/`), shape, coarse/fine, `osc_reset`.
- [ ] Mixer, `filter.c` (all 6 types, cutoff/res/key/env1/lfo1/vel), VCA, pan.
- [ ] `env.c`: full DADSR with repeats and trigger modes.
- [ ] `voice.c`: Poly mode, allocation and stealing with fade, sustain.
- [ ] Pages: Main, Osc 1–3 (+Mod), Mixer, Filter, Amp/Mod1/Mod2 Env, Voice (partial).
- [ ] `validate_contract.py`, plus the params, state, osc, filter, env and voice test suites.

**Accept:** an 8-voice, 3-osc subtractive synth that is fully editable on Move. The contract validates, state round-trips, and `cpu_us` ≤ 450 µs with 8 voices and 3 saws.

## M2 — Character
- [ ] Density with density detune. Virtual sync for classic waves.
- [ ] `tools/wavegen/wavegen.py`: 17 original tables × 16 frames with mip-maps → `wavetables.c`. WT oscillator with frame morphing and sync.
- [ ] Pre-OD, filter drive and post-dist with ADAA. Resonance compensation.
- [ ] Drift. Voice modes Poly2, Mono, MonoLG and Mono2. Unison with detune and spread. Glide.
- [ ] Golden test harness with the first references (INIT, supersaw, sync lead, driven bass).
- [ ] Alias and droop tests from [04 §10](04-oscillators.md), including the chord check.
- [ ] A/B of `CREST_BLEP_POINTS` 2 vs 4 and of a table-based Saw/Pulse by ear and by the tests. The default stays 2-point unless the aliasing is audible or the table wins clearly.
- [ ] Listening pass ([14 §8](14-testing.md)). If aliasing is audible, decide whether to add 2× oversampling in the drive and filter stages.

**Accept:** the worst-case oscillator, filter and drive patch is ≤ 600 µs on the device, the golden tests pass, and the listening checklist items 1–5 pass.

## M3 — Modulation
- [ ] `lfo.c` covering LFO 1–4: all waves, sync to host BPM and beat, delay, fade, slew and trigger modes.
- [ ] `mod.c`: the 16-slot matrix, sources and destinations, global-destination handling, and `:effective` live values.
- [ ] Animate 1/2 (params + CC). Macros with direct routing.
- [ ] Proxy keys, the `mm_items`/`mac_items` list pages, and the Matrix Edit and Macro Edit pages.
- [ ] Hard-wired depths connected: `flt_lfo`, `oN_plfo`, `oN_penv`, `oN_shsrc`.
- [ ] Matrix, LFO and proxy test suites.

**Accept:** "LFO1 × Wheel → Pitch" and "Poly AT → Cutoff" work on the device, the list pages show live labels, and all 16 slots survive a Set reload.

## M4 — FX & Arp
- [ ] `fx.c`: distortion, chorus, delay (sync, ping-pong, slew) and FDN reverb, with incremental clears and bypass.
- [ ] `arp.c`: modes, octaves, 33 rhythms from `tools/rhythms`, gate, swing, latch, key sync, and the host clock.
- [ ] FX and Arp pages. FX matrix destinations.
- [ ] FX and arp test suites. Golden references with FX.

**Accept:** the full worst case is ≤ 900 µs p99 on the device, the arp follows Move's transport, and 4 Crest tracks run with no xruns.

## M5 — Content
- [ ] 64 factory presets ([13 §4](13-presets-and-state.md)), authored in the WASM build. Golden test for every preset.
- [ ] Final `help.json`.
- [ ] Viz polish: check groups and short names on the device.
- [ ] Preset browser page.

**Accept:** every preset loads click-free and has macros 1–4 assigned. A blind run-through on the device finds no unusable pages.

## M6 — Remote UI & release
- [ ] `web_ui.html` ([12 §8](12-ui-pages.md)).
- [ ] `scripts/build_web.sh` site published (optional).
- [ ] CI workflow, `release.json` and a catalog entry PR to Schwung.
- [ ] README with a feature list, install steps and the parameter reference (generated by `tools/gen/dump_contract.py`).

**Accept:** v1.0.0 tarball installs through Schwung Manager, and the remote UI edits every section.

## Effort guide (rough)
| Milestone | Relative size |
|---|---|
| M0 | S |
| M1 | L |
| M2 | L |
| M3 | M |
| M4 | M–L |
| M5 | M |
| M6 | M |

M1 and M2 carry most of the DSP risk. M0's measurements decide whether the budget in [03 §8](03-dsp-architecture.md) needs revising before M1.
