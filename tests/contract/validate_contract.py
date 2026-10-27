#!/usr/bin/env python3
"""Validate Crest's generated Move contract (plan/14 section 1).

Reads build/chain_params.json and build/ui_hierarchy.json (written by tools/gen/gen.py) plus
src/module.json and src/help.json. Exits non-zero on the first failing group of checks.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
errors = []


def err(msg):
    errors.append(msg)


def load(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return json.load(f)


def minified(o):
    return len(json.dumps(o, separators=(",", ":")))


cp = load("build/chain_params.json")
uh = load("build/ui_hierarchy.json")
mod = load("src/module.json")
help_ = load("src/help.json")

# ---- chain_params -------------------------------------------------------------------------------
if minified(cp) >= 31 * 1024:
    err("chain_params is %d bytes; the limit is 31 KiB (host buffer 32 KiB)" % minified(cp))
keys = [e["key"] for e in cp]
if len(set(keys)) != len(keys):
    err("duplicate chain_params keys")
for e in cp:
    k = e["key"]
    if e["type"] == "filepath" and not e.get("root", "").startswith("/data/UserData"):
        err("%s: a filepath must browse inside /data/UserData" % k)
    if e["type"] not in ("float", "int", "enum", "canvas", "filepath"):
        err("%s: bad type %r" % (k, e["type"]))
    if e["type"] == "canvas":
        script = e.get("canvas_script", "canvas.js").split("#")[0]
        if not os.path.exists(os.path.join(ROOT, "src", script)):
            err("%s: canvas_script %s is not in src/" % (k, script))
        if len(e.get("extra_keys", [])) > 4:
            err("%s: more than 4 extra_keys" % k)
    if "card_script" in e:
        script, _, fn = e["card_script"].partition("#")
        path = os.path.join(ROOT, "src", script)
        if not os.path.exists(path) or ("globalThis.%s =" % fn) not in open(path, encoding="utf-8").read():
            err("%s: card_script %s does not export %s" % (k, script, fn))
    if len(e.get("short_name", "")) > 5:
        err("%s: short_name %r is longer than 5" % (k, e["short_name"]))
    if e["type"] == "enum":
        opts = e["options"]
        if not 1 <= len(opts) <= 128:
            err("%s: %d enum options" % (k, len(opts)))
        so = e.get("short_options")
        if so is not None:
            if len(so) != len(opts):
                err("%s: short_options length mismatch" % k)
            for s in so:
                if len(s) > 4:
                    err("%s: short option %r is longer than 4" % (k, s))
            if len(set(so)) != len(so):
                err("%s: duplicate short options %r" % (k, so))
        if not 0 <= e.get("default", 0) < len(opts):
            err("%s: default out of range" % k)
    elif "min" in e and not e["min"] <= e.get("default", e["min"]) <= e["max"]:
        err("%s: default %r outside [%r, %r]" % (k, e["default"], e["min"], e["max"]))
    viz = e.get("viz")
    if viz and "group" in viz and "role" not in viz:
        err("%s: viz group without role" % k)

# ---- ui_hierarchy -------------------------------------------------------------------------------
levels = uh.get("levels")
if not isinstance(levels, dict) or "root" not in levels:
    err("ui_hierarchy must be a bare {\"levels\": {...}} object with a root level")
    levels = {}
keyset = set(keys)
selectors = set()
for lid, lv in levels.items():
    for sel in ("list_param", "count_param", "name_param", "items_param", "select_param"):
        if sel in lv:
            selectors.add(lv[sel])
reachable = {"root"}
for lid, lv in levels.items():
    knobs = lv.get("knobs", [])
    if len(knobs) > 8:
        err("%s: %d knobs (max 8)" % (lid, len(knobs)))
    for k in knobs:
        if k not in keyset:
            err("%s: knob %r is not in chain_params" % (lid, k))
        if k in selectors:
            err("%s: selector %r must not be a knob" % (lid, k))
    for p in lv.get("params", []):
        if isinstance(p, str):
            if p not in keyset:
                err("%s: param %r is not in chain_params" % (lid, p))
        elif "level" in p:
            if p["level"] not in levels:
                err("%s: link to unknown level %r" % (lid, p["level"]))
            reachable.add(p["level"])
        elif p.get("key") not in keyset:
            err("%s: param %r is not in chain_params" % (lid, p.get("key")))
    for sel in ("list_param", "count_param", "name_param"):
        if sel in lv and sel != "name_param" and lv[sel] not in keyset | {"preset_count"}:
            err("%s: %s %r unknown" % (lid, sel, lv[sel]))
    if "navigate_to" in lv and lv["navigate_to"] not in levels:
        err("%s: navigate_to unknown level" % lid)
for lid in levels:
    if lid not in reachable:
        err("level %r is not reachable from root" % lid)
if minified(cp) + minified(uh) >= 64 * 1024:
    err("chain_params + ui_hierarchy exceed 64 KiB")

# ---- viz groups must be contiguous within one row of 4 --------------------------------------------
by_key = {e["key"]: e for e in cp}
for lid, lv in levels.items():
    knobs = lv.get("knobs", [])
    groups = {}
    for pos, k in enumerate(knobs):
        v = (by_key.get(k) or {}).get("viz") or {}
        if "group" in v and v.get("span", True) is not False:
            groups.setdefault(v["group"], []).append(pos)
    for g, pos in groups.items():
        if len(pos) < 2:
            continue
        if pos != list(range(pos[0], pos[-1] + 1)) or pos[0] // 4 != pos[-1] // 4:
            err("%s: viz group %r is not contiguous within one row (positions %r)" % (lid, g, pos))

# ---- module.json / help.json --------------------------------------------------------------------
raw = open(os.path.join(ROOT, "src/module.json"), "rb").read()
if len(raw) >= 8192:
    err("module.json is %d bytes (limit 8 KB)" % len(raw))
for f in ("id", "name", "version", "api_version"):
    if f not in mod:
        err("module.json: missing %s" % f)
if mod.get("api_version") != 2:
    err("module.json: api_version must be 2")
if mod.get("dsp") != "dsp.so":
    err("module.json: dsp must be dsp.so")
if "ui_hierarchy" in mod:
    err("module.json: a synth's ui_hierarchy is never read from module.json")
if mod.get("component_type") != "sound_generator":
    err("module.json: component_type must be sound_generator")


def walk_help(node, path):
    if "lines" in node:
        for ln in node["lines"]:
            if len(ln) > 20:
                err("help %s: line %r is longer than 20" % (path, ln))
            if not ln.isascii():
                err("help %s: non-ASCII line %r" % (path, ln))
    for c in node.get("children", []):
        walk_help(c, path + "/" + c.get("title", "?"))


if "children" not in help_:
    err("help.json: top level needs children")
walk_help(help_, "help")

if errors:
    print("contract validation FAILED:")
    for e in errors:
        print("  - " + e)
    sys.exit(1)
print("contract ok: %d params (%d B), %d levels (%d B)" % (len(cp), minified(cp), len(levels), minified(uh)))
