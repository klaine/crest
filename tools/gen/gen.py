#!/usr/bin/env python3
"""Generate Crest's parameter tables, UI contract and factory bank from tools/gen/params.py.

  python3 tools/gen/gen.py          write the generated files
  python3 tools/gen/gen.py --check  exit 1 if any generated file is stale
  python3 tools/gen/gen.py --no-peak  build/web/: the four bank-dependent files without the Peak banks
  python3 tools/gen/gen.py --release  write the files as if presets/peak were empty (and drop build/peak/)

Outputs:
  src/dsp/params_gen.h, params_gen.c   descriptor table + P_* ids
  src/dsp/contract_gen.c               chain_params and ui_hierarchy strings
  src/dsp/factory_presets.c            compiled factory bank (Crest patches only)
  build/chain_params.json, build/ui_hierarchy.json   pretty copies for the validator
  build/ui/ui_meta.json, build/ui/ui_presets.json    metadata and patch bank for the web UI (src/web)
  src/cards.js, src/canvas.js                        the GENERATED META blocks of the on-device scripts
  build/peak/factory_presets.c, contract_gen.c, canvas.js
                                       the same with the local Peak banks, when presets/peak has any
"""
import re
import glob
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import params as PM  # noqa: E402

BY_KEY = {p["key"]: p for p in PM.PARAMS}
assert len(BY_KEY) == len(PM.PARAMS), "duplicate key"


def cname(key):
    return "P_" + key.upper()


def fnum(x):
    """Stable short number formatting for JSON/C."""
    if isinstance(x, int) or float(x).is_integer():
        return int(x)
    return round(float(x), 4)


def cfloat(x):
    s = repr(float(x))
    return s + "f"


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cchunks(text, width=110):
    out = []
    for i in range(0, len(text), width):
        out.append("    " + cstr(text[i:i + width]))
    return "\n".join(out)


# --- factory presets ----------------------------------------------------------------------------
def parse_value(p, v):
    if p["kind"] == "enum" and isinstance(v, str):
        return p["opts"].index(v)
    v = float(v)
    assert p["mn"] <= v <= p["mx"], "%s=%s out of range" % (p["key"], v)
    return v


BANK_NAMES = {"Peak Factory Ban.syx": "Peak Factory A", "Peak Factory Ban (1).syx": "Peak Factory B",
              "GEOSynths Soundb.syx": "GEOSynths", "Hugo R.A. Paris.syx": "Hugo R.A. Paris",
              "Kuba Kara\u015b Sign.syx": "Kuba Karas", "Mylar Melodies'.syx": "Mylar Melodies",
              "Sean Tyas - I'm.syx": "Sean Tyas", "arc rae  Frag.syx": "arc rae"}
BANKS = []  # [name, first index, count], filled by load_factory


def load_factory(peak=True):
    presets = [("Init Saw", {})]
    BANKS.clear()
    for path in sorted(glob.glob(os.path.join(ROOT, "presets", "factory", "*.json"))):
        with open(path, "r", encoding="utf-8") as f:
            d = json.load(f)
        name = d["name"]
        assert len(name) <= 20, name
        vals = {}
        for k, v in d["values"].items():
            assert k in BY_KEY, "%s: unknown key %s" % (os.path.basename(path), k)
            vals[k] = parse_value(BY_KEY[k], v)
        presets.append((name, vals))
    BANKS.append(["Crest", 0, len(presets)])
    peak_banks = sorted(glob.glob(os.path.join(ROOT, "presets", "peak", "*.json"))) if peak else []
    for path in peak_banks:  # tools/peak/import_banks.py
        with open(path, "r", encoding="utf-8") as f:
            bank = json.load(f)
        src = bank["source"]
        BANKS.append([BANK_NAMES.get(src, os.path.splitext(src)[0]), len(presets), len(bank["presets"])])
        for d in bank["presets"]:
            assert len(d["name"]) <= 20, d["name"]
            vals = {}
            for k, v in d["values"].items():
                assert k in BY_KEY, "%s: unknown key %s" % (os.path.basename(path), k)
                vals[k] = parse_value(BY_KEY[k], v)
            presets.append((d["name"], vals))
    out = []
    for name, vals in presets:
        row = [float(p["default"]) for p in PM.PARAMS]
        for i, p in enumerate(PM.PARAMS):
            if p["key"] in vals:
                row[i] = float(vals[p["key"]])
        out.append((name, row))
    return out


# --- contract -----------------------------------------------------------------------------------
def chain_params(nfactory):
    out = []
    for p in PM.PARAMS:
        if not p["cp"]:
            continue
        e = {"key": p["key"], "name": p["name"], "short_name": p["short"], "type": p["kind"]}
        if p["opts"] is PM.TABLE_NAMES:
            # 75 table names x 3 oscillators would not fit the host's 32 KiB buffer: the knob is an index
            # and its card (src/cards.js) names the table and draws it. The DSP takes the same index.
            e.update(type="int", min=0, max=len(p["opts"]) - 1, step=1)
        elif p["kind"] == "enum":
            e["options"] = list(p["opts"])
            so = PM.short_opts(p["opts"])
            if so != e["options"]:
                e["short_options"] = so
        else:
            e["min"], e["max"] = fnum(p["mn"]), fnum(p["mx"])
            if p["kind"] != "float" or p["step"] != 0.01:  # the host's float step is 0.01 already (as Amino ships)
                e["step"] = fnum(p["step"])
        e["default"] = fnum(p["default"])
        if p["unit"]:
            e["unit"] = p["unit"]
        if p["access"]:
            e["access"] = p["access"]
        if p["viz"]:
            e["viz"] = p["viz"]
        if p["key"] in PM.CARD_KEYS:
            e["card_script"] = "cards.js#crst"
        out.append(e)
    out.append({"key": "preset", "name": "Factory Patch", "type": "int", "min": 0,
                "max": nfactory - 1, "step": 1})
    # The Peak bank file browser (src/dsp/peak_bank.c reads the file on its worker thread).
    out.append({"key": "syx_file", "name": "Peak Bank", "type": "filepath", "root": "/data/UserData/UserLibrary/Crest",
                "filter": ".syx"})
    # Module-drawn pages (src/canvas.js): the home page in front of the root knobs, and the bank browser.
    out.append({"key": "home", "name": "Crest", "type": "canvas", "canvas_script": "canvas.js", "as_page": True,
                "page_first": True, "show_value": False, "extra_keys": ["flt_type", "preset_name"]})
    out.append({"key": "browse", "name": "Patches", "type": "canvas", "canvas_script": "canvas.js",
                "as_page": True, "preset_browser": True, "show_value": False})
    return out


def knob_entry(k):
    if isinstance(k, tuple):
        key, short = k
        return {"key": key, "short_name": short, "label": BY_KEY[key]["name"]}
    return k


def knob_key(k):
    return k[0] if isinstance(k, tuple) else k


def ui_hierarchy():
    levels = {}
    root_params = [{"key": "home"}] + [knob_entry(k) for k in PM.ROOT_KNOBS]
    root_params.append({"level": "factory", "label": "Factory Patches"})
    for lid, label, knobs, extra in PM.LEVELS:
        root_params.append({"level": lid, "label": label})
    levels["root"] = {"label": PM.ROOT_LABEL, "knobs": [knob_key(k) for k in PM.ROOT_KNOBS],
                      "params": root_params}
    levels["factory"] = {"label": "Factory Patches", "list_param": "preset", "count_param": "preset_count",
                         "name_param": "preset_name", "navigate_to": "root", "params": [{"key": "browse"}]}
    for lid, label, knobs, extra in PM.LEVELS:
        if extra:
            levels[lid] = dict({"label": label}, **extra, params=[])
        else:
            levels[lid] = {"label": label, "knobs": [knob_key(k) for k in knobs],
                           "params": [knob_entry(k) for k in knobs]}
    return {"levels": levels}


# --- web UI and on-device script metadata ----------------------------------------------------------
def rhythms():
    text = open(os.path.join(ROOT, "src", "dsp", "rhythms_gen.h"), encoding="utf-8").read()
    return [[int(m, 16), int(n)] for m, n in re.findall(r"\{0x([0-9A-Fa-f]+), (\d+)\}", text)]


def wavetable_previews(frames, samples):
    """Band-limited previews of every wavetable: [table][frame][sample] in -127..127."""
    text = open(os.path.join(ROOT, "src", "dsp", "wavetables.c"), encoding="utf-8").read()
    hdr = open(os.path.join(ROOT, "src", "dsp", "wavetables.h"), encoding="utf-8").read()
    nt, nf, stride = (int(re.search(r"#define %s (\d+)" % k, hdr).group(1)) for k in ("WT_TABLES", "WT_FRAMES", "WT_FRAME_STRIDE"))
    lens = [int(x) for x in re.search(r"WT_LEN\[WT_LEVELS\] = \{([^}]*)\}", text).group(1).split(",")]
    offs = [int(x) for x in re.search(r"WT_LEVEL_OFF\[WT_LEVELS\] = \{([^}]*)\}", text).group(1).split(",")]
    body = text[text.index("WT_DATA[WT_TABLES * WT_FRAMES * WT_FRAME_STRIDE] = {"):]
    data = [int(x) for x in re.findall(r"-?\d+", body[body.index("{"):])]
    lvl = 3  # 128 samples: smooth, still band-limited
    out = []
    for t in range(nt):
        tab = []
        for k in range(frames):
            f = round(k * (nf - 1) / max(1, frames - 1))
            base = (t * nf + f) * stride + offs[lvl]
            row = [data[base + (i * lens[lvl]) // samples] for i in range(samples)]
            tab.append([max(-127, min(127, round(v * 127 / 32767))) for v in row])
        out.append(tab)
    return out


def disp(p):
    if p["key"] in PM.DISP:
        return PM.DISP[p["key"]]
    if p["kind"] == "float":
        return ["bip"] if p["mn"] < 0 else ["pct"]
    return None


def ui_meta(presets):
    params = []
    for p in PM.PARAMS:
        e = {"k": p["key"], "n": p["name"], "s": p["short"], "t": p["kind"], "d": fnum(p["default"])}
        if p["kind"] == "enum":
            e["o"] = p["opts"]
        else:
            e["mn"], e["mx"] = fnum(p["mn"]), fnum(p["mx"])
        if p["unit"]:
            e["u"] = p["unit"]
        dp = disp(p)
        if dp:
            e["f"] = dp
        if not p["state"]:
            e["vol"] = 1  # not saved in patches (selectors, proxies)
        params.append(e)
    return {"params": params, "banks": BANKS, "names": [n for n, _ in presets], "rhythms": rhythms(),
            "modSources": PM.MOD_SOURCES, "modDests": PM.MOD_DESTS, "mmSlots": PM.MM_SLOTS,
            "tables": PM.TABLE_NAMES, "wt": wavetable_previews(8, 64)}


def ui_presets(presets):
    """Every factory patch as its difference from INIT: flat [param index into ui_meta params, value, ...]."""
    defaults = [float(p["default"]) for p in PM.PARAMS]
    out = []
    for name, row in presets:
        flat = []  # [param index, value, ...]
        for i, p in enumerate(PM.PARAMS):
            if p["state"] and row[i] != defaults[i]:
                flat += [i, round(row[i], 4) if p["kind"] == "float" else int(row[i])]
        out.append(flat)
    return out


def js_meta(presets):
    """The META objects of src/cards.js and src/canvas.js (device scripts cannot fetch files)."""
    by = {p["key"]: p for p in PM.PARAMS}
    names = {by[k]["name"]: k for k in PM.CARD_KEYS}
    cards = {"keys": names, "disp": {k: disp(by[k]) for k in PM.CARD_KEYS},
             "src": PM.MOD_SOURCE_SHORT, "dst": PM.MOD_DEST_SHORT, "routes": PM.FX_ROUTES,
             "rhythms": rhythms(), "tables": PM.TABLE_NAMES, "wt": wavetable_previews(4, 32)}
    canvas = {"banks": BANKS, "types": PM.FILTER_TYPES}
    return cards, canvas


def fill_block(path, name, obj):
    text = open(path, encoding="utf-8").read()
    a, b = "/* BEGIN GENERATED %s */" % name, "/* END GENERATED %s */" % name
    i, j = text.index(a) + len(a), text.index(b)
    return text[:i] + "\n  var META = " + json.dumps(obj, separators=(",", ":")) + ";\n  " + text[j:]


def minify(o):
    return json.dumps(o, separators=(",", ":"), ensure_ascii=True)


# --- emit ---------------------------------------------------------------------------------------
def emit_header():
    L = ["/* GENERATED by tools/gen/gen.py from tools/gen/params.py. Do not edit. */",
         "#ifndef CREST_PARAMS_GEN_H", "#define CREST_PARAMS_GEN_H", "#include <stdint.h>", "",
         "enum {"]
    for p in PM.PARAMS:
        L.append("    %s," % cname(p["key"]))
    L += ["    P_COUNT", "};", "",
          "enum { PK_FLOAT = 0, PK_INT = 1, PK_ENUM = 2 };", "enum { PF_STATE = 1 };", "",
          "typedef struct {", "    const char *key;", "    uint8_t kind;", "    uint8_t nopts;", "    uint8_t flags; /* PF_STATE: saved in the state blob */",
          "    float mn, mx, def;", "    const char *const *opts;", "} param_desc_t;", "",
          "#define CREST_MOD_SRC_COUNT %d" % len(PM.MOD_SOURCES), "#define CREST_MOD_DST_COUNT %d" % len(PM.MOD_DESTS),
          "extern const char *const CREST_MOD_SRC_SHORT[CREST_MOD_SRC_COUNT];",
          "extern const char *const CREST_MOD_DST_SHORT[CREST_MOD_DST_COUNT];", "",
          "extern const param_desc_t CREST_PARAMS[P_COUNT];",
          "extern const uint16_t CREST_PARAMS_SORTED[P_COUNT]; /* ids ordered by strcmp(key) */", "",
          "extern const char CREST_CHAIN_PARAMS[];", "extern const int CREST_CHAIN_PARAMS_LEN;",
          "extern const char CREST_UI_HIERARCHY[];", "extern const int CREST_UI_HIERARCHY_LEN;", "",
          "extern const int CREST_FACTORY_COUNT;", "extern const char *const CREST_FACTORY_NAMES[];",
          "extern const float CREST_FACTORY_VALUES[][P_COUNT];", "", "#endif", ""]
    return "\n".join(L)


def emit_params_c():
    L = ["/* GENERATED by tools/gen/gen.py from tools/gen/params.py. Do not edit. */",
         '#include "params_gen.h"', ""]
    for p in PM.PARAMS:
        if p["kind"] == "enum":
            L.append("static const char *const OPTS_%s[] = {%s};" % (
                p["key"].upper(), ", ".join(cstr(o) for o in p["opts"])))
    L += ["", "const param_desc_t CREST_PARAMS[P_COUNT] = {"]
    for p in PM.PARAMS:
        kind = {"float": "PK_FLOAT", "int": "PK_INT", "enum": "PK_ENUM"}[p["kind"]]
        opts = "OPTS_%s" % p["key"].upper() if p["kind"] == "enum" else "0"
        n = len(p["opts"]) if p["kind"] == "enum" else 0
        L.append("    [%s] = {%s, %s, %d, %s, %s, %s, %s, %s}," % (
            cname(p["key"]), cstr(p["key"]), kind, n, "PF_STATE" if p["state"] else "0",
            cfloat(p["mn"]), cfloat(p["mx"]), cfloat(p["default"]), opts))
    L += ["};", ""]
    L.append("const char *const CREST_MOD_SRC_SHORT[CREST_MOD_SRC_COUNT] = {%s};" % ", ".join(cstr(x) for x in PM.MOD_SOURCE_SHORT))
    L.append("const char *const CREST_MOD_DST_SHORT[CREST_MOD_DST_COUNT] = {%s};" % ", ".join(cstr(x) for x in PM.MOD_DEST_SHORT))
    L += ["", "const uint16_t CREST_PARAMS_SORTED[P_COUNT] = {"]
    order = sorted(range(len(PM.PARAMS)), key=lambda i: PM.PARAMS[i]["key"])
    L.append("    " + ", ".join(cname(PM.PARAMS[i]["key"]) for i in order))
    L += ["};", ""]
    return "\n".join(L)


def emit_contract_c(cp, uh):
    L = ["/* GENERATED by tools/gen/gen.py from tools/gen/params.py. Do not edit. */",
         '#include "params_gen.h"', "",
         "const char CREST_CHAIN_PARAMS[] =", cchunks(cp) + ";",
         "const int CREST_CHAIN_PARAMS_LEN = %d;" % len(cp), "",
         "const char CREST_UI_HIERARCHY[] =", cchunks(uh) + ";",
         "const int CREST_UI_HIERARCHY_LEN = %d;" % len(uh), ""]
    return "\n".join(L)


def emit_factory_c(presets):
    L = ["/* GENERATED by tools/gen/gen.py from the factory preset JSON files. Do not edit. */",
         '#include "params_gen.h"', "", "const int CREST_FACTORY_COUNT = %d;" % len(presets), "",
         "const char *const CREST_FACTORY_NAMES[] = {"]
    for name, _ in presets:
        L.append("    %s," % cstr(name))
    L += ["};", "", "const float CREST_FACTORY_VALUES[][P_COUNT] = {"]
    for name, row in presets:
        L.append("    {  /* %s */" % name)
        for i in range(0, len(row), 6):
            L.append("        " + ", ".join(cfloat(v) for v in row[i:i + 6]) + ",")
        L.append("    },")
    L += ["};", ""]
    return "\n".join(L)


def generate(peak):
    """Every generated file, keyed by its path in a build with (peak) or without the Peak banks."""
    presets = load_factory(peak=peak)
    cp_obj = chain_params(len(presets))
    uh_obj = ui_hierarchy()
    cp, uh = minify(cp_obj), minify(uh_obj)
    files = {
        "src/dsp/params_gen.h": emit_header(),
        "src/dsp/params_gen.c": emit_params_c(),
        "src/dsp/contract_gen.c": emit_contract_c(cp, uh),
        "src/dsp/factory_presets.c": emit_factory_c(presets),
        "build/chain_params.json": json.dumps(cp_obj, indent=1) + "\n",
        "build/ui_hierarchy.json": json.dumps(uh_obj, indent=1) + "\n",
        "build/ui/ui_meta.json": json.dumps(ui_meta(presets), separators=(",", ":")) + "\n",
        "build/ui/ui_presets.json": json.dumps(ui_presets(presets), separators=(",", ":")) + "\n",
    }
    cards_meta, canvas_meta = js_meta(presets)
    for rel, obj in (("src/cards.js", cards_meta), ("src/canvas.js", canvas_meta)):
        if os.path.exists(os.path.join(ROOT, rel)):
            files[rel] = fill_block(os.path.join(ROOT, rel), "META", obj)
    return files, len(presets), len(cp), len(uh)


def main():
    # The committed files never hold the imported Peak banks: they are local only (presets/peak is
    # git-ignored). When presets/peak has banks, the bank-dependent sources that include them go to
    # build/peak/, which scripts/build.sh and build_web.sh compile and package in place of src/'s,
    # and build/ gets the matching contract and UI data.
    # --no-peak: the shareable single file (scripts/build_web.sh) writes its Peak-free copies to build/web/.
    # --release: ignore presets/peak (scripts/build.sh --release), so build/ matches the committed files.
    no_peak = "--no-peak" in sys.argv
    files, npresets, ncp, nuh = generate(peak=False)
    if no_peak:
        files = {"build/web/" + os.path.basename(rel): files[rel] for rel in (
            "src/dsp/factory_presets.c", "src/dsp/contract_gen.c", "build/ui/ui_meta.json", "build/ui/ui_presets.json")}
    elif glob.glob(os.path.join(ROOT, "presets", "peak", "*.json")) and "--release" not in sys.argv:
        local, npresets, ncp, nuh = generate(peak=True)
        for rel, text in local.items():
            if rel.startswith("build/"):
                files[rel] = text
            elif rel in ("src/dsp/factory_presets.c", "src/dsp/contract_gen.c", "src/canvas.js"):
                files["build/peak/" + os.path.basename(rel)] = text
    elif "--check" not in sys.argv:
        shutil.rmtree(os.path.join(ROOT, "build", "peak"), ignore_errors=True)  # no banks, or a release: drop the copies
    stale = []
    for rel, text in files.items():
        path = os.path.join(ROOT, rel)
        if "--check" in sys.argv:
            if rel.startswith("build/"):
                continue
            cur = open(path, "r", encoding="utf-8").read() if os.path.exists(path) else None
            if cur != text:
                stale.append(rel)
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
    if "--check" in sys.argv:
        if stale:
            print("stale generated files: " + ", ".join(stale) + " (run tools/gen/gen.py)", file=sys.stderr)
            sys.exit(1)
        print("generated files are up to date")
    else:
        print("generated %d params, %d factory presets; chain_params %d B, ui_hierarchy %d B" % (
            len(PM.PARAMS), npresets, ncp, nuh))


if __name__ == "__main__":
    main()
