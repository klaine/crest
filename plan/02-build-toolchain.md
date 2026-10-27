# 02 — Build Toolchain (Zig as the C compiler)

All C code is plain **C11** and is compiled with **`zig cc`**, for the device, for native tests and for WASM. That gives one hermetic toolchain on macOS, Linux and Windows, with no Docker or aarch64 GCC. Zig 0.17.0 is installed at `/opt/homebrew/bin/zig`; pin the minimum in `scripts/build.sh` with `zig version` ≥ 0.13. Zig source is not used. Zig is only the compiler and linker driver.

## 1. Targets
| Target | Command (core flags) | Output |
|---|---|---|
| Device | `zig cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a72 -std=c11 -O2 -ffast-math -fno-finite-math-only -fPIC -shared -fvisibility=hidden` | `modules/sound_generators/crest/dsp.so` |
| Native test (host) | `zig cc -std=c11 -O0 -g -fsanitize=undefined,address` (macOS arm64) | `build/crest_test` |
| Native golden/bench | `zig cc -std=c11 -O2` | `build/crest_golden`, `build/crest_bench` |
| Native preview lib | `zig cc -std=c11 -O2 -fPIC -shared` | `build/libcrest_host.dylib` |
| Browser | `zig cc -target wasm32-wasi -mexec-model=reactor -O2 -DCREST_WASM` | `build/site/crest.wasm` |

Notes:
- **glibc version.** `aarch64-linux-gnu.2.28` pins the glibc symbol version so the `.so` loads on Move's older glibc. **VERIFY** Move's glibc with `ldd --version` over SSH and adjust. Amino's unversioned `aarch64-linux-gnu` works today, so this is a safety margin.
- **Warnings.** Every target uses `-Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wno-unused-parameter` (`-Wdouble-promotion` catches accidental `double` maths in hot loops).
- **Fast-math.** `-ffast-math -fno-finite-math-only` keeps NaN checks (`isnan`) working while allowing reassociation and FMA.
- **Exports.** `-fvisibility=hidden`, with `move_plugin_init_v2` and future optional exports marked `__attribute__((visibility("default")))` through a `CREST_EXPORT` macro.
- **Link flags.** Use `-lm` only. `-ldl` is not needed.
- **Stray objects.** Zig sometimes leaves `dsp.so.o` next to the output; the build script deletes it (Amino does the same).
- **Sanitizers.** ASan/UBSan under `zig cc` on macOS: if a Zig version lacks ASan for the host, fall back to `-fsanitize=undefined -fsanitize-trap=undefined`.

## 2. `scripts/build.sh`
Adapted from Amino's `scripts/build.sh`, with `$CC` replaced by `"$ZIG" cc` everywhere.

```
scripts/build.sh              # gen check → contract → native tests → golden → dsp.so → package
scripts/build.sh --gen        # run tools/gen/gen.py first
scripts/build.sh --no-test    # device build + package only
scripts/build.sh --profile    # adds -DCREST_PROFILE (cpu_us key) to dsp.so
scripts/build.sh --bench      # build and run build/crest_bench (native worst-case timing)
```
Steps:
1. `find_zig`: use `$ZIG`, then `PATH`, then `python -m ziglang` (Amino pattern). Fail with "brew install zig".
2. `--gen`: `python3 tools/gen/gen.py`. Without it, run `gen.py --check`, which fails if generated files are stale.
3. Contract: `python3 tests/contract/validate_contract.py`.
4. Native tests: `zig cc … src/dsp/*.c tests/host/test_main.c -o build/crest_test && build/crest_test`.
5. Golden: build `tests/golden/golden_main.c`, then run `python3 tests/golden/check_golden.py build/crest_golden`.
6. Device `dsp.so`, then `file`/`llvm-readelf` check: `readelf -h dsp.so | grep AArch64` (skipped if readelf is missing).
7. Symbol check: `nm -D dsp.so` must show `move_plugin_init_v2` and no undefined symbols beyond libc/libm.
8. Package:
   ```
   COPYFILE_DISABLE=1 tar -czf crest-module.tar.gz --exclude .DS_Store -C modules/sound_generators crest
   ```

`modules/sound_generators/crest/` is a build output directory. The build copies `src/module.json`, `src/help.json`, `LICENSE` and (M6) `src/web_ui.html` into it. It is git-ignored except for `.gitkeep`.

## 3. `scripts/build_web.sh`
Adapted from Amino:
1. Compile `src/dsp/*.c` and `web/wasm_host.c` to `build/site/crest.wasm`.
2. Copy `web/` (an AudioWorklet player, WebMIDI and a page UI driven by `chain_params` / `ui_hierarchy`) into `build/site/`.
3. Serve it with `python3 -m http.server -d build/site`.

`-DCREST_WASM` disables the `host_api_v1_t` `+120` static assert, which does not hold on wasm32 because pointers are 4 bytes.

## 4. Deploy
`scripts/deploy.sh [host]` (default `move.local`). Requires an SSH key added at `http://move.local/development/ssh`.

```
scp crest-module.tar.gz root@$HOST:/tmp/
ssh root@$HOST 'tar -xzf /tmp/crest-module.tar.gz -C /data/UserData/schwung/modules/sound_generators \
  && chown -R ableton:users /data/UserData/schwung/modules/sound_generators/crest'
```
Then rescan modules in Schwung Manager (`http://move.local:7700`) or restart Schwung. Alternatively, upload the tarball through Schwung Manager. **VERIFY** whether there is a CLI rescan (e.g. a signal to the host), and add it to `deploy.sh` if so.

`scripts/logs.sh`: `ssh root@move.local tail -f /data/UserData/schwung/debug.log` (**VERIFY** the path).

## 5. Repository layout
```
Crest for Move/
  plan/                     these documents
  docs/                     vendored Schwung refs (_schwung_modules.md, _schwung_api.md, _schwung_param_pages.md)
  src/
    module.json  help.json  web_ui.html
    dsp/
      crest.c crest.h       entry, instance pool, render loop, master
      voice.c voice.h       allocation, modes, unison, glide, drift
      osc.c osc.h           classic + wavetable oscillators, noise, ring
      filter.c filter.h     SVF multimode, drives
      env.c env.h           DADSR + repeat
      lfo.c lfo.h           LFOs
      mod.c mod.h           matrix, Animate, macros
      arp.c arp.h           arpeggiator
      fx.c fx.h             distortion, chorus, delay, reverb
      params.c params.h     get/set, state, special keys
      dsp_util.h            fast math (tanh, exp2, polyBLEP), smoothing, RNG, FTZ macros
      plugin_api.h move_info.h  vendored
      params_gen.c params_gen.h contract_gen.c factory_presets.c wavetables.c luts_gen.c   (GENERATED)
  tools/
    gen/params.py gen.py    parameter source of truth + generator
    wavegen/wavegen.py      wavetable synthesis + mip-mapping → src/dsp/wavetables.c
    rhythms/rhythms.py      arp rhythm bitmasks → part of params_gen.c
  presets/factory/*.json    one file per factory patch
  tests/
    contract/validate_contract.py
    host/test_main.c bench_main.c
    golden/golden_main.c golden.json check_golden.py refs/*.wav
  web/wasm_host.c index.html worklet.js
  scripts/build.sh build_web.sh deploy.sh logs.sh
  modules/sound_generators/crest/   build output
  LICENSE  README.md  .gitignore
```

Generated files are committed so a fresh checkout builds without Python. `gen.py --check` keeps them honest.

## 6. Git & CI
- `git init` at M0. Branch `main`; work happens on feature branches.
- CI on GitHub Actions (optional, M6): `mlugg/setup-zig` → `scripts/build.sh` → upload `crest-module.tar.gz` as an artifact. Tags `v*` attach the tarball to the release and update `release.json` (Schwung catalog; see `_schwung_modules.md` §release).
