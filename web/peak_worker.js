/* Peak SysEx import for the browser build: a Web Worker running its own copy of crest.wasm, so
 * converting and levelling a bank never touches the audio thread.
 *
 *   in:  {t:"import", wasm: ArrayBuffer, bytes: ArrayBuffer, file}
 *   out: {t:"bank", file, patches:[{name, values: Float32Array}]}   right after conversion
 *        {t:"level", i, vol}                                         as each patch's loudness is solved
 *        {t:"done"} | {t:"error", message}
 *
 * The conversion is src/dsp/peak_import.c (the same rules as the factory import, tools/peak) and the
 * loudness solve is src/dsp/level.c (the factory's): each patch renders 6 s offline. */
let x = null;

function readName(len) {
  const b = new Uint8Array(x.memory.buffer, x.wa_out(), len);
  let s = "";
  for (let i = 0; i < len; i++) s += String.fromCharCode(b[i]);
  return s;
}

self.onmessage = async (e) => {
  const d = e.data;
  if (d.t !== "import") return;
  try {
    if (!x) {
      const { instance } = await WebAssembly.instantiate(d.wasm, {});
      x = instance.exports;
      x._initialize();
      if (x.wa_init(44100) !== 1) throw new Error("the engine did not start");
    }
    const bytes = new Uint8Array(d.bytes);
    if (bytes.length > x.wa_syx_cap()) throw new Error("the file is larger than a Peak bank");
    new Uint8Array(x.memory.buffer, x.wa_syx(), bytes.length).set(bytes);
    const n = x.wa_syx_scan(bytes.length);
    if (n === 0) throw new Error("no Peak patches in this file");
    const np = x.wa_nparams();
    const patches = [];
    for (let i = 0; i < n; i++) {
      const len = x.wa_syx_patch(i);
      patches.push({ name: readName(len), values: new Float32Array(x.memory.buffer, x.wa_vals(), np).slice() });
    }
    self.postMessage({ t: "bank", file: d.file, patches });
    for (let i = 0; i < n; i++) {
      new Float32Array(x.memory.buffer, x.wa_vals(), np).set(patches[i].values);
      const vol = x.wa_level(1);
      if (vol >= 0) self.postMessage({ t: "level", i, vol });
    }
    self.postMessage({ t: "done" });
  } catch (err) {
    self.postMessage({ t: "error", message: String((err && err.message) || err) });
  }
};
