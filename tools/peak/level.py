#!/usr/bin/env python3
"""Solve patch_vol for the imported Peak banks and write it back into presets/peak/*.json.
Needs build/crest_level (tests/host/level_presets.c) built against the current factory presets."""
import glob, json, os, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from import_banks import write_bank  # noqa: E402
files = sorted(glob.glob(ROOT + "/presets/peak/*.json"))
banks = [json.load(open(f)) for f in files]
first = 1 + len(glob.glob(ROOT + "/presets/factory/*.json")) + 0
out = subprocess.run([ROOT + "/build/crest_level", str(first)], capture_output=True, text=True, check=True).stdout
vols = {int(l.split()[0]): float(l.split()[1]) for l in out.splitlines() if l.strip()}
idx = first
n = 0
for f, b in zip(files, banks):
    for p in b["presets"]:
        if idx in vols:
            p["values"]["patch_vol"] = round(vols[idx], 4)
            n += 1
        idx += 1
    write_bank(f, b["source"], b["presets"])
print("leveled", n, "of", idx - first)
