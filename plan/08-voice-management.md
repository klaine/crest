# 08 — Voice Management

## 1. Voice pool
- **Pool.** 8 `crest_voice_t` per instance (`CREST_VOICES`).
- **Groups.** A **note group** is the set of `v_unison` voices that play one note. Polyphony is `floor(8 / v_unison)` notes: unison 1 gives 8 notes, 2 gives 4, 3 gives 2, and 4–8 give 1.
- **Voice fields used for allocation:**
  - `active` (sounding, including release)
  - `gate` (key held or sustained)
  - `note`
  - `group`
  - `age` (monotonic counter set at note-on)
  - `amp_level` (Amp env output)

## 2. Voice modes (`v_mode`, append-only)
| Mode | Behaviour |
|---|---|
| `Poly` | One note group per note. A repeated note that is already sounding **re-uses its group** (retrigger), so the same pitch is never doubled. |
| `Poly2` | Like Poly, but a repeated note takes a **new** group, so release tails overlap. |
| `Mono` | One group. Last-note priority with a 16-note held stack. Every new note retriggers the envelopes (subject to `_trig`). Glide follows `v_gmode`. |
| `MonoLG` | Mono legato. Overlapping notes do not retrigger the envelopes, and glide applies only to legato transitions. |
| `Mono2` | Two groups alternating. Each new note goes to the other group while the previous one releases, so mono lines get overlapping tails (good for basses with long release). Last-note priority applies as in Mono. |

When mode or unison changes while notes sound: release all voices with a 5 ms fade, then reset the allocator.

## 3. Allocation & stealing (poly modes)
To start a note group of size U:
1. **Reuse** (Poly only). If a group with the same note is active, retrigger those voices in place.
2. **Idle voices.** Take idle voices, oldest `age` first.
3. **Releasing voices.** Take releasing voices (gate off), lowest `amp_level` first.
4. **Held voices.** Take held voices, oldest `age` first. Take whole groups when possible.

**Stealing a sounding voice:**
- The voice gets `pending = {note, vel}` and a 64-sample (1.45 ms) linear fade-out of its current output. After the fade it re-initialises (filter and ADAA state cleared, phases per `osc_reset`) and starts the pending note.
- Envelopes start from 0 after a steal. Steals use the fade, so they are not subject to the `Multi` rule.
- In mono modes there is no stealing. The single group retriggers in place from the current env level (or legato), with no fade, so there is no gap.

## 4. Glide
- **Parameters.**
  - `v_glide` (0..1) gives the time `t = 5.6 s·x⁵` (0 = off; calibrated so Peak glide 90/127 ≈ 1 s, the figure in Novation's user guide).
  - `v_gmode`: enum `Always, Legato`. Legato glides only when the previous note is still held.
- **Curve.** Glide is constant-time: an exponential approach in the semitone domain, `pitch += (target − pitch)·c` with `c = 1 − exp(−4.6/(t·fs_ctl))`, which is about 99 % at t.
- **Where it glides from.**
  - In Poly modes, a new group glides from the **last played note** (instance-wide `last_note`), as most polysynths do.
  - In Mono modes it glides from the current pitch.
  - The first note after all notes were released does not glide in Legato mode, but does in Always mode.
- **Matrix.** Destination `Glide Time` adds to `v_glide`.

## 5. Unison
- **Parameters.** `v_unison` (int 1..8), `v_udet` (0..1) and `v_spread` (0..1).
- **Detune.** The voices of a group are spread symmetrically: `pos_i = (2i/(U−1) − 1)` for U > 1. The offset is `pos_i · 50 cents · v_udet³` (cubic for fine control near 0) plus the matrix `Unison Detune`.
- **Pan.** `pos_i · v_spread`, with an equal-power law ([05 §6](05-mixer-filter-drive.md)).
- **Variation.** Each unison voice gets an independent random start phase per oscillator. Drift is independent per voice.
- **Gain.** Per-voice gain stays fixed. Unison makes the sound louder, as on hardware. The patch volume and the master soft clip handle level.

## 6. MIDI event handling
- **Queue.** `on_midi` pushes events into a 64-entry ring buffer in the instance; overflow drops the oldest. `render_block` drains the queue at the start of the block. `on_midi` and `render_block` run on the same thread, so no atomics are needed.
- **Event types:**
  - **Note-on.** Goes to the arpeggiator if `arp_on`, otherwise to the allocator.
  - **Note-off.** Same routing. If sustain is down, the voice is marked `sustained` instead of released.
  - **Sustain (CC 64).** On release, every `sustained` voice whose key is not held gets a note-off.
  - **Poly AT.** Sets `polyat` on every voice with a matching note.
  - **Channel AT, CCs and pitch bend.** Write the global source targets, which are smoothed ([07 §2](07-modulation.md)).
  - **CC 120 / 123.** All sound off (immediate 5 ms fade) / all notes off (release). Also on `set_param("state")` and preset load.
  - **CC 121.** Reset controllers: wheel 0, AT 0, bend 0, sustain off, expression 1.
- **Held-note stack.** 16 entries, in insertion order, used by Mono modes and the arp. A note-off removes the note from the stack. In Mono modes the voice returns to the most recent remaining held note, gliding per `v_gmode` and retriggering per `_trig`.

## 7. Other voice parameters
| Key | Range | Purpose |
|---|---|---|
| `v_oct` | int −3..3 | Octave transpose |
| `v_bend` | int 0..24, default 2 | Pitch bend range in semitones (up and down) |
| `v_drift` | 0..1 | Analog drift ([04 §9](04-oscillators.md)) |
| `osc_reset` | enum `Free, Reset` | Oscillator phase on note-on ([04 §2](04-oscillators.md)) |
| `patch_vol` | 0..1, default 0.7 | Patch level ([03 §5](03-dsp-architecture.md)) |

## 8. Tests
- **Polyphony.** Poly with unison 1: 9 notes in sequence steal voice #1 (oldest). With 8 held notes plus 1 released, the released voice is stolen first.
- **Poly vs Poly2.** Poly retriggering the same note leaves exactly U voices on that note; Poly2 gives 2U.
- **Mono stack.** Mono: hold A, then hold B, then release B returns to A, and glide happens when `v_glide > 0`.
- **Legato.** MonoLG: an overlapping note does not reset the Amp env stage. Mono2: alternating groups.
- **Unison layout.** Unison 4: polyphony is 2, and the detune offsets are symmetric (their sum ≈ 0).
- **Sustain.** Notes released while the pedal is down keep sounding until pedal-up.
- **Steal fade.** No click > 0.05 per sample in the steal fade (measured in a golden test).
