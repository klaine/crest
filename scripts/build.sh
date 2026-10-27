#!/bin/sh
# Build Crest with zig cc: codegen check, contract validation, native tests, the aarch64
# dsp.so, crest-module.tar.gz and build/crest.html (the browser synth in one shareable file,
# without the Peak banks: scripts/build_web.sh --single). dsp.so and the package include the Peak
# banks only when presets/peak has them, which stays local (build/peak/). crest.html also goes on
# the local `pages` branch as index.html (scripts/pages.sh). Run from anywhere:
#
#   scripts/build.sh            # checks + tests + device build + package
#   scripts/build.sh --gen      # run tools/gen/gen.py first
#   scripts/build.sh --no-test  # skip the native tests
#   scripts/build.sh --profile  # dsp.so with the cpu_us debug key (not for release)
#   scripts/build.sh --bench    # also build and run the native CPU benchmark
#   scripts/build.sh --render   # also write one demo WAV per factory patch to build/demo
#   scripts/build.sh --release  # package without the local Peak banks, checked (scripts/check_release.py)
#
# Needs zig (brew install zig) and python3. Set ZIG=/path/to/zig to override the lookup,
# CREST_BLEP_POINTS=4 to build the 4-point polyBLEP variant (plan/04 section 3.1).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

GEN=0
TEST=1
PROFILE=0
BENCH=0
RENDER=0
RELEASE=0
for arg in "$@"; do
  case "$arg" in
    --gen) GEN=1 ;;
    --no-test) TEST=0 ;;
    --profile) PROFILE=1 ;;
    --bench) BENCH=1 ;;
    --render) RENDER=1 ;;
    --release) RELEASE=1 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done
if [ "$RELEASE" = 1 ] && [ "$PROFILE" = 1 ]; then
  echo "--profile is not for release" >&2
  exit 2
fi

PY=${PYTHON:-python3}
BUILD="$ROOT/build"
MODULE="$ROOT/modules/sound_generators/crest"
BLEP=${CREST_BLEP_POINTS:-2}
mkdir -p "$BUILD" "$MODULE"

find_zig() {
  if [ -n "${ZIG:-}" ]; then echo "$ZIG"; return; fi
  if command -v zig >/dev/null 2>&1; then command -v zig; return; fi
  "$PY" -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "zig"))' 2>/dev/null || true
}
ZIG=$(find_zig)
if [ -z "$ZIG" ] || [ ! -x "$ZIG" ]; then
  echo "zig not found: brew install zig (or set ZIG=...)" >&2
  exit 1
fi
echo "== zig $("$ZIG" version), polyBLEP points: $BLEP"

# Same math flags on the device and in the tests: -ffast-math without assuming finite values, so
# the NaN guard in render_block keeps working.
WARN="-Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wno-unused-parameter"
MATH="-ffast-math -fno-finite-math-only"
DEFS="-DCREST_BLEP_POINTS=$BLEP"
[ "$PROFILE" = 1 ] && DEFS="$DEFS -DCREST_PROFILE"
# --release: gen.py ignores presets/peak and drops build/peak/, so nothing below sees the Peak banks.
GENARGS=""
[ "$RELEASE" = 1 ] && GENARGS="--release"

if [ "$GEN" = 1 ]; then
  echo "== gen"
  "$PY" tools/gen/gen.py $GENARGS
else
  "$PY" tools/gen/gen.py --check
  # build/ is not committed: write its contract and UI data, and build/peak/ when presets/peak has banks.
  "$PY" tools/gen/gen.py $GENARGS >/dev/null
fi

# The local Peak banks: gen.py writes the bank-dependent sources that include them to build/peak/,
# compiled and packaged here in place of the committed (Crest-only) ones.
DSP_SRC=""
for f in src/dsp/*.c; do
  case "$f" in */factory_presets.c|*/contract_gen.c) [ -d build/peak ] && f="build/peak/${f##*/}" ;; esac
  DSP_SRC="$DSP_SRC $f"
done
CANVAS=src/canvas.js
[ -d build/peak ] && CANVAS=build/peak/canvas.js
if [ -d build/peak ]; then
  echo "== local Peak banks: build/peak/"
elif [ "$RELEASE" = 1 ]; then
  echo "== release: Crest patches only"
else
  echo "== Crest patches only (no presets/peak)"
fi

if [ "$TEST" = 1 ]; then
  echo "== contract"
  "$PY" tests/contract/validate_contract.py
  # the C twin of the Peak converter (browser and Move), compared with the Python one by test_import.py
  "$ZIG" cc -std=c11 -O1 $WARN $MATH -I src/dsp -o "$BUILD/crest_syx2json" \
    src/dsp/peak_import.c src/dsp/params_gen.c tests/host/syx2json.c -lm
  "$PY" tests/peak/test_import.py
  if command -v node >/dev/null 2>&1; then
    node tests/device/test_device_ui.mjs
    node tests/web/test_web_ui.mjs
  else
    echo "== device/web ui tests: skipped (node missing)"
  fi

  echo "== native tests"
  SAN="-fsanitize=undefined -fno-sanitize-recover=undefined"
  "$ZIG" cc -std=c11 -O1 -g $SAN $WARN $MATH $DEFS -I src/dsp \
    -o "$BUILD/crest_test" $DSP_SRC tests/host/test_main.c -lm
  "$BUILD/crest_test"

  if [ "$BENCH" = 1 ]; then
    echo "== bench"
    "$ZIG" cc -std=c11 -O2 $WARN $MATH $DEFS -I src/dsp \
      -o "$BUILD/crest_bench" $DSP_SRC tests/host/bench_main.c -lm
    "$BUILD/crest_bench"
  fi
fi

if [ "$RENDER" = 1 ]; then
  echo "== demo renders"
  mkdir -p "$BUILD/demo"
  "$ZIG" cc -std=c11 -O2 $WARN $MATH $DEFS -I src/dsp \
    -o "$BUILD/crest_render" $DSP_SRC tests/host/render_wav.c -lm
  "$BUILD/crest_render" "$BUILD/demo"
fi

echo "== dsp.so (aarch64-linux-gnu)"
"$ZIG" cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a72 -std=c11 -O2 $WARN $MATH $DEFS \
  -fPIC -shared -fvisibility=hidden -s -I src/dsp \
  -o "$MODULE/dsp.so" $DSP_SRC -lm -lpthread  # pthread: the Peak bank worker (glibc 2.28 keeps it apart)
rm -f "$MODULE/dsp.so.o"

echo "== check dsp.so"
"$PY" scripts/check_so.py "$MODULE/dsp.so"

cp src/module.json src/help.json src/cards.js "$CANVAS" src/web_ui.html LICENSE "$MODULE/"
# Remote UI (Schwung's browser view of the slot): the app and its generated data
rm -rf "$MODULE/assets"
mkdir -p "$MODULE/assets/ui"
cp src/web/*.js src/web/style.css build/ui/ui_meta.json build/ui/ui_presets.json "$MODULE/assets/ui/"

echo "== package"
# COPYFILE_DISABLE keeps macOS tar from adding ._ AppleDouble files
COPYFILE_DISABLE=1 tar -czf "$ROOT/crest-module.tar.gz" --exclude '.DS_Store' --exclude '.gitkeep' \
  -C "$ROOT/modules/sound_generators" crest
if [ "$RELEASE" = 1 ]; then
  echo "== check release package"
  "$PY" scripts/check_release.py "$ROOT/crest-module.tar.gz"
fi

[ "$TEST" = 1 ] && WEBARGS="--single" || WEBARGS="--single --no-test"
ZIG="$ZIG" PYTHON="$PY" sh scripts/build_web.sh $WEBARGS
sh scripts/pages.sh   # build/crest.html as index.html on the local pages branch (push: scripts/pages.sh --push)

ls -l "$MODULE/dsp.so" "$ROOT/crest-module.tar.gz" "$BUILD/crest.html"
echo "built $ROOT/crest-module.tar.gz and $BUILD/crest.html"
