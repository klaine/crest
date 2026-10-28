# Feature backlog

Features proposed but not started, beyond the milestones in [plan/15](plan/15-roadmap.md). Each entry
says what done looks like; record it in [plan/17](plan/17-implementation-status.md) when it ships.

## Scale-lock the arpeggiator to the Move set

Shared with Bizar (its `BACKLOG.md` has the same entry); the two can be done together.

- **What:** an optional scale quantizer on the notes the arpeggiator plays (`src/dsp/arp.c`, the
  parameter in `tools/gen/params.py`). Options: Off, Set (follow the Move set's root and scale) and a
  few fixed scales.
- **Model:** Amino's Scale = Set option (`quantize_scale` and `amino_move_scale_update` in Amino's
  `src/dsp/voice.c`). Crest has no set-info reader yet: copy `move_info.h` from Amino (Schwung code,
  MIT; credit it in the notices). The read is a lock-free seqlock copy, safe on the audio thread;
  resolve it first in `create_instance`, as Amino does.
- **Done:** the parameter on the device and in the Remote UI, generated files current
  (`scripts/build.sh --gen`), a native test, `plan/17` updated and `scripts/build.sh` passing.

## Pad-grid editor for the modulation matrix

- **What:** an on-device editor for the 16-slot matrix on Move's pads, ported from Amino's Pad Matrix
  (Amino's `src/ui_chain.js` and `src/mod_matrix_ui.mjs`): pads pick a slot and field, the jog edits
  the enum fields, knobs set depths, pad LEDs show the routes, and Move's own pad LEDs are restored on
  exit. A Crest slot has two sources, a destination and a depth.
- **Today:** the matrix is edited through a list plus proxy knobs (`mm_items`, `mm_sel`,
  `mm_s1`/`mm_s2`/`mm_dst`/`mm_dep` in `src/dsp/params.c`; UI in `src/cards.js` and `src/canvas.js`).
  Keep that page working.
- **Done:** the editor works in the device UI harness and `tests/device/test_device_ui.mjs` covers it,
  generated files are current, `plan/17` records it, and `scripts/build.sh` passes. It needs a check
  on a Move: LED ownership and restore, and the pad feel.
