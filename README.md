# Crest

An 8-voice hybrid polysynth for Ableton Move, delivered as a [Schwung](https://github.com/charlesvestal/schwung)
sound-generator module. Three band-limited oscillators, a multimode saturating filter, three envelopes
and a Distortion / Chorus / Delay / Reverb chain, all on the Move's encoder pages.

**Try it live:** [play Crest in your browser](https://klaine.github.io/crest/) (WebAssembly build, WebMIDI).

Status: **milestones M0-M4 of `plan/` plus the web UI**. It builds, passes its test suite and has a
release-style package, and it runs on a Move. See
[plan/17-implementation-status.md](plan/17-implementation-status.md) for what is done and what is not.

## Features
- **Voices:** 8, modes Poly / Poly2 / Mono / MonoLG / Mono2, unison up to 8, glide, divergence, analog drift.
- **Oscillators (x3):** Sine, Tri, Saw, Pulse with Shape, 75 wavetables (17 Crest originals plus one for every Novation
  Peak wavetable name, designed for Crest), density stacks, virtual sync,
  fixed-note mode, polyBLEP anti-aliasing (2-point default, 4-point build option).
- **FM:** Osc1>Osc2, Osc2>Osc3, Osc3>Osc1 and Noise>Osc1 phase modulation; Osc 3 and noise into the cutoff.
- **Mixer and filter:** three oscillators, ring, coloured noise, pre-overdrive and post-distortion;
  LP24 / LP12 / BP12 / BP6 / HP24 / HP12 with resonance up to self-oscillation and in-loop drive.
- **Modulation:** 3 delay-AHDSR envelopes with repeats and legato/retrigger modes; 4 LFOs (1-2 per voice
  with Low/High ranges, 3-4 global) with the Peak's sync table, repeats, four fade modes, slew and Common
  modes; a 20-slot matrix (33 sources, 59 destinations), 8 macros, Animate 1-2. LFOs run at control rate,
  so free rates are capped at 1.24 kHz.
- **Arpeggiator:** 7 modes, 19 rates, 33 rhythms, 1-6 octaves, gate, swing, latch, key sync, host tempo.
- **Effects:** distortion, chorus, tempo-synced delay and reverb, in any of 7 routings with dry/wet.
- **11 factory patches:** Init Saw and 10 curated, full state save and recall. Peak banks are not bundled:
  load your own (below), or build them into a local factory set.
- **On the Move:** a home page drawing the filter and amp envelope, a bank-aware patch browser, and cards
  that show held knobs in real units (ms, Hz, semitones, note names) with a picture where it helps
  (`src/canvas.js`, `src/cards.js`).
- **Your own Peak banks:** load a Novation Peak `.syx` bank or patch in the browser build (Patches → Load Peak
  .syx…) or, on the Move, open one from `UserLibrary/Crest` on the Peak Banks page. Both convert it with the same rules
  as the built-in banks and set each patch's loudness in the background (see
  [plan/18-peak-import.md](plan/18-peak-import.md)).
- **Web UI:** a Peak-style panel for every parameter (`src/web_ui.html`, `src/web/`). On a Move it opens in
  Schwung's Remote UI; standalone it runs the same engine as WebAssembly with an on-screen keyboard,
  computer-keyboard play, Web MIDI, mod and bend strips, patch browser, undo, and patch save/load.

## Build and test
Needs [zig](https://ziglang.org) (`brew install zig`) and python3. Zig is the only C compiler used.

```sh
scripts/build.sh            # codegen check, contract, ~9,600 native checks, UI tests, aarch64 dsp.so, package
scripts/build.sh --bench    # also a native CPU benchmark
scripts/build.sh --profile  # dsp.so with the cpu_us debug key (for measuring on the Move)
scripts/build.sh --release  # a package to publish: no local Peak banks, checked by scripts/check_release.py
CREST_BLEP_POINTS=4 scripts/build.sh   # 4-point polyBLEP variant
python3 tools/gen/gen.py    # regenerate params / contract / presets after editing tools/gen/params.py
```

Output: `modules/sound_generators/crest/dsp.so`, `crest-module.tar.gz` and `build/crest.html` (below).

Listen without a Move: `scripts/build.sh --render` writes one WAV per factory patch to `build/demo`.

Play it in a browser:

```sh
scripts/build_web.sh                          # crest.wasm + build/site
python3 -m http.server -d build/site 8765     # then open http://localhost:8765
```

To share it, send `build/crest.html` (about 7 MB, made by `scripts/build.sh` and
`scripts/build_web.sh`). It's the whole synth in one file that opens from disk, with no server. It
has only the Crest patches, even when your local build has Peak banks built in, but loading your own
Peak `.syx` still works. The build also puts it on the `pages` branch as `index.html` for GitHub Pages;
`scripts/pages.sh --push` publishes it.

Preview the on-device cards and pages as PNGs: `node tests/device/test_device_ui.mjs --png build/device_preview`.

## Release
Bump `version` in `src/module.json` (the only version string) and commit it, then:

```sh
scripts/build.sh --release
gh release create v0.1.0 crest-module.tar.gz --title "Crest 0.1.0" --generate-notes
scripts/build.sh            # rebuild your own package with the local Peak banks again
```

`--release` leaves `presets/peak/` out of `dsp.so` and the package, then checks the tarball: only the
Crest bank in its tables, no Peak patch name in any file (`dsp.so` included), no build paths and no
macOS metadata. It fails rather than packaging otherwise.

## Install on a Move
Add an SSH key at `http://move.local/development/ssh`, then:

```sh
scripts/deploy.sh            # stages the package on move.local and renames it into place
scripts/logs.sh              # follows Schwung's debug log
```

## Peak banks
Novation offers Peak patch banks as `.syx` downloads at <https://components.novationmusic.com/peak/banks/>.
Crest converts them with the same rules as its built-in banks ([plan/18](plan/18-peak-import.md)), in three ways:

- **On the Move, no rebuild.** Copy the `.syx` files to `/data/UserData/UserLibrary/Crest` (subfolders are fine),
  then on the *Peak Banks* page pick a file with *Peak Bank* and a patch with *Peak Patch*. Each patch's loudness
  is levelled in the background after the bank loads.

  ```sh
  ssh -4 move.local 'mkdir -p /data/UserData/UserLibrary/Crest'
  scp -4 *.syx move.local:/data/UserData/UserLibrary/Crest/
  ```
- **In the browser build.** Patches → *Load Peak .syx…*; the bank is kept in the browser's storage.
- **Built into your own factory set, locally.** Put the files in `reference_novation/` and run the pipeline in
  [plan/18](plan/18-peak-import.md#pipeline): `tools/peak/import_banks.py`, the loudness pass
  (`tools/peak/level.py`), `tools/gen/gen.py`, then `scripts/build.sh`. The converted banks land in
  `presets/peak/`, and `gen.py` writes the bank sources that include them to `build/peak/`, which the build
  compiles into your `dsp.so`, package and `build/site` in place of the committed Crest-only ones. None of it
  is committed. `tests/peak/test_import.py` checks the counts of the author's set (8 banks, 529 unique
  patches), so update those numbers when you import different banks.

## Layout
```
plan/                  specification and roadmap (start with plan/README.md)
src/module.json help.json
src/cards.js canvas.js  on-device knob cards and pages (Schwung card_script / canvas)
src/web_ui.html src/web/ the web UI (Remote UI on a Move, or standalone with web/ and crest.wasm)
web/                   the browser engine host: wasm_host.c, AudioWorklet, engine.js, embed.js (single file)
src/dsp/               the plugin: crest.c (entry, MIDI, render), voice.c, osc.c, filter.c, env.c, fx.c, params.c
src/dsp/*_gen.*        generated by tools/gen (do not edit)
tools/gen/params.py    every parameter, enum and Move page in one place
presets/factory/*.json factory patches (only the keys that differ from INIT)
presets/peak/*.json    patches converted from your Peak SysEx banks (tools/peak/; local, not committed)
tests/                 contract validator, native tests / benchmark / WAV renderer, device and web UI tests
scripts/               build, deploy, symbol check
```

## Third-party preset banks
Crest doesn't ship any Peak banks: Novation's factory banks and the community banks have their own
authors, and their redistribution rights are unclear. `reference_novation/`, `presets/peak/` and
`build/peak/` stay local and git-ignored (the Peak import tests skip without them). A package built
with them is for your own Move; get the authors' consent before sharing it, or remove `presets/peak/`
and rebuild.

## Licence
MIT. The effect algorithms are adapted from Amino (MIT), by the same author.
