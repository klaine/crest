#!/bin/sh
# Build the web UI and the standalone browser synth:
#
#   scripts/build_web.sh            # crest.wasm, node tests, build/site, build/crest.html
#   scripts/build_web.sh --no-test  # skip the node tests
#   scripts/build_web.sh --single   # only build/crest.html (scripts/build.sh runs this)
#
# The Remote UI (src/web_ui.html + src/web/) is packaged into the Move module by scripts/build.sh.
# This script also compiles src/dsp to WebAssembly and lays out build/site: the same UI driving the
# engine in an AudioWorklet, played from the page, the computer keyboard or Web MIDI.
# Serve it with: python3 -m http.server -d build/site 8765
# build/crest.html is the same synth in one file to share (scripts/build_single.py), built without
# the imported Peak banks: open it from disk, no server needed. build/site has them when presets/peak
# does (local only).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

TEST=1
SINGLE=0
for arg in "$@"; do
  case "$arg" in
    --no-test) TEST=0 ;;
    --single) SINGLE=1 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

PY=${PYTHON:-python3}
ZIG=${ZIG:-$(command -v zig || true)}
if [ -z "$ZIG" ]; then echo "zig not found: brew install zig (or set ZIG=...)" >&2; exit 1; fi

mkdir -p build

# wasm OUT SOURCES...: CREST_WASM drops the device-only 64-bit ABI assert in plugin_api.h.
wasm() {
  out=$1; shift
  "$ZIG" cc -target wasm32-wasi -mexec-model=reactor -DCREST_WASM -std=c11 -O2 -ffast-math -fno-finite-math-only \
    -ffile-prefix-map="$ROOT"=. -I src/dsp -o "$out" "$@" web/wasm_host.c -lm \
    -Wl,--no-entry -Wl,-z,stack-size=1048576 -Wl,--strip-all \
    -Wl,--initial-memory=33554432 -Wl,--max-memory=134217728 -Wl,--export-dynamic
}

single() {
  echo "== single file (no Peak banks)"
  "$PY" tools/gen/gen.py --no-peak >/dev/null   # build/web/: the bank-dependent sources and UI data
  set --
  for f in src/dsp/*.c; do
    case "$f" in */factory_presets.c|*/contract_gen.c) ;; *) set -- "$@" "$f" ;; esac
  done
  wasm build/web/crest.wasm "$@" build/web/factory_presets.c build/web/contract_gen.c
  if [ "$TEST" = 1 ] && command -v node >/dev/null 2>&1; then
    node tests/web/test_wasm.mjs build/web/crest.wasm
  fi
  "$PY" scripts/build_single.py --out build/crest.html
}

if [ "$SINGLE" = 1 ]; then
  single
  exit 0
fi

"$PY" tools/gen/gen.py >/dev/null   # build/ui/*.json, and build/peak/ when presets/peak has banks

# The local Peak banks: their bank-dependent sources in build/peak/ replace the committed ones.
set --
for f in src/dsp/*.c; do
  case "$f" in */factory_presets.c|*/contract_gen.c) [ -d build/peak ] && f="build/peak/${f##*/}" ;; esac
  set -- "$@" "$f"
done
echo "== crest.wasm ($("$ZIG" version))"
wasm build/crest.wasm "$@"

if [ "$TEST" = 1 ]; then
  if command -v node >/dev/null 2>&1; then
    echo "== web tests"
    node tests/web/test_wasm.mjs build/crest.wasm
    node tests/web/test_web_ui.mjs
  else
    echo "== web tests: skipped (node missing)"
  fi
fi

echo "== site"
SITE=build/site
rm -rf "$SITE"
mkdir -p "$SITE/assets/ui" "$SITE/assets/engine"
cp src/web/*.js src/web/style.css build/ui/ui_meta.json build/ui/ui_presets.json "$SITE/assets/ui/"
cp web/engine.js web/worklet.js web/peak_worker.js build/crest.wasm "$SITE/assets/engine/"
# The page is web_ui.html without the device bridge, so it boots the browser engine.
"$PY" - src/web_ui.html "$SITE/index.html" <<'PYEOF'
import sys
src = open(sys.argv[1], encoding="utf-8").read()
tag = '<script src="/static/schwung-remote-api.js"></script>\n'
assert tag in src, "web_ui.html no longer loads /static/schwung-remote-api.js"
open(sys.argv[2], "w", encoding="utf-8").write(src.replace(tag, ""))
PYEOF
touch "$SITE/.nojekyll"
echo "built $SITE ($(du -sh "$SITE" | cut -f1))"

single
