# 12 — Move UI Pages

## 1. Approach
- **Host-drawn UI.** Crest uses the **host knob grid**. `get_param("ui_hierarchy")` returns a static generated document, and Crest ships no `ui_chain.js` in v1.
- **What the host provides.** Jog paging, Shift+jog-click section picker, Shift fine-adjust, touch-to-show-value, viz cards, preset browser, "My Presets" and "Module" pages, help, and Scenes/automation on every CP key.
- **Page rules** (from Amino's `docs/05-ux-control-design.md`):
  1. Every page is a 2×4 grid. Top row = source/shape/what; bottom row = time/motion/how much.
  2. The same position does the same job across sibling pages (e.g. knob 1 = level or type, knobs 5–8 = A D S R on every env page).
  3. A viz group sits contiguously within one row of 4.
  4. Short names are at most 5 characters; enum `short_options` are at most 4.
  5. Selector keys (`mm_sel`, `mac_sel`, `preset`) never appear in `knobs`.
- **`visible_if`.** Not used in v1. All 4 gate slots are kept in reserve; inactive knobs show their value and are simply ignored by the DSP.

## 2. Page order (the jog walk)
```
root (Main) ─► Presets ─► Perform ─► Macros
           ─► Osc 1 ─► Osc 1 Mod ─► Osc 2 ─► Osc 2 Mod ─► Osc 3 ─► Osc 3 Mod ─► Mixer
           ─► Filter ─► Amp Env ─► Mod Env 1 ─► Mod Env 2 ─► Envelope Delays
           ─► LFO 1 ─► LFO 1 Fade ─► LFO 2 ─► LFO 2 Fade ─► LFO 3 ─► LFO 4
           ─► Mod Matrix (list) → Matrix Edit
           ─► Macro Routing (list) → Macro Edit
           ─► Voice ─► Arp ─► Arp More
           ─► FX: Distortion ─► Chorus ─► Delay ─► Reverb
           ─► (host) My Presets ─► Module (+ Help)
```
Shift+jog-click opens the section picker at the level labels: Main, Presets, Perform, Oscillators, Mixer, Filter, Envelopes, LFOs, Matrix, Macros, Voice, Arp, FX. To get that grouping, the levels are nested under parent levels whose `params` are level links (the SchwungFreak root pattern).

## 3. Page layouts
| Level | Knob 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| **Main** (root) | Cut | Res | Env1 | PreOD | ae Atk | ae Dec | ae Sus | ae Rel |
| Perform | An1 | An2 | An1Md | An2Md | Vol | Drift | ArpOn | Scale |
| Macros | M1 | M2 | M3 | M4 | M5 | M6 | M7 | M8 |
| Osc N | Wave | Table | Shape | Dens | Crse | Fine | VSync | Level (`mx_oN`) |
| Osc N Mod | ShSrc | ShMod | DnDet | – | PEnv | PLfo | – | – |
| Mixer | Osc1 | Osc2 | Osc3 | Ring | Noise | NCol | PreOD | PstDs |
| Filter | Type | Cut | Res | Drive | Env1 | LFO1 | KeyTk | Vel |
| Amp / Mod1 / Mod2 Env | Atk | Dec | Sus | Rel | Hold | Vel | Rpt | Trig |
| Envelope Delays | AmpD | M1Dly | M2Dly | – | – | – | – | – |
| LFO 1 / 2 | Wave | Range | Rate | Sync | Phase | Slew | Mode | Rpt |
| LFO 1 / 2 Fade | Fade | FMode | Dly | MTrig | – | – | – | – |
| LFO 3 / 4 | Wave | Rate | Sync | Phase | Slew | Mode | Fade | Dly |

FadeSync (LFO 1/2) and Repeats / Fade Mode (LFO 3/4) have no knob: chain_params is close to its
31 KiB limit (the build prints its size). They are saved in patches and set from the web UI.
| Matrix Edit | Src1 | Src2 | Dest | Depth | Slot | Clear | – | – |
| Macro Edit | Dst1 | Amt1 | Dst2 | Amt2 | Dst3 | Amt3 | Dst4 | Amt4 |
| Voice | Mode | Unis | UDet | Sprd | Glide | GMode | Bend | Oct |
| Arp | On | Mode | Oct | Rhy | Rate | Gate | Swing | Latch |
| Arp More | KSync | Vel | – | – | – | – | – | – |
| Distortion | Level | Type | Tone | Mix | – | – | – | – |
| Chorus | Level | Type | Rate | Depth | Dly | Fb | – | – |
| Delay | Level | Time | Sync | Fb | Mode | Slew | LP | HP |
| Reverb | Level | Type | Size | Decay | Damp | HP | Pre | – |

**Empty knob slots.** A dash means no key. **VERIFY** that `knobs` arrays shorter than 8 are accepted. If they are not, pad with `null` or repeat the level knob, depending on the host behaviour (see SchwungFreak's pages for precedent).

## 4. Viz groups
| Group | Kind | Keys (row) | Page |
|---|---|---|---|
| `ae`, `m1`, `m2` | envelope | `_a _d _s _r` (top row) | Env pages; also Main's bottom row for `ae` |
| `flt` | filter | `flt_type flt_cut flt_res` (top row), extra `flt_type` | Filter; Main top row (cut, res) |
| `l1`…`l4` | lfo | `lN_wave lN_rate` (top row) | LFO pages |
| `o1`…`o3` | waveform | `oN_wave oN_table oN_shape` | Osc pages |
| `mix` | fader | `mx_o1 mx_o2 mx_o3 mx_ring` | Mixer top row |
| `anim` | switch | `anim1 anim2` | Perform |

**`live:true` keys.** `flt_cut`, `oN_shape` and `pre_od` animate their knob rings with modulation through `:effective`.

## 5. List pages (matrix & macro routing)
This follows SchwungFreak (`modmatrix` level in `src/schwungfreak.c:1342`):
```json
"matrix":     {"label":"Mod Matrix","items_param":"mm_items","select_param":"mm_sel",
               "navigate_to":"matrix_edit","params":[]},
"matrix_edit":{"label":"Matrix Edit","knobs":["mm_s1","mm_s2","mm_dst","mm_dep","mm_slot","mm_clear"],
               "params":["mm_s1","mm_s2","mm_dst","mm_dep","mm_slot","mm_clear"]},
"macro_routing":{"label":"Macro Routing","items_param":"mac_items","select_param":"mac_sel",
               "navigate_to":"macro_edit","params":[]},
"macro_edit": {"label":"Macro Edit","knobs":["mac_d1","mac_a1","mac_d2","mac_a2","mac_d3","mac_a3","mac_d4","mac_a4"], "params":[…]}
```
- **`mm_items`.** Returns 16 entries, e.g. `{"index":0,"label":"1: LFO1xWhl>Pitch"}`. They are built on demand into the caller's buffer with `snprintf`; the 16 × ~30 B output is fine on the RT thread.
- **`mac_items`.** Returns 8 entries such as `"M1: Cut +Res +2"` (the first destination name plus a count of the others).
- **Header label.** On Matrix Edit, the selected slot label is shown in the page header via the level label. **VERIFY** whether a dynamic label is supported. If not, the `Slot` knob value carries the context.

## 6. Presets level
```json
"presets": {"label":"Presets","list_param":"preset","count_param":"preset_count","name_param":"preset_name","params":[]}
```
`preset` selects a factory preset. A write loads it, like a `state` load, so it is RT-safe because the factory data is `const`.

## 7. Help (`help.json`)
- **Format.** The top level must have `children`. Lines are at most 20 characters, in ASCII.
- **Topics:** Overview, Oscillators, Shape & Density, Virtual Sync, Filter & Drive, Envelopes, LFOs, Mod Matrix, Macros & Animate, Arp, FX, Tips.
- **Generation.** `gen.py` validates line lengths. The help content itself is hand-written.

## 8. Remote UI (M6) — built
Implemented as `src/web_ui.html` + `src/web/` (see README). Module-drawn pages and cards (`src/canvas.js`,
`src/cards.js`) were added on top of the host knob grid; the original plan below is kept for reference.

`src/web_ui.html` follows Amino's `src/web_ui.html` and `src/web/*.js` pattern, using `/static/schwung-remote-api.js`:
- **Panel layout.** A panel-style layout of all sections, a full 16×(S1, S2, Dst, Dep) matrix grid, and macro routing tables. It also covers the arp step display and preset browse/rename.
- **Size.** First load is at most 150 KB, with lazy sections (Amino's lesson).
- **Telemetry.** About 2 Hz through `rui_poll` (`rev:on:tick:bpm`).

## 9. Contract generation
`gen.py` builds both strings from `params.py` plus a `PAGES` table in the same file. Each `PAGES` entry is `{level, label, knobs[8], parent, kind}`. The generator:
- emits minified JSON
- strips descriptions from `chain_params`
- writes `contract_gen.c` with `static const char CHAIN_PARAMS[]` and `UI_HIERARCHY[]`, plus their lengths
- writes `build/chain_params.json` and `build/ui_hierarchy.json` (pretty-printed) for review and the validator
