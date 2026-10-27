# Crest — Implementation & Specification Plan

Crest is a Peak-inspired, 8-voice, 3-oscillator polyphonic synthesizer for Ableton Move, delivered as a Schwung sound-generator module (`dsp.so` + `module.json`). These documents are the spec it is built against. The code is the source of truth once it exists. If code and spec disagree, fix one of them in the same change.

| # | Document | Contents |
|---|----------|----------|
| 00 | [Overview & requirements](00-overview.md) | Product goals, scope, non-goals, naming/IP rules |
| 01 | [Platform & ABI](01-platform-and-abi.md) | Move hardware, Schwung plugin API v2, realtime rules, `module.json` |
| 02 | [Build toolchain (Zig)](02-build-toolchain.md) | `zig cc` for device, native and WASM builds, scripts, packaging, deploy |
| 03 | [DSP architecture](03-dsp-architecture.md) | Signal flow, block/control rate, memory layout, smoothing, CPU budget |
| 04 | [Oscillators](04-oscillators.md) | Classic waves, shape, density, virtual sync, wavetables, noise, ring |
| 05 | [Mixer, filter & drive](05-mixer-filter-drive.md) | Mixer, pre-overdrive, multimode SVF, filter drive, post-distortion, VCA |
| 06 | [Envelopes & LFOs](06-envelopes-lfos.md) | 3 DADSR envelopes with repeat, 4 LFOs (2 voice, 2 global) |
| 07 | [Modulation, Animate & Macros](07-modulation.md) | 16-slot dual-source matrix, sources/destinations, Animate, 8 macros |
| 08 | [Voice management](08-voice-management.md) | Allocation, stealing, voice modes, unison, glide, drift, MIDI handling |
| 09 | [Arpeggiator](09-arpeggiator.md) | Modes, rhythms, clocking, latch |
| 10 | [Effects](10-effects.md) | Distortion, Chorus, Delay, Reverb |
| 11 | [Parameter reference](11-parameter-reference.md) | Every key, range, default, curve, enum list |
| 12 | [Move UI pages](12-ui-pages.md) | `ui_hierarchy`, page layouts, viz groups, list pages, help |
| 13 | [Presets & state](13-presets-and-state.md) | State blob, factory bank, codegen, migration |
| 14 | [Testing & verification](14-testing.md) | Contract, native, golden, WASM, device and CPU tests |
| 15 | [Roadmap](15-roadmap.md) | Milestones M0–M6 with tasks and acceptance criteria |
| 16 | [Risks & open questions](16-risks-and-open-questions.md) | Things to VERIFY on hardware |
| 17 | [Implementation status](17-implementation-status.md) | What is built, measured and deviates from this spec; what to verify on a Move |

## Reference projects (siblings in `../`)
- **Amino** (`../Amino for Move/Development`) is the primary template. It provides the Zig build, the `tools/gen` codegen, the contract validator, golden tests and the WASM host. Its `docs/_schwung_*.md` files are vendored copies of the Schwung docs; copy them into `docs/` at M0.
- **Bizar** (`../Bizar for Move`, formerly SchwungFreak; `src/schwungfreak.c` is now `src/bizar.c`, so the line numbers cited in these docs are out of date) provides the dynamic `ui_hierarchy`, the items-list page pattern (`mod_matrix_items`), the FTZ handling, and the 32 KiB `chain_params` lesson.

## Conventions used in these docs
- **VERIFY** marks a claim that must be confirmed on a device or in the Schwung source before code depends on it.
- Parameter keys are written like `flt_cut`. The definitive list is [11](11-parameter-reference.md).
- Normalised values are written as `0..1`. Physical units are given where the mapping matters.
