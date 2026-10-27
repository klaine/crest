/* AudioWorklet host for the Crest engine (crest.wasm, built by scripts/build_web.sh). Adapted from Amino.
 *
 * The engine lives on the audio thread; the page talks to it with messages:
 *   {t:"set", key, val}   set_param          {t:"get", id, key}  get_param -> {t:"got"}
 *   {t:"midi", m:[s,d1,d2]}                   {t:"tempo", bpm, run}
 * Messages are handled in order, so a get always sees the sets before it.
 * The module has no imports; TextEncoder is not guaranteed in every
 * AudioWorkletGlobalScope, so strings are converted by hand.
 */
function utf8Write(mem, ptr, cap, s) {
  let p = ptr;
  const end = ptr + cap - 1;
  for (let i = 0; i < s.length && p < end; i++) {
    let c = s.charCodeAt(i);
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.length) {
      c = 0x10000 + ((c - 0xd800) << 10) + (s.charCodeAt(++i) - 0xdc00);
    }
    if (c < 0x80) mem[p++] = c;
    else if (c < 0x800) { if (p + 1 >= end) break; mem[p++] = 0xc0 | (c >> 6); mem[p++] = 0x80 | (c & 63); }
    else if (c < 0x10000) { if (p + 2 >= end) break; mem[p++] = 0xe0 | (c >> 12); mem[p++] = 0x80 | ((c >> 6) & 63); mem[p++] = 0x80 | (c & 63); }
    else { if (p + 3 >= end) break; mem[p++] = 0xf0 | (c >> 18); mem[p++] = 0x80 | ((c >> 12) & 63); mem[p++] = 0x80 | ((c >> 6) & 63); mem[p++] = 0x80 | (c & 63); }
  }
  mem[p] = 0;
}

function utf8Read(mem, ptr, len) {
  let out = "";
  for (let i = ptr, end = ptr + len; i < end;) {
    const b = mem[i++];
    let c = b;
    if (b >= 0xf0) { c = ((b & 7) << 18) | ((mem[i++] & 63) << 12) | ((mem[i++] & 63) << 6) | (mem[i++] & 63); }
    else if (b >= 0xe0) { c = ((b & 15) << 12) | ((mem[i++] & 63) << 6) | (mem[i++] & 63); }
    else if (b >= 0xc0) { c = ((b & 31) << 6) | (mem[i++] & 63); }
    if (c >= 0x10000) { c -= 0x10000; out += String.fromCharCode(0xd800 + (c >> 10), 0xdc00 + (c & 1023)); }
    else out += String.fromCharCode(c);
  }
  return out;
}

class CrestProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ok = false;
    this.peak = 0;
    this.blocks = 0;
    try {
      const module = new WebAssembly.Module(options.processorOptions.wasm);
      this.x = new WebAssembly.Instance(module, {}).exports;
      this.x._initialize();
      this.ok = this.x.wa_init(sampleRate) === 1;
    } catch (err) {
      this.error = String(err && err.message || err);
    }
    this.port.onmessage = (e) => this.onMessage(e.data);
    this.port.postMessage({ t: "ready", ok: this.ok, error: this.error, sampleRate });
  }

  mem() { return new Uint8Array(this.x.memory.buffer); }

  onMessage(d) {
    if (!this.ok) { if (d.t === "get") this.port.postMessage({ t: "got", id: d.id, val: null }); return; }
    const x = this.x;
    switch (d.t) {
      case "set":
        utf8Write(this.mem(), x.wa_key(), x.wa_key_cap(), d.key);
        utf8Write(this.mem(), x.wa_val(), x.wa_val_cap(), String(d.val));
        x.wa_set();
        break;
      case "get": {
        utf8Write(this.mem(), x.wa_key(), x.wa_key_cap(), d.key);
        const n = x.wa_get();
        this.port.postMessage({ t: "got", id: d.id, val: n < 0 ? null : utf8Read(this.mem(), x.wa_out(), n) });
        break;
      }
      case "midi": x.wa_midi(d.m[0], d.m[1] | 0, d.m[2] | 0); break;
      case "tempo": x.wa_set_tempo(d.bpm, d.run ? 1 : 0); break;
    }
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.ok || !out || out.length < 2) return true;
    const l = out[0], r = out[1];
    const n = l.length;
    this.x.wa_render(n);
    const pcm = new Int16Array(this.x.memory.buffer, this.x.wa_pcm(), n * 2);
    let pk = this.peak;
    for (let i = 0; i < n; i++) {
      const a = pcm[2 * i] / 32768, b = pcm[2 * i + 1] / 32768;
      l[i] = a; r[i] = b;
      const m = Math.max(a < 0 ? -a : a, b < 0 ? -b : b);
      if (m > pk) pk = m;
    }
    this.peak = pk;
    if (++this.blocks >= 8) { this.port.postMessage({ t: "peak", v: this.peak }); this.peak = 0; this.blocks = 0; }
    return true;
  }
}
registerProcessor("crest", CrestProcessor);
