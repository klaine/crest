# 13 — Presets & State

## 1. Requirements
- **Round trip.** `get_param("state")` returns a complete, self-contained blob of **base** (unmodulated) values, and `set_param("state", blob)` restores it exactly. The host builds My Presets, autosave and Set recall on this.
- **Always answer.** `state` must always be answered.
- **Realtime safety.** Serialise and parse with no allocation or I/O, in bounded time.
- **Compatibility.** Forward/backward tolerant: unknown keys are ignored, and missing keys fall back to INIT defaults.

## 2. Blob format
The blob is flat JSON with string values, the same format as Amino's (`Development/src/dsp/params.c:435-573`):
```json
{"fmt":"crest","v":"1","name":"Init","o1_wave":"2","o1_shape":"0.0000", … ,"mm01_s1":"15","mm01_dst":"20","mm01_dep":"0.2500", …}
```
- **Key order.** Fixed generated order from `params.py`. All ~294 state keys are always written. Default-valued keys are **not** skipped, because skipping them complicates diffing and migration.
- **Size.** Roughly 294 × ~22 B ≈ 6.5 KB. **VERIFY** the host's `get_param` buffer size for `state`. If it is under 8 KB, switch to a compact format: skip keys at their default value, and record `"d":"1"` so that loading starts from INIT. Decide this at M0.
- **Excluded.** Proxy keys, `mm_sel`, `mac_sel` and the arp position are not stored.
- **Name.** `name` is at most 20 printable ASCII characters; quotes and backslashes are escaped.

**Serialiser.** A single pass over the generated descriptor table, writing into the caller buffer with a bounds-checked append. Floats are written with a custom `%.4f` formatter that avoids locale effects and `snprintf` overhead. If the buffer is too small it returns −1.

**Parser.**
- A single-pass tokenizer over `"key":"value"` pairs. Each key is looked up in the generated hash; the value goes through the normal setter without marking dirty.
- After parsing: recompute all derived values, snap smoothers to their targets, trigger all-sound-off (5 ms fade), and set `clear_pending` for the FX.
- It never reads past `strlen(blob)`. Malformed input stops parsing; the values parsed so far are kept and the rest are left at their defaults.

## 3. Versioning & migration
- **Version bump.** `"v"` is an integer string. Bump it when semantics change, e.g. a curve remap.
- **Migration table.** `params.c` holds `{from_v, key, transform}` entries applied after parsing, for renames and value remaps.
- **Append-only enums.** Enum lists are append-only, so no migration is needed for additions.
- **Missing format tag.** If `"fmt"` is not `crest`, ignore the blob and keep the current patch.

## 4. Factory presets
- **Sources.** `presets/factory/NN_<category>_<name>.json`, one file per patch. Each file contains only the keys that differ from INIT, plus `name` and `category`.
- **Generation.** `gen.py` validates every key and range, expands each preset against INIT, and emits `factory_presets.c`, which holds:
  - `const crest_preset_t FACTORY[] = {{"Init Saw", CAT_INIT, {…full patch_t…}}, …}`, a binary `patch_t` per preset, so loading is a `memcpy` plus a recompute
  - names for `preset_name`
- **Browser keys.**
  - `preset_count` returns N.
  - `preset_name` returns the current name (**VERIFY** whether the host passes an index; Amino's implementation shows the convention).
  - Writing `preset` = i loads it.
- **v1 bank: 64 patches, 8 per category:**

| # | Category | Character targets |
|---|---|---|
| 1–8 | Init / Basics | Init Saw, Init Pulse, Init WT, 3-Osc Stack, Supersaw (density), Sync Lead, Ring Bell, Noise Sweep |
| 9–16 | Pads | Wide unison, slow filter LFOs, density + chorus + hall |
| 17–24 | Bass | Mono/Mono2, pre-OD and filter drive, short Mod Env 1 |
| 25–32 | Leads | Glide, vsync with Mod Env 2, post-dist |
| 33–40 | Keys & Plucks | Repeat envelopes, velocity to cutoff |
| 41–48 | Arps & Sequences | Arp rhythms, synced delay |
| 49–56 | Wavetable & Motion | WT frame sweeps via LFO and macros, Animate |
| 57–64 | FX & Drones | Self-oscillating filter, long reverb, ring mod |

**Macros in presets.** Every factory patch assigns at least Macro 1–4 to meaningful moves (brightness, motion, space, grit) and uses Animate 1 for a performance change. This keeps the Macros page useful out of the box.

**Authoring.** Use the WASM build with a "dump state" button that writes the preset JSON diff. Golden tests render every factory preset ([14](14-testing.md)).

## 5. INIT patch
The INIT patch is a single saw on Osc 1 into LP24 at cutoff 0.75 and res 0.1, with Amp env A 0.02 / D 0.45 / S 0.8 / R 0.3. It has no FX, poly mode, drift 0.15 and volume 0.7. `json_defaults` from `module.json` (if any) are applied on top at `create_instance`.
