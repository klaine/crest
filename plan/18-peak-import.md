# 18 — Peak SysEx import

The committed factory bank has 11 patches: Init Saw and 10 curated. A local build adds the 529 imported
from Novation Peak SysEx banks in `reference_novation/` (540 in all). The conversion runs offline
(`tools/peak/`), its output (`presets/peak/`, `build/peak/`) stays local, and only local packages carry it.

## Pipeline
```sh
python3 tools/peak/import_banks.py        # reference_novation/*.syx -> presets/peak/*.json (keeps patch_vol)
python3 tools/gen/gen.py                  # bank JSON -> factory_presets.c
# build/crest_level is built from tests/host/level_presets.c (see scripts/build.sh --render for the flags)
python3 tools/peak/level.py               # solves patch_vol per patch, rewrites presets/peak/*.json
python3 tools/gen/gen.py
python3 tests/peak/test_import.py         # decoder and converter checks (also run by scripts/build.sh)
```
Duplicates (identical bytes, ignoring the matrix-selector byte) and "Init Patch" entries are dropped:
8 banks x 128 = 1024 patches, 529 unique.

## Message layout
One patch = one 527-byte SysEx message (`F0 ... F7`). Novation does not publish the layout. It was derived
from the NRPN/CC list and the "Init Patch" table in the Peak manual (`reference_novation/`), then checked by
per-byte value statistics over all 1024 patches. Offsets live in `tools/peak/peak_syx.py`.

| Offset | Content |
|---|---|
| 16..31 | Name (ASCII, space padded) |
| 32 | Category |
| 48..61 | Voice mode, unison, detune, spread, octave, glide, pre-glide, divergence, drift, noise LP/HP |
| 62, 83, 104 | Oscillator blocks (21 bytes: wave, shape, shape source, coarse, fine, bend, level of sync/density...) |
| 125..134 | Mixer: osc 1-3, ring, noise (14-bit style pairs, 0..255) |
| 135..153 | Patch level, VCA gain, FX dry/wet, overdrive, post-drive, filter slope/shape/key/res/freq/LFO/Osc3 FM/env depths |
| 160, 169, 177 | Amp, Mod 1, Mod 2 envelopes (A, D, S, R, velocity, MonoTrig) |
| 166/167, 175/176, 183/184 | Hold and Repeats of the Amp, Mod 1 and Mod 2 envelopes (firmware 1.2, inferred) |
| 185, 199 | LFO 1, LFO 2 blocks (14 bytes: range, rate, sync, wave, phase, slew, fade time/mode, FadeSync, MonoTrig, repeats, common) |
| 213..249 | Distortion, FX routing, delay, reverb, chorus |
| 251..261 | Arpeggiator |
| 266..329 | Mod matrix: 16 slots x (src 1, src 2, depth, dest) |
| 330..345 | FX matrix: 4 slots |
| 220 | Delay L/R Ratio, 0..10 (inferred: 0 = 1/1 in 350 patches, the Init value; between Time and Width as in the manual's list) |
| 362..367 | LFO 3 and LFO 4: wave, rate (0..127), sync (0 = Off, n = entry n of the sync table) (inferred) |

**Inferred fields.** The manual's MIDI list predates firmware 1.2 and does not list envelope Hold and
Repeats or the LFO 3/4 settings. They were located from value statistics over the 529 patches:
- Hold bytes hold 0 in nearly all patches and 11..54 in a few (10 patches). Repeats bytes hold small counts
  or 127 (the manual's "On"; 25 patches loop an envelope, 38 use repeats at all).
- Bytes 362..367 hold a wave (0..3), a rate and a sync index, twice. 64 of the 72 patches that route LFO 3 in
  the matrix have them set; most of the rest are 0 (rate 0, sync Off). Sync 17 (the most common non-zero
  value) is "8 beats", the Init Patch's LFO 3/4 sync.
`tests/peak/test_import.py` checks these stay in range.

Byte 137 is the **dry** level and 138 the **wet** level, in the order of the manual's parameter list. The data
agrees: wet is 127 in all but one patch, while dry is lowered (often to 0) in 25 patches, nearly all with effects in
use. One patch ("Gang Leader") has dry 0 and no effect in use, which would be silent; the importer keeps dry and
wet at full whenever no effect level is up and the FX matrix does not move one. Gang Leader's FX matrix brings
the effects in from Animate 1/2 and the mod wheel, so it stays dry-muted until one of those moves, as on the Peak.

## Mapping
Aims at 1:1; every deviation is listed below and counted by `import_banks.py --report`.

| Peak | Crest |
|---|---|
| Osc wave/shape/coarse/fine/level, Mod Env 2 and LFO 2 pitch depth | `oN_*` (coarse range widened to +-36 so Peak's +-1 oct steps map exactly) |
| Shape source Mod Env 1 / LFO 1 + depth | `oN_shsrc`, `oN_shamt` |
| Density, density detune, virtual sync | `oN_dens`, `oN_ddet`, `oN_vsync` |
| Fixed note | `oN_fixed` (0 = off) |
| FM osc 3>1, 1>2, 2>3, noise>1 | `fm_31`, `fm_12`, `fm_23`, `fm_n1` (matrix destinations too) |
| Filter Osc 3 / noise FM | `flt_o3`, `flt_ns` |
| Wavetable (60 Peak tables) | `oN_table`: the Crest table of the same name, 1:1 (see below) |
| Mod Env 2 pitch depth (linear, 8 steps = 1 octave) | `oN_penv` (cubic law, 96 semitones at full depth) |
| LFO 2 pitch depth (±5 octaves, finer below ±12) | `oN_plfo` (cubic law, 60 semitones at full depth; the Peak value maps linearly onto the knob, so the common vibrato settings 12..21 give 5..27 cents. The Peak's exact taper is unpublished) |
| Filter env depths (Mod Env 1, Amp Env) | `flt_env`, `flt_ae` (cubic law, 96 semitones at full depth) |
| Filter LFO 1 depth (up to eight octaves) | `flt_lfo` (cubic law, 96 semitones at full depth; the Peak value maps linearly onto the knob, so value 16 of 127 gives ±0.2 st and 64 gives ±12 st. The Peak's taper is unpublished) |
| Divergence (voice, filter) | `v_div`, `flt_div` |
| VCA gain | `vca_gain` (a base level; the VcaLevel matrix destination adds to it) |
| Patch level | `patch_vol`, then overridden by the loudness solve |
| Mod matrix, 16 slots | up to 20 slots, 33 sources, 59 destinations |
| FX matrix (4 slots) | `mm` destinations for delay time, reverb decay/damp/HP, chorus rate/depth/feedback |
| FX routing (7 orders) | `fx_route` (Distortion always first) |
| Delay time (127 = 1.4 s), sync (16 divisions) | `dly_time` on Crest's 10 ms … 2 s law, so 127 lands on 1.4 s (`0.010·140^(v/127)` s; the Peak's curve below the top is unpublished); `dly_sync` the same division |
| Chorus LoPass/HiPass, Reverb LoPass/HiPass/ModDepth/ModRate, Delay L/R Ratio/Width/Slew Rate | `cho_lp/hp`, `rev_lpf/hpf/mdep/mrate`, `dly_ratio/width/slew` (linear onto the knobs; web UI only). Every one sits at the Peak's Init value in most patches, which confirms the bytes |
| Chorus level, Reverb PreDelay | `cho_lvl` (a return added to the dry, as on the Peak), `rev_pre` (linear to 500 ms) |
| Delay feedback, LP Damp, HP Damp | `dly_fb` (linear onto the knob), `dly_lp = 1 − LP Damp/127` (damping is an amount, like HP Damp: 0 = none), `dly_hp` |
| Arpeggiator | `arp_*` (modes, 6 octaves, 33 rhythm patterns, latch, key sync, swing, gate) |
| Envelope A/D/S/R, Hold, Repeats (Off, 1..126, On), Velocity (−64..+63) | `P_a/d/s/r`, `P_hold`, `P_rep` (127 = loop), `P_vel` (−1..1) |
| Envelope MonoTrig (Legato / Re-Trig) | `P_trig` `Single` / `Multi` |
| LFO 1/2 range Low / High / Sync, rate, sync table (35) | `lN_range`, `lN_rate` (same curves, see [06](06-envelopes-lfos.md)), `lN_sync` (the same 35 entries) |
| LFO 1/2 wave, phase, slew, repeats, fade time and mode | `lN_wave`, `lN_trig` Key + `lN_phase`, `lN_slew`, `lN_rep`, `lN_fade`, `lN_fmode` |
| LFO 1/2 MonoTrig, FadeSync, Common | `lN_mtrig`, `lN_fsync`, `lN_trig` Common (CommonKey when a fixed phase is set too) |
| LFO 3/4 wave, rate, sync | `l3_*`, `l4_*` (wide rate range) |

## Not representable (patch counts from the importer)
- LFO rate above the 1.24 kHz control-rate cap: 4 patches (High range near the top; the knob still reads the Peak's rate)
- Different bend range per oscillator: 12 (one global range)
- Pre-glide: 7
- Matrix Ns>FiltF with an FM/other source: 6; a few more with sources Crest does not have

The Peak's rate curves are not published. Crest's Low range is an exponential 0.02..200 Hz over the knob and High
is 8× that, so a Peak rate value lands on the same knob position; how close the Hz are to the hardware is
unverified. The same goes for Hold (taken as linear to 500 ms) and Fade Time (linear to 10 s).

The 33 rhythm patterns in `arp_rhy` are Crest's own (Euclidean and hand-made), not Peak's; patches that
use one get the nearest by density.

## Wavetables
The Peak's 60 wavetables are Novation's sampled audio and are not in the SysEx banks, so Crest cannot use
them. `tools/wavegen/peak_tables.py` designs a Crest table for each Peak name instead (58 new; "Organ" and
"Harsh" use Crest's existing tables), built from the name's character: vowel morphs for the Vox tables,
octave partials for Octaves, a driven sine for Tubey, chord harmonic sets for Chords, and so on. A
converted patch keeps its table name, but the timbre is Crest's approximation, not the Peak's. 261 of the
529 patches use a wavetable. On the Move the table knob is an index (75 names would not fit
`chain_params`); its card shows the name and draws the table.

## Loudness
`patch_vol` is solved per patch by rendering a held chord (or a three-note phrase for mono modes) for 6 s and
scaling to a -3 dBFS peak or -17 dBFS RMS, whichever is quieter. The solve is `src/dsp/level.c`, shared by the
factory tool and the two loaders below. (The factory tool used to render only about 4 of the 6 s, because its
tail loop re-read a length that shrank as it ran; the fix raised 226 RMS-limited patches by up to 0.09.) About 120 patches reach the volume limit
below that target, mostly closed or sparse patches. `#>}} =<>|>` has VCA Gain 0 in the source and is mute; Gang Leader is silent until Animate or the wheel is used.

## Rights
The Peak Factory banks are Novation's. The other six banks (Hugo, Kuba Karas, Mylar, GEOSynths, arc rae,
Sean Tyas) are community banks whose redistribution terms are unclear. They are here for the author's own
testing; check with their authors before publishing the module with them included.

## Loading your own banks
The converter also exists in C, `src/dsp/peak_import.c` (decoder and conversion rules, the twin of
`peak_syx.py` + `import_banks.py`). `tests/peak/test_import.py` runs it on all 1024 reference patches through
`build/crest_syx2json` and checks every value against the Python converter. It accepts any file holding 527-byte
Peak patch messages: a bank dump, a single patch, or a mix (up to 128).

- **Browser build.** The Patches dialog has *Load Peak .syx…*. `web/peak_worker.js` (a Web Worker with its own
  `crest.wasm`) converts the file and adds it as a bank tab at once, then solves each patch's loudness in the
  background (about 15 patches a second on a laptop). Banks are kept in the browser's storage as differences from
  the defaults; × on a tab removes one. Not offered in the Move's Remote UI, which has no engine of its own.
- **On the Move.** The *Peak Banks* page has a *Peak Bank* file browser (Schwung `filepath`, rooted at
  `/data/UserData/UserLibrary/Crest`, `.syx` only, subfolders allowed) and a *Peak Patch* knob (0–127). `src/dsp/peak_bank.c` runs one
  worker thread per process, started from the loader thread, `SCHED_OTHER`, kept off core 3, polling every
  50 ms. It reads and converts the file into static per-slot storage (published under a sequence counter), then
  levels the patches one by one, the selected one first; each solve renders 6 s offline on a pool instance
  (about 0.3 s of CPU on the device, estimated). The audio thread only copies a path and an atomic counter, and
  copies a finished patch out. A patch picked before its level is known plays at its converted level and moves
  to the solved one when it arrives. While no bank is loaded, *Peak Patch* only stores its value, so a Move set
  that restores the key before its bank has loaded keeps the patch it saved. **Not run on a Move yet.**
- **Limits.** The level solve borrows a free instance from the pool of 4; with all four in use it waits. The
  Peak Patch knob shows a number; the patch name appears on the home page once loaded.

