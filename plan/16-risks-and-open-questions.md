# 16 — Risks & Open Questions

## 1. Risks
| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | CPU over budget (8 voices × 3 osc with density + 24 dB filter + reverb) | Medium | High | Measure at M0/M1. Levers in [03 §8](03-dsp-architecture.md). Fallbacks: cap density to 2 copies, Eco reverb, unison voice cap. |
| R2 | `chain_params` exceeds 32 KiB as params grow | Low | High | Matrix and macro routing stay out of CP. Descriptions stripped. Validator limit at 30 KiB. |
| R3 | Aliasing audible in oscillators or in drive/filter stages at 1× | Medium | Medium | 2-point polyBLEP by default, with a 4-point build option (3–9 dB better, measured; it adds droop, see [04 §3.1](04-oscillators.md)), and ADAA first. Then the table-based Saw/Pulse A/B, then optional 2× halfband in M2 (budgeted). |
| R4 | `state` blob too large for the host buffer | Low–Med | High | VERIFY at M0. Compact default-skipping format as fallback ([13 §2](13-presets-and-state.md)). |
| R5 | Wavetable `.rodata` (~2.3 MB) slows loading or hits limits | Low | Medium | 8-frame fallback (1.2 MB), or int8 µ-law tables. |
| R6 | Host doesn't refresh proxy values or list labels after a selector change | Medium | Medium | Copy SchwungFreak's exact mechanism (`is_loading` / `contract_dirty_blocks`). Test on the device in M3. |
| R7 | Animate CCs (80/81) not delivered to the chain slot | Medium | Low | `anim1`/`anim2` params remain usable from knobs, Scenes and automation. |
| R8 | Zig version differences (flags, ASan on macOS, `.so.o` leftovers) | Low | Low | Pin the minimum version. The build script handles leftovers. UBSan-trap fallback. |
| R9 | glibc symbol-version mismatch on the device | Low | High | `aarch64-linux-gnu.2.28` target pin. Check `nm`/`readelf` in build. |
| R10 | Scope (the full feature set) delays a usable release | Medium | Medium | Each milestone is shippable. M1 is already a complete subtractive synth. |
| R11 | IP: resemblance to Peak branding or presets | Low | High | Original names and content only ([00 §5](00-overview.md)). |

## 2. Open questions (VERIFY), with the milestone that answers each
1. **[M0]** Move's glibc version, and whether zig's `aarch64-linux-gnu.2.28` is accepted.
2. **[M0]** The buffer size the host passes to `get_param("state")`, and to `chain_params`.
3. **[M0]** Whether `knobs` arrays shorter than 8 render correctly, and what pads empty cells.
4. **[M0]** Whether CC 80/81 and 0xA0 poly aftertouch arrive at a chain synth with the `aftertouch` capability.
5. **[M0]** The `debug.log` path, and whether there is a CLI module rescan for `deploy.sh`.
6. **[M0]** Device versus host benchmark factor.
7. **[M1]** `preset_name` semantics: is it the current name, or does the host query by index? Check Amino's implementation.
8. **[M3]** Whether a dynamic level label is supported, for Matrix Edit showing the slot.
9. **[M3]** Polling rate and cost of `<key>:effective` for `live:true` keys.
10. **[M4]** `get_beat_position` precision and jitter: is per-block reading enough for the arp, or is an internal PLL needed?
11. **[M5]** Arp rests: should the note order advance on rests?

## 3. Decisions log
| Date | Decision |
|---|---|
| 2026-10-25 | Zig (`zig cc`) is the only C compiler, for device, native tests and WASM. |
| 2026-10-25 | Reuse Amino's toolchain and codegen; use SchwungFreak's list-page pattern for the matrix. |
| 2026-10-25 | Host knob grid (`ui_hierarchy` from DSP) for v1; no `ui_chain.js`. |
| 2026-10-25 | Full Peak-style feature set as the v1.0 target, delivered over M0–M6. |
| 2026-10-25 | No `noise_type` or `dly_width` keys (page space); noise is white with a colour tilt. |
