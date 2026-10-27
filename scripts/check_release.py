#!/usr/bin/env python3
"""Check a module package before it's published (scripts/build.sh --release runs it).

    python3 scripts/check_release.py crest-module.tar.gz

Fails if the package would ship anything of the Peak banks (their rights are unclear, see the
README) or of the build machine:
  * the bank tables in canvas.js and assets/ui/ui_meta.json hold only the Crest bank, sized to
    Init Saw plus presets/factory,
  * no file, dsp.so included, contains a patch name that only the Peak banks have (needs the local
    presets/peak to know the names; without it this part is skipped, and the bank check still runs),
  * no file contains a build path, and there are no .DS_Store or ._ AppleDouble entries.
"""
import glob
import json
import os
import re
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_single import peak_names  # noqa: E402


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_release.py PACKAGE.tar.gz")
    files = {}
    with tarfile.open(sys.argv[1], "r:gz") as tar:
        for m in tar.getmembers():
            base = os.path.basename(m.name)
            assert base != ".DS_Store" and not base.startswith("._"), "macOS metadata in the package: " + m.name
            if m.isfile():
                files[m.name] = tar.extractfile(m).read()

    want = [["Crest", 0, 1 + len(glob.glob(str(ROOT / "presets" / "factory" / "*.json")))]]
    canvas = files.get("crest/canvas.js")
    meta = files.get("crest/assets/ui/ui_meta.json")
    assert canvas and meta and "crest/dsp.so" in files and "crest/module.json" in files, "incomplete package"
    m = re.search(rb'var META = (\{.*?\});\n', canvas)
    assert m, "canvas.js has no GENERATED META block"
    banks = json.loads(m[1])["banks"]
    assert banks == want, "canvas.js banks are %s, want %s: build with --release" % (banks, want)
    meta = json.loads(meta)
    assert meta["banks"] == want and len(meta["names"]) == want[0][2], \
        "ui_meta.json has %s: build with --release" % [b[0] for b in meta["banks"]]

    names = peak_names()
    leaks = set()
    for name, data in files.items():
        for n in names:
            if n.encode() in data or n.encode("latin1", "replace") in data:
                leaks.add("%s in %s" % (n, name))
        assert str(ROOT).encode() not in data and b"/Users/" not in data, "a build path in " + name

    assert not leaks, "Peak patch names in the package: " + ", ".join(sorted(leaks)[:5])
    version = json.loads(files["crest/module.json"])["version"]
    print("release package ok: version %s, %d files, %d patches (%s)" % (
        version, len(files), want[0][2],
        "%d Peak names checked" % len(names) if names else "no presets/peak: Peak name scan skipped"))


if __name__ == "__main__":
    main()
