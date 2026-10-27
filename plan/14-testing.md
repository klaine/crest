# 14 — Testing & Verification

All native test binaries are built with `zig cc` ([02](02-build-toolchain.md)) and run from `scripts/build.sh`. A red test blocks packaging.

## 1. Contract validation — `tests/contract/validate_contract.py`
Adapted from Amino's `tests/contract/validate_contract.py`. It reads `build/chain_params.json`, `build/ui_hierarchy.json` and `src/module.json`.

**`chain_params`:**
- valid JSON
- minified size < 30 KiB (hard limit 32 KiB)
- unique keys
- `short_name` ≤ 5 characters
- `short_options` ≤ 4 characters
- enum options ≤ 128
- defaults within range

**Hierarchy:**
- bare `{"levels":…}` object
- every knob and param key exists in `chain_params`, or is a level link
- no selector key in `knobs`
- at most 8 knobs per level
- viz groups contiguous within one row
- at most 4 `visible_if` gates
- every level is reachable from `root`

**`module.json`:**
- < 8 KB
- required fields present
- `dsp == "dsp.so"`
- no `ui_hierarchy`

**`help.json`:**
- has `children`
- lines ≤ 20 characters
- ASCII only

**Codegen freshness:** `gen.py --check` reports no stale generated files.

## 2. Native unit tests — `tests/host/test_main.c`
A single binary that links `src/dsp/*.c` directly and drives the plugin through `move_plugin_init_v2` with a fake `host_api_v1_t` providing a controllable BPM, clock status and beat position.

| Suite | Checks |
|---|---|
| ABI | init returns `api_version 2`. Create 4 instances OK; the 5th returns NULL. Destroy and recreate works. |
| Params | Every CP key: set the min, max and default values, then `get_param` gives back the same value (within 1e-4). Enum by name equals enum by index. Unknown keys are ignored. |
| State | Random patches (fuzz 1000) round-trip `state` byte-identically. Malformed blobs (truncated, garbage, huge) never crash. Unknown keys are tolerated. |
| Presets | All factory presets load. `preset_count` and `preset_name` are consistent. |
| Oscillators | DC, peak, frequency, alias floor ([04 §10](04-oscillators.md)) |
| Filter | Slopes, cutoff accuracy, self-oscillation, NaN fuzz ([05 §7](05-mixer-filter-drive.md)) |
| Env / LFO | Timing, repeats, sync ([06 §3](06-envelopes-lfos.md)) |
| Matrix | Routing maths, proxies, items JSON validity ([07 §7](07-modulation.md)) |
| Voices | Allocation, stealing, modes, unison, sustain ([08 §8](08-voice-management.md)) |
| Arp | Timing and modes against the simulated clock ([09 §6](09-arpeggiator.md)) |
| FX | Delay time, RT60, stability, bypass, incremental clear ([10 §7](10-effects.md)) |
| Realtime | See §4 |

Build flags: `-O0 -g -fsanitize=address,undefined`, falling back to UBSan-trap if ASan is unavailable under zig on macOS. Exit code ≠ 0 on failure, with a summary line `N passed, M failed`.

## 3. Golden audio — `tests/golden/`
- **`golden.json`** lists cases. Each case is `{preset, midi_script, seconds, bpm, seed}`. The cases are:
  - each factory preset playing a C-minor chord for 2 s, then release
  - arp patterns
  - a matrix-heavy patch
  - a worst-case CPU patch
- **`golden_main.c`** renders a case to float WAV. RNG seeds are fixed, so output is deterministic.
- **`check_golden.py`** compares each render with `refs/*.wav`. It passes if the RMS difference is < −80 dBFS and the peak difference is < −60 dBFS. A failure writes the diff WAV to `build/golden/`.
- **Updating references.** Use `--update` to regenerate. Reference updates need a listening check and a note in the commit message.
- **Float differences.** Floating-point results may differ slightly between host arm64 and device arm64. Golden tests run on the host only.

## 4. Realtime-safety checks
- **No forbidden calls.** Link the native test with `-Wl,--wrap=malloc,--wrap=free,--wrap=fopen,--wrap=printf…`, or use interposition on macOS. Interposed functions abort if called while the `in_rt_callback` flag is set, which is on during every plugin entry point. **VERIFY** whether zig's lld supports `--wrap` for the host target. If not, use `nm` on the `dsp.so` undefined symbols as a static check instead.
- **Symbol whitelist.** `nm -D --undefined-only dsp.so` must only show symbols from the whitelist: `memcpy`, `memset`, `memmove`, `strcmp`, `strncmp`, `strlen`, `strtof`, `snprintf`, `sinf`, `cosf`, `tanf`, `expf`, `logf`, `powf`, `exp2f`, `floorf`, `fabsf`, `__stack_chk_*`, and the atomic helpers. The script fails on anything else, e.g. `malloc` or `fopen`.
- **Per-block bounds.** Test hooks count the bytes cleared per block and the loop iterations in the state parser.

## 5. Benchmark — `tests/host/bench_main.c`
- **Workload.** Renders 10 s of the worst-case patch ([03 §8](03-dsp-architecture.md)) with 8 held notes. It reports µs per block (mean, p99, max) in total and per stage (osc, filter, control, fx), using compile-time stage timers under `-DCREST_BENCH`.
- **Budget.** The host M-series Mac is about 2–3× faster than the A72, so the host-side budget is **≤ 300 µs per block**. Calibrate this factor once at M0 with the sine-only build on the device.

## 6. Browser (WASM) build
`scripts/build_web.sh` → `build/site/`. Use it for:
- auditioning patches
- preset authoring ("dump state")
- UI page review: a simulated 128×64 grid rendering the generated `ui_hierarchy`, following Amino's `web/` pages

## 7. On-device verification (each milestone)
1. Run `scripts/build.sh`, then `scripts/deploy.sh`, and rescan in Schwung Manager.
2. Load Crest on a track. Check that it appears in the sound-generator list under the name "Crest", and that `debug.log` has no errors.
3. Play pads: velocity, poly aftertouch (route Poly AT → Cutoff in the matrix), and sustain.
4. Walk every page with the jog. Check labels, viz cards, list pages and the preset browser.
5. Save to My Presets, reload the Set, and confirm the sound and all matrix slots are identical.
6. **CPU.** Build with `--profile`, load the worst-case preset, hold 8 notes with all FX on, and read `cpu_us`. The target is ≤ 900 µs (p99). Repeat with 4 tracks of Crest plus Move's own instruments running, and listen for xruns or clicks.
7. **Transport.** Start and stop Move playback. The arp and synced LFOs/delay follow the tempo, and a tempo change is picked up within one block.
8. **Automation and Scenes.** Record knob automation on Cutoff and a macro, and check Scene recall.

## 8. Listening checklist (sound character)
These are judged by ear against the design intent, not against any hardware recording:
- Saw density at 1.0 sounds wide and smooth with no obvious beating artefacts.
- Driving the filter at high resonance growls without fizz.
- Self-oscillation tracks the keyboard when key tracking is 1.0.
- Pre-OD on a 3-osc stack thickens without mush.
- Wavetable sweeps are click-free.
- No zipper noise when turning any knob fast.
- No clicks on voice steal, preset change, or unison/mode change.
