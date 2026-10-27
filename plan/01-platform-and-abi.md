# 01 — Platform & ABI

Most of this section is taken from Amino's `docs/02-platform-constraints.md` and the vendored Schwung `MODULES.md` (`_schwung_modules.md`). Copy those references into `docs/` at M0.

## 1. Hardware
| Resource | Detail | Crest relevance |
|---|---|---|
| CPU | Quad Cortex-A72 @ 1.5 GHz, 2 GB RAM; audio on **core 3** | NEON available; budget in [03 §8](03-dsp-architecture.md) |
| Display | 128×64, 1-bit | Host knob grid draws it; viz cards for env/filter/LFO |
| Encoders | 8 relative (CC 71–78), touch notes 0–7 | Host-owned page grid |
| Jog | CC 14, click CC 3 | Page navigation (host) |
| Pads | 32, velocity + **poly aftertouch** | The chain slot receives the *track's notes*, not pad IDs |
| Audio | 44 100 Hz, 128 frames/block (2.902 ms), int16 stereo interleaved | `MOVE_SAMPLE_RATE`, `MOVE_FRAMES_PER_BLOCK` |

## 2. Plugin API v2
`src/dsp/plugin_api.h` and `src/dsp/move_info.h` are vendored **verbatim** from Amino (`Development/src/dsp/`). Key types:

```c
typedef struct plugin_api_v2 {
    uint32_t api_version;                         /* = 2 */
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int   (*get_error)(void *instance, char *buf, int buf_len);
    void  (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
```

Host services we use: `host->get_bpm()`, `host->get_clock_status()`, `host->get_beat_position()` (it may be NULL and returns <0 when stopped, so always guard it), and `host->sample_rate`. `host->log` must **never** be called from an entry point.

Not used in v1: `move_plugin_render_split`, `midi_send_*`, `mod_emit_value`.

### 2.1 Entry point responsibilities
| Function | Crest behaviour |
|---|---|
| `move_plugin_init_v2` | Store `host` in a static. Return a static `plugin_api_v2_t`. |
| `create_instance` | Claim a slot from `g_pool[CREST_MAX_INSTANCES=4]` with an atomic CAS. Set the init patch and apply `json_defaults` if non-empty, then make the first `move_info_read()` call. No malloc. Return NULL if the pool is full. |
| `destroy_instance` | Release the slot (atomic store). |
| `on_midi` | Parse into the event queue processed at the start of the next sub-block ([08 §6](08-voice-management.md)). |
| `set_param` | Look the key up in the generated hash table, store the target value and set a dirty bit. O(1), no parsing except `strtof`/enum lookup. The `state` key is the exception: parse the full blob ([13](13-presets-and-state.md)). |
| `get_param` | Copy static strings for contract keys; format the value for normal keys. |
| `get_error` | Return the last error text set by load paths (e.g. "pool full"). |
| `render_block` | Set FTZ, then run sub-blocks of 16 samples: events → control → audio. Then FX, master, int16 conversion; restore FPCR. |

### 2.2 Special `get_param` keys
| Key | Returns |
|---|---|
| `chain_params` | Static JSON array (generated), **< 32 KiB** |
| `ui_hierarchy` | Static bare `{"levels":{…}}` object (generated) |
| `state` | Full patch blob ([13](13-presets-and-state.md)); always answered |
| `preset`, `preset_count`, `preset_name` | Factory bank browser |
| `mm_items`, `mac_items` | JSON list pages for the matrix and macro editors ([12 §5](12-ui-pages.md)) |
| `<key>:effective` | Modulated value for `live:true` params (cutoff, osc shapes) |
| `rui_poll` | `rev:on:tick:bpm` for the remote UI ([12 §8](12-ui-pages.md)) |
| `split_voices` | `""` (none) |
| `cpu_us` | **Debug builds only** (`-DCREST_PROFILE`): averaged render time |

## 3. Realtime rules (non-negotiable)
All entry points run on the SPI audio callback (SCHED_FIFO 70, core 3). That includes `create_instance`, `set_param` and `get_param`. The roughly 2.37 ms of slack per block is shared with Move and all other modules.

Forbidden in every entry point:
- file I/O
- `malloc`/`free`/`realloc`
- mutexes and blocking syscalls
- any logging, including `fprintf(stderr)`
- `dlopen`, `fork`
- unbounded loops
- memsets larger than 64 KB in one block

Consequences:
- **Static data.** Wavetables, presets, lookup tables, `chain_params` and `ui_hierarchy` are `const` data compiled into `dsp.so`.
- **Static instance memory.** Each instance is a fixed-size struct in a static pool. Delay and reverb lines are statically sized inside it.
- **Lazy clears.** Large buffers are cleared incrementally over several blocks after a preset load or instance claim. Effects stay muted while `clear_pending`.
- **Once-guarded tables.** Any table not compiled in, such as `tanh` or `exp2` LUTs, is built by a once-guard that is safe under concurrent `create_instance`. Prefer generating it at build time.

## 4. `module.json`
```json
{
  "id": "crest",
  "name": "Crest",
  "abbrev": "CRST",
  "version": "0.1.0",
  "api_version": 2,
  "description": "8-voice hybrid polysynth: 3 osc, drive filter, mod matrix, arp, FX",
  "author": "AI & Klaine",
  "dsp": "dsp.so",
  "component_type": "sound_generator",
  "capabilities": {
    "chainable": true,
    "midi_in": true,
    "audio_out": true,
    "aftertouch": true,
    "claims_edit_ccs": false,
    "default_forward_channel": -2
  }
}
```
Rules:
- **Parser limits.** The host JSON parser takes no comments and caps the file at 8 KB.
- **No `ui_hierarchy` here.** A synth's hierarchy is only read from `get_param`.
- **The DSP file must be `dsp.so`.** The host ignores the `dsp` field for chain synths.
- **`claims_edit_ccs` stays false.** Crest has no layer copy or delete, so it leaves Undo/Copy/Delete to Move.

## 5. Install layout on device
```
/data/UserData/schwung/modules/sound_generators/crest/
  dsp.so  module.json  help.json  LICENSE  [web_ui.html]
```
Owner `ableton:users`. Patches (My Presets) are written by the host to `/data/UserData/schwung/patches/`.

## 6. MIDI input
| Message | Handling |
|---|---|
| 0x90 note on (vel>0) | Voice allocation or the arp ([08](08-voice-management.md), [09](09-arpeggiator.md)) |
| 0x80, or 0x90 vel 0 | Note off; release velocity is stored |
| 0xA0 poly AT | Per-voice `polyat` source (matched by note) |
| 0xD0 channel AT | Global `chanat` source |
| 0xE0 pitch bend | Global bend, range `v_bend` |
| CC 1 | Mod wheel source |
| CC 2 / 11 | Breath / Expression sources |
| CC 64 | Sustain (holds note-offs) |
| CC 80 / 81 | Animate 1 / 2 (≥64 = on) — **VERIFY** no conflict with Move's CC use |
| CC 120 / 123 | All sound off / all notes off |
| CC 121 | Reset controllers |

`source` handling: accept `INTERNAL`, `EXTERNAL` and `HOST`. Ignore `TOUCH`, which is knob touch notes.
