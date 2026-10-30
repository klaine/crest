# 11 — Parameter Reference

This table is the authoritative list that `tools/gen/params.py` must encode.

## How to read the table
- **Wire format.** Floats are written with 4 decimals. Ints are written as integers. Enums are written as their **index**; `set_param` also accepts the option name.
- **Append-only.** Enum option order is append-only, and keys are never renamed. If a key must change, add an alias in `params.c`'s migration table instead.
- **Columns.**
  - **Short** is the 5-character label shown in a knob cell.
  - **CP** marks keys published in `chain_params`. Keys without it are stored in state only, or served only through `get_param`.
  - **Viz** is the viz group used for drawing cards.
- **Defaults** describe the INIT patch.

## 1. Oscillators (N = 1, 2, 3)
| Key | Name | Short | Type | Range | Default | CP | Viz |
|---|---|---|---|---|---|---|---|
| `oN_wave` | Osc N Wave | Wave | enum | Sine, Tri, Saw, Pulse, WT | Saw | ✓ | waveform `oN` |
| `oN_table` | Osc N Table | Table | enum | 17 table names | Glass | ✓ | |
| `oN_shape` | Osc N Shape | Shape | float | 0..1 | 0 | ✓ (live) | |
| `oN_shsrc` | Osc N Shape Src | ShSrc | enum | Manual, ModEnv1, LFO1 | Manual | ✓ | |
| `oN_shamt` | Osc N Shape Mod | ShMod | float | −1..1 | 0 | ✓ | |
| `oN_dens` | Osc N Density | Dens | float | 0..1 | 0 | ✓ | |
| `oN_ddet` | Osc N Dens Detune | DnDet | float | 0..1 | 0.3 | ✓ | |
| `oN_vsync` | Osc N VSync | VSync | float | 0..1 | 0 | ✓ | |
| `oN_coarse` | Osc N Coarse | Crse | int | −24..24 st | 0 | ✓ | |
| `oN_fine` | Osc N Fine | Fine | int | −100..100 ct | 0 | ✓ | |
| `oN_penv` | Osc N ModEnv2>Pitch | PEnv | float | −1..1 | 0 | ✓ | |
| `oN_plfo` | Osc N LFO2>Pitch | PLfo | float | −1..1 (60 st · x³, ±5 octaves) | 0 | ✓ | |

Osc 2 and 3 have the same defaults, except `o2_fine = +7` and `o3_coarse = −12`. Their mixer levels are 0, so the INIT patch is a single saw.

## 2. Mixer & drive
| Key | Name | Short | Type | Range | Default | CP |
|---|---|---|---|---|---|---|
| `mx_o1` | Osc 1 Level | Osc1 | float | 0..1 | 1.0 | ✓ (viz fader `mix`) |
| `mx_o2` | Osc 2 Level | Osc2 | float | 0..1 | 0 | ✓ |
| `mx_o3` | Osc 3 Level | Osc3 | float | 0..1 | 0 | ✓ |
| `mx_noise` | Noise Level | Noise | float | 0..1 | 0 | ✓ |
| `mx_ring` | Ring 1×2 Level | Ring | float | 0..1 | 0 | ✓ |
| `noise_col` | Noise Colour | NCol | float | −1..1 | 0 | ✓ |
| `pre_od` | Pre-Filter Overdrive | PreOD | float | 0..1 | 0 | ✓ (live) |
| `post_dist` | Post-Filter Distortion | PstDs | float | 0..1 | 0 | ✓ |

## 3. Filter
| Key | Name | Short | Type | Range | Default | CP | Viz |
|---|---|---|---|---|---|---|---|
| `flt_type` | Filter Type | Type | enum | LP24, LP12, BP12, BP6, HP24, HP12 | LP24 | ✓ | filter `flt` (extra: `flt_type`) |
| `flt_cut` | Cutoff | Cut | float | 0..1 | 0.75 | ✓ (live) | filter `flt` cutoff |
| `flt_res` | Resonance | Res | float | 0..1 | 0.1 | ✓ | filter `flt` resonance |
| `flt_drive` | Filter Drive | Drive | float | 0..1 | 0 | ✓ | |
| `flt_key` | Key Tracking | KeyTk | float | 0..1 | 0.5 | ✓ | |
| `flt_env` | ModEnv1 > Cutoff | Env1 | float | −1..1 | 0 | ✓ | |
| `flt_lfo` | LFO1 > Cutoff | LFO1 | float | −1..1 (96 st · x³, ±8 octaves) | 0 | ✓ | |
| `flt_vel` | Velocity > Cutoff | Vel | float | 0..1 | 0 | ✓ | |

## 4. Envelopes (P = `ae`, `m1`, `m2`)
| Key | Name | Short | Type | Range | Default (ae / m1 / m2) | CP | Viz |
|---|---|---|---|---|---|---|---|
| `P_a` | Attack | Atk | float | 0..1 | 0.02 / 0.02 / 0.02 | ✓ | envelope `P` attack |
| `P_d` | Decay | Dec | float | 0..1 | 0.45 / 0.45 / 0.45 | ✓ | decay |
| `P_s` | Sustain | Sus | float | 0..1 | 0.8 / 0.0 / 0.0 | ✓ | sustain |
| `P_r` | Release | Rel | float | 0..1 | 0.3 / 0.3 / 0.3 | ✓ | release |
| `P_dly` | Delay | Dly | float | 0..1 | 0 | ✓ | |
| `P_hold` | Hold | Hold | float | 0..1 (0 … 500 ms) | 0 | ✓ | |
| `P_vel` | Velocity | Vel | float | −1..1 | 0.5 / 0 / 0 | ✓ | |
| `P_rep` | Repeats | Rpt | int | 0..127 (127 = loop) | 0 | ✓ | |
| `P_trig` | Trigger | Trig | enum | Multi, Single, Reset | Single / Multi / Multi | ✓ | |

## 5. LFOs (N = 1..4)
| Key | Name | Short | Type | Range | Default | CP | Viz |
|---|---|---|---|---|---|---|---|
| `lN_wave` | LFO N Wave | Wave | enum | Sine, Tri, SawUp, SawDn, Square, S&H, Rnd, Exp | Tri | ✓ | lfo `lN` |
| `lN_rate` | LFO N Rate | Rate | float | 0..1 | 0.45 (≈ 1.3 Hz at Low) | ✓ | lfo rate |
| `lN_sync` | LFO N Sync | Sync | enum | Off + the Peak's 35 divisions ([06 §2.1](06-envelopes-lfos.md)) | Off | ✓ | |
| `lN_phase` | LFO N Phase | Phase | float | 0..1 | 0 | ✓ | |
| `lN_dly` | LFO N Delay | Dly | float | 0..1 | 0 | ✓ | |
| `lN_fade` | LFO N Fade Time | Fade | float | 0..1 (0 … 10 s) | 0 | ✓ | |
| `lN_slew` | LFO N Slew | Slew | float | 0..1 | 0 | ✓ | |
| `lN_trig` | LFO N Mode | Mode | enum | Free, Key, Common, CommonKey | Free | ✓ | |
| `lN_range` | LFO N Range | Range | enum | Low, High | Low | 1–2 | |
| `lN_rep` | LFO N Repeats | Rpt | int | 0..127 (0 = Off) | 0 | 1–2 | |
| `lN_fmode` | LFO N Fade Mode | FMode | enum | FadeIn, FadeOut, GateIn, GateOut | FadeIn | 1–2 | |
| `lN_mtrig` | LFO N MonoTrig | MTrig | enum | Legato, Re-Trig | Legato | 1–2 | |
| `lN_fsync` | LFO N FadeSync | FSync | enum | Off, On | On | – (web UI) | |

The LFO 3/4 copies of Range, MonoTrig and FadeSync exist only to keep the four LFO blocks the same layout; the DSP ignores them. LFO 3/4 Repeats and Fade Mode work but are not in chain_params (set them from the web UI).

## 6. Voice
| Key | Name | Short | Type | Range | Default | CP |
|---|---|---|---|---|---|---|
| `v_mode` | Voice Mode | Mode | enum | Poly, Poly2, Mono, MonoLG, Mono2 | Poly | ✓ |
| `v_unison` | Unison Voices | Unis | int | 1..8 | 1 | ✓ |
| `v_udet` | Unison Detune | UDet | float | 0..1 | 0.25 | ✓ |
| `v_spread` | Stereo Spread | Sprd | float | 0..1 | 0.5 | ✓ |
| `v_glide` | Glide Time | Glide | float | 0..1 | 0 | ✓ |
| `v_gmode` | Glide Mode | GMode | enum | Always, Legato | Always | ✓ |
| `v_bend` | Bend Range | Bend | int | 0..24 | 2 | ✓ |
| `v_oct` | Octave | Oct | int | −3..3 | 0 | ✓ |
| `v_drift` | Analog Drift | Drift | float | 0..1 | 0.15 | ✓ |
| `osc_reset` | Osc Phase | OPhs | enum | Free, Reset | Free | ✓ |
| `patch_vol` | Patch Volume | Vol | float | 0..1 | 0.7 | ✓ |

## 7. Modulation matrix, Animate & Macros ([07](07-modulation.md))
| Key | Type | Range | Default | CP |
|---|---|---|---|---|
| `mmNN_s1`, `mmNN_s2` (NN = 01..16) | enum | 29 sources | Off | – |
| `mmNN_dst` | enum | 46 destinations | Off | – |
| `mmNN_dep` | float | −1..1 | 0 | – |
| `mm_sel` | int | 0..15 | 0 | ✓ (select_param) |
| `mm_slot` | int | 1..16 | 1 | ✓ (alias of `mm_sel`) |
| `mm_s1`, `mm_s2`, `mm_dst`, `mm_dep` | proxies | as raw | – | ✓ |
| `mm_clear` | enum (access write) | `-`, Clear | `-` | ✓ |
| `anim1`, `anim2` | enum | Off, On | Off | ✓ (viz switch) |
| `anim1_mode`, `anim2_mode` | enum | Hold, Latch | Hold | ✓ |
| `mac1`…`mac8` | float | 0..1 | 0 | ✓ |
| `macN_dK`, `macN_aK` (K = 1..4) | enum / float | dst list / −1..1 | Off / 0 | – |
| `mac_sel` | int | 0..7 | 0 | ✓ (select_param) |
| `mac_d1..4`, `mac_a1..4` | proxies | as raw | – | ✓ |

## 8. Arpeggiator ([09](09-arpeggiator.md))
`arp_on`, `arp_mode`, `arp_oct`, `arp_rhy`, `arp_rate`, `arp_gate` (default 0.5), `arp_swing` (default 0), `arp_latch`, `arp_ksync` (default On), `arp_vel`, `arp_scale` (default Off). All are CP.

## 9. Effects ([10](10-effects.md))
The CP effect keys and their non-zero defaults:
- **Distortion:** `dst_lvl`, `dst_type`, `dst_tone` (0.7), `dst_mix` (1).
- **Chorus:** `cho_lvl`, `cho_type`, `cho_rate` (0.3), `cho_depth` (0.5), `cho_dly` (0.3), `cho_fb`.
- **Delay:** `dly_lvl`, `dly_time` (0.5), `dly_sync` (1/8D), `dly_fb` (0.35), `dly_mode` (Stereo), `dly_lp` (0.7), `dly_hp` (0.1), `dly_slew` (0).
- **Reverb:** `rev_lvl`, `rev_type` (Hall), `rev_size` (0.6), `rev_decay` (0.5), `rev_damp` (0.4), `rev_hp` (0.15), `rev_pre` (0.1).

Every effect level defaults to 0.

## 10. Special keys (`get_param` / `set_param` only)
`chain_params`, `ui_hierarchy`, `state`, `preset`, `preset_count`, `preset_name`, `mm_items`, `mac_items`, `<key>:effective`, `rui_poll`, `split_voices`, `cpu_us` (profile builds), `name` (patch name, string ≤ 20 characters, stored in state).

## 11. Counts & contract budget
| Group | State keys | CP entries |
|---|---|---|
| Oscillators | 36 | 36 |
| Mixer & drive | 8 | 8 |
| Filter | 8 | 8 |
| Envelopes | 24 | 24 |
| LFOs | 32 | 32 |
| Voice | 11 | 11 |
| Matrix | 64 raw | 8 proxy/selector |
| Animate | 4 | 4 |
| Macros | 8 + 64 raw | 8 + 9 proxy/selector |
| Arp | 10 | 10 |
| FX | 25 | 25 |
| **Total** | **≈ 296** | **≈ 185** |

At about 120 bytes per entry with descriptions stripped, `chain_params` is about **22 KB**. That is under the 32 KiB ceiling with headroom for `short_options`. The validator enforces < 30 KiB ([14](14-testing.md)).

## 12. `params.py` entry shape
This follows Amino's `tools/gen/params.py`:
```python
P(key="flt_cut", name="Cutoff", short="Cut", kind="float", mn=0, mx=1, default=0.75,
  smooth=4, live=True, viz=("flt", "cutoff", "filter", ["flt_type"]), page="filter", cp=True)
```
- **Generated C.** `gen.py` emits a `patch_t` struct with one field per key, a perfect-hash or sorted-bsearch key table, setter/getter thunks, the default patch, the contract strings and the state serialiser tables.
