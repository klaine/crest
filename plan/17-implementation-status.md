# 17 — Implementation Status

State of the code against this plan. Updated when the code changes; the code is the source of truth.

## Done: the core version
Milestones M0 and M1 of [15](15-roadmap.md), plus the effects chain from M4 (ported from Amino).

| Area | Status |
|---|---|
| Repo, vendored `plugin_api.h`, `module.json`, `help.json`, LICENSE | Done |
| Zig build (`scripts/build.sh`): codegen check, contract validation, native tests, aarch64 `dsp.so`, symbol check, package | Done. Tested with zig 0.17.0 |
| Codegen: `tools/gen/params.py` to descriptor table, contract strings and factory bank | Done: 89 parameters |
| Instance pool (4), `create/destroy`, MIDI, render loop, FTZ, soft clip, NaN guard | Done |
| Oscillators: Sine, Tri, Saw, Pulse with Shape; polyBLEP 2-point (default) / 4-point (`CREST_BLEP_POINTS=4`) | Done |
| Mixer: 3 oscillators, noise with colour tilt, ring modulator | Done |
| Filter: 6 types, resonance to self-oscillation, in-loop drive, key / Mod Env 1 / velocity | Done |
| Envelopes x3 with delay, repeats, trigger modes, velocity | Done |
| Voices: Poly, Poly2, Mono, MonoLG, glide, bend, octave, sustain, stealing with fade | Done |
| Effects: distortion, chorus, delay (tempo sync), reverb | Done, adapted from Amino's `fx.c` |
| State blob, 11 factory presets, `preset` browser | Done |
| Move pages: Main, Osc 1-3, Mixer, Filter, 3 envelopes, Voice, 4 effects, Factory Patches | Done (host knob grid) |
| Tests: contract validator, 998 native checks (UBSan), benchmark, WAV renderer | Done |

Build output: `crest-module.tar.gz` (29 KB), `dsp.so` 64 KB stripped. `chain_params` is 12.4 KB (limit 32 KiB), `ui_hierarchy` 3.4 KB.

### Measured on the build machine (Apple silicon, not a Move)
`scripts/build.sh --bench`, 128-frame blocks, 3000 blocks per case:

| Patch | mean | p99 |
|---|---|---|
| idle | 0.9 us | 1.0 us |
| 8 voices, init saw | 25.8 us | 37 us |
| 8 voices, 3 oscillators, resonant filter | 31.2 us | 43 us |
| worst case: same plus all 4 effects | 42.7 us | 54 us |

That is under 2% of the 2902 us block. The Move's Cortex-A72 is several times slower, so expect roughly 150-300 us; the 900 us budget in [03 §8](03-dsp-architecture.md) looks safe, but this is **not measured on the device**.

## Since the core version
M2 (character), M3 (modulation) and most of M4 are in: density / virtual sync / wavetables, pre-overdrive and
post-distortion, drift, unison, Mono2, divergence, fixed note, FM between oscillators and into the filter,
LFOs 1-4, a 20-slot mod matrix, Animate and macros, FX routing, and the arpeggiator. 335 parameters.
The Peak SysEx banks are converted and built into local builds only ([18](18-peak-import.md)): the
committed bank has the 11 Crest patches, a local build with `presets/peak/` has 540.
`tests/peak/test_import.py` and 9,596 native checks pass. Crest runs on a Move, Peak `.syx` import included.

## UI (October 2026)
- **On the Move:** `src/canvas.js` adds a home page (patch name, filter response, amp envelope) in front of
  the root knobs and a bank-aware patch browser; `src/cards.js` draws a card for 38 knobs (env times,
  cutoff Hz, LFO rate, FM route, arp rhythm, FX chain, matrix route, note names). Rendered against a
  mock context by `tests/device/test_device_ui.mjs`; **not seen on hardware**. `chain_params` is 31.5 KB.
- **Web UI:** `src/web_ui.html` + `src/web/` is packaged as the module's Remote UI. `scripts/build_web.sh`
  also builds `build/site`: the same UI driving `crest.wasm` in an AudioWorklet, and `build/crest.html`
  (also made by `scripts/build.sh`): that synth in one ~7 MB file to share, without the Peak banks
  (11 patches; `.syx` import still works). The single file was checked over http in Chromium; opening
  it from `file://` and in Safari/Firefox is untested. Checked in a browser
  (standalone, plus a mocked `schwungRemote`); the real Remote UI on a Move is unverified, in particular
  whether its first value burst includes the raw matrix and macro keys (they are not in `chain_params`).
- **Fixes from the review:** FM is now true phase modulation (it used to detune the carrier); a voice-mode
  change followed by a note in the same block no longer drops the note; Mod Env 2 pitch depth reaches
  8 octaves like the Peak; pitch bend is smoothed.

## Envelopes and LFOs against the Peak manual (October 2026)
Checked against the Peak 1.2 manual (pp.20-23); see [06](06-envelopes-lfos.md):
- **Envelopes:** a Hold stage (to 500 ms); attack/decay/release full-scale 20 / 22 / 25 s; repeats 0..126 or
  loop, cycling attack-hold-decay; negative velocity. MonoLG no longer forces legato envelopes: each
  envelope's trigger decides, as on the Peak (Amp defaults to Single = Legato, Mod Envs to Multi = Re-Trig).
- **LFOs:** Low (0.02..200 Hz) and High (0.16..1600 Hz) ranges for LFO 1/2, one wide range for LFO 3/4;
  the Peak's 35 sync divisions; Repeats (OneShot is gone: Repeats 1); FadeIn / FadeOut / GateIn / GateOut;
  MonoTrig and FadeSync; Common shares S&H values; CommonKey restarts the shared phase per note.
- **Fixes:** the LFO fade, delay and repeats restart on a reused voice (same key again in Poly, detached
  notes in Mono), which they used to skip.
- **Import:** envelope MonoTrig, Hold and Repeats, negative velocity, the LFO range, repeats, fade mode,
  MonoTrig and FadeSync, and LFO 3/4 are now converted (the inferred bytes are listed in [18](18-peak-import.md)).
- **Not done:** audio-rate LFOs. LFOs run at the 2 756 Hz control rate and a free rate is capped at
  1 240 Hz, though the knob spans the Peak's ranges. Animate attack/release times (Peak firmware 2.0) are
  not implemented. Enum orders for `lN_sync` and `lN_trig` changed; nothing had been released, and every
  preset was regenerated.

## Peak banks you load yourself (October 2026)
The Peak converter is now also C (`src/dsp/peak_import.c`), checked value for value against the Python one.
The browser build loads `.syx` files in a Web Worker; the Move loads them from `UserLibrary/Crest` on a worker thread
(`src/dsp/peak_bank.c`), the first threads and file I/O in `dsp.so` (whitelisted for the worker only in
`scripts/check_so.py`). Host tests cover the worker; the device side is **not tried on a Move**. `chain_params`
is now 31.7 KB: within 22 bytes of the validator's 31 KiB limit.

## Code review fixes (October 2026)
- **Chord after an all-off:** after a preset or state load, CC 120 or a voice-mode change every voice fades, and a
  chord played during that 1.5 ms fade kept only one note: each note overwrote the oldest fading voice's queued
  start. The victim picker now prefers a fading voice with nothing queued.
- **Loudness worker:** the solver renders on a scratch instance kept outside the 4-slot pool, so levelling a bank
  never takes a slot the host needs, and it polls for a cancel between blocks: unloading `dsp.so` (whose destructor
  joins the worker) or a new bank request no longer waits for up to three 6 s renders.
- **CPU:** the oscillators' sine is a 9th-order polynomial instead of `sinf` (within 4e-6), and the pre-filter
  overdrive and post-filter distortion keep the previous sample's antiderivative while the drive is steady.
- **Loudness solve:** `crest_level_measure` now sets `offline`, which nothing set before: on a Move the worker thread
  asked the host for tempo and transport, and synced patches levelled differently depending on what was playing.
- **Mono unison:** changing Unison in a mono mode retires the voices (like a voice-mode change). Lowering it during a
  held note used to leave the extra voices sounding forever.
- **Parameter values:** `nan` and `inf` are rejected (NaN passed the clamp and muted every note through the NaN guard).
- **BP12 self-oscillation** is about 8 dB below LP12 instead of about 24 dB (23 Peak patches use BP12 above resonance
  0.7, where the floor applies). All 529 Peak patches were re-levelled: 388 `patch_vol` values changed, most because
  the stored levels predated earlier sound changes (the code before these fixes gives the same new values).
- **Effects** stay on for 2 s at level 0 (see Deviations).
- **Virtual sync** predicts the reset jump only inside the polyBLEP window and steps the sync ratio by a constant
  factor: about 22% less time on an 8-voice patch with all oscillators synced (146 to 114 us on the build machine),
  bit-identical output.

## Not done yet
- **M4:** reverb freeze. (`dly_slew`, delay L/R ratio and width, chorus and reverb EQ and reverb modulation are in, web UI only: chain_params is full.)
- **M5:** 64 curated patches (the 529 Peak patches cover evaluation for now).
- **M6:** CI, catalog entry, golden-file tests.
- Open points in the Peak import: see the end of [18](18-peak-import.md); the byte layout is inferred, not documented by Novation.
- `chain_params` is 31.5 KB against the validator's 31 KiB limit (host buffer 32 KiB): about 250 bytes left.
  LFO FadeSync and LFO 3/4 Repeats / Fade Mode are web-UI only for that reason.

## Deviations from the spec
- **MIDI is applied immediately in `on_midi`** instead of through a queue drained at the start of the block. Both run on the same thread, so the result is identical (events have no timestamps).
- **No lookup tables:** pitch, cutoff and sine use `exp2f` / `tanf` / `sinf` directly. The benchmark shows this is cheap enough; revisit only if the device measurement says otherwise.
- **Filter details** the spec left open: damping `k = 2 - 2.08 * res^0.9` (res near 1 goes negative, so the filter self-oscillates); the band-pass outputs `max(k, 0.5) * v1`, unity at the centre until high resonance, where the peak grows as a low-pass's does (plain `k * v1` muted BP12's self-oscillation); the band-pass state is soft-saturated (`tanh`) with onset set by Drive; input gain `(1 + 0.5 res)(1 + 2 drive)` and output gain `1/(1 + drive)`; a trace of noise (1e-6) is added at low damping so self-oscillation starts without input.
- **Parameters left out of this version** (they belong to later milestones): `noise_type` (dropped for good, noise colour covers it), `dly_width` (dropped), `dly_slew`, `pre_od`, `post_dist`, `flt_lfo`, `oN_plfo`, `oN_dens`, `oN_ddet`, `oN_vsync`, the wavetable wave and `oN_table`, `v_unison`, `v_udet`, `v_spread`, `v_drift`, matrix, macros, Animate, arp.
- **Effects at level 0 are switched off after 2 s** (a reverb or delay tail is then cut), which saves CPU. The hold keeps a level that modulation dips to 0 from wiping the echoes. Switching an effect back on first clears its buffers at 64 KB per block, so it is dry for up to about 40 ms.
- **Chorus depth** is limited to 90% of the base delay so a tap can never go negative.
- **Alias limits** for the default 2-point polyBLEP are in `tests/host/test_main.c`; the 4-point limits are the ones in [04 §10](04-oscillators.md).

## To verify on a Move (nothing below has been run on hardware)
1. Install with `scripts/deploy.sh`; the module appears as a sound generator and `debug.log` is clean. The build targets `aarch64-linux-gnu.2.28`; check Move's glibc if it fails to load.
2. Pages with fewer than 8 knobs (Mixer 6, Filter 7, Voice 7, effects 4-7) draw correctly.
3. The `viz` declarations: envelope groups, the filter picture with the type as a separate cell, and the waveform and fader kinds.
4. `get_param("state")` fits the host's buffer (the blob is about 1.6 KB).
5. `preset_name` returns the current patch name when the browser asks.
6. CPU: build with `scripts/build.sh --profile`, load the worst-case patch, hold 8 notes and read `cpu_us`.
7. Notes arrive per track as expected, polyphonic aftertouch arrives (unused so far), and tempo sync follows Move's BPM.
8. Save to My Presets, reload the Set, and compare the sound.
