#!/usr/bin/env python3
"""Checks for the Peak SysEx decoder and the bank importer (run by scripts/build.sh)."""
import glob
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "peak"))
sys.path.insert(0, os.path.join(ROOT, "tools", "gen"))
import import_banks as IB  # noqa: E402
import params as PM        # noqa: E402
import peak_syx            # noqa: E402

fails = 0


def check(ok, msg):
    global fails
    if not ok:
        fails += 1
        print("  FAIL", msg)


by_key = {p["key"]: p for p in PM.PARAMS}
syx = sorted(glob.glob(os.path.join(ROOT, "reference_novation", "*.syx")))
# reference_novation/ holds Novation's banks and is not committed: a fresh clone has none.
if not syx:
    print("peak import: (reference_novation/*.syx missing: skipped)")
    sys.exit(0)

n_patches = 0
for path in syx:
    bank = peak_syx.read_bank(path)
    check(len(bank) == 128, "%s has 128 patches" % os.path.basename(path))
    for d in bank:
        check(len(d["name"]) <= 16, "name length")
        v = IB.Conv(d).run()
        n_patches += 1
        for k, x in v.items():
            p = by_key.get(k)
            if p is None:
                check(False, "unknown param %s in %s" % (k, d["name"]))
                continue
            if p["kind"] in ("float", "int"):
                check(p["mn"] - 1e-6 <= x <= p["mx"] + 1e-6, "%s=%r out of range in %s" % (k, x, d["name"]))
            elif p["kind"] == "enum":
                check(x in p["opts"], "%s=%r not an option in %s" % (k, x, d["name"]))
check(n_patches == 8 * 128, "decoded %d patches" % n_patches)

# a bank file must load, hold only known params and only in-range values
banks = sorted(glob.glob(os.path.join(ROOT, "presets", "peak", "*.json")))
total = 0
for f in banks:
    b = json.load(open(f, encoding="utf-8"))
    check(b["source"].endswith(".syx"), "%s source" % os.path.basename(f))
    for p in b["presets"]:
        total += 1
        check(0 < len(p["name"]) <= 20, "preset name %r" % p["name"])
        check(0.0 <= p["values"].get("patch_vol", 0.5) <= 1.0, "patch_vol in range for %s" % p["name"])
check(total == 529, "529 unique imported patches (got %d)" % total)

# known values: the first factory patch bank entry decodes to sensible numbers
d = peak_syx.read_bank(syx[5])[0]
check(0 <= d["f_freq"] <= 255 and 0 <= d["f_res"] <= 127, "filter bytes in range")

# The firmware 1.2 fields (env hold/repeats, LFO 3/4) are inferred from byte statistics (plan/18): their
# values must stay in the manual's ranges, and patches that route LFO 3 should nearly all set it up.
all_d = [d for path in syx for d in peak_syx.read_bank(path)]
check(all(d[e + "_hold"] <= 127 and d[e + "_rep"] <= 127 for d in all_d for e in ("ae", "me1", "me2")), "env hold/repeats in range")
check(all(g["wave"] <= 3 and g["rate"] <= 127 and g["sync"] <= 35 for d in all_d for g in d["glfo"]), "LFO 3/4 bytes in range")
check(sum(1 for d in all_d if any(d[e + "_rep"] == 127 for e in ("ae", "me1", "me2"))) > 10, "some envelopes loop (Repeats On)")
uses = [d for d in all_d if any(m["depth"] != 64 and m["dest"] and {17, 18} & {m["s1"], m["s2"]} for m in d["mm"])]
setup = sum(1 for d in uses if any(d["glfo"][0].values()))
check(uses and setup >= 0.8 * len(uses), "patches routing LFO 3 set it up (%d of %d)" % (setup, len(uses)))
# the converter keeps the Peak's per-envelope MonoTrig: Legato -> Single, Re-Trig -> Multi
v = IB.Conv(all_d[0]).run()
check(v["ae_trig"] == ("Multi" if all_d[0]["ae_trig"] else "Single"), "amp env MonoTrig converted")

# The C converter (src/dsp/peak_import.c, used by the browser build and banks loaded on the Move) must give
# the same values as this one for every patch. build/crest_syx2json is built by scripts/build.sh.
import subprocess  # noqa: E402
tool = os.path.join(ROOT, "build", "crest_syx2json")
if os.path.exists(tool):
    out = subprocess.run([tool] + syx, capture_output=True, text=True, check=True).stdout.splitlines()
    py = [(f, d) for f in range(len(syx)) for d in peak_syx.read_bank(syx[f])]
    check(len(out) == len(py), "C converter found %d patches, Python %d" % (len(out), len(py)))
    worst, bad = 0.0, 0
    for line, (f, d) in zip(out, py):
        c = json.loads(line)
        ref = IB.Conv(d).run()
        for i, p in enumerate(PM.PARAMS):
            want = ref.get(p["key"], p["default"])
            if p["kind"] == "enum" and isinstance(want, str):
                want = p["opts"].index(want)
            diff = abs(float(c["values"][i]) - float(want))
            if diff > 1.5e-4:
                bad += 1
                if bad <= 10:
                    print("  C/Python differ: %s #%d %s: C %s, Python %s" % (os.path.basename(syx[f]), c["index"], p["key"],
                                                                          c["values"][i], want))
        if all(32 <= ord(ch) < 127 for ch in d["name"]):
            check(c["name"] == (d["name"] or "Unnamed"), "name %r vs %r" % (c["name"], d["name"]))
    check(bad == 0, "C and Python converters agree (%d values differ)" % bad)
else:
    print("  (build/crest_syx2json not built: C/Python comparison skipped)")

print("peak import: %d failures" % fails)
sys.exit(1 if fails else 0)
