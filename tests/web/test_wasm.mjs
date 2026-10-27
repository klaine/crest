// Smoke test of the browser build: build/crest.wasm loads with no imports, makes sound for a note,
// answers get_param("state") and loads a factory patch by index.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const wasm = fs.readFileSync(process.argv[2] || path.join(ROOT, "build/crest.wasm"));
let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; console.log("  FAIL", msg); } };

const { instance } = await WebAssembly.instantiate(wasm, {});
const x = instance.exports;
x._initialize();
check(x.wa_init(44100) === 1, "wa_init");
const mem = () => new Uint8Array(x.memory.buffer);
const put = (ptr, s) => { const b = Buffer.from(s + "\0"); mem().set(b, ptr); };
const set = (k, v) => { put(x.wa_key(), k); put(x.wa_val(), String(v)); x.wa_set(); };
const get = (k) => { put(x.wa_key(), k); const n = x.wa_get(); return n < 0 ? null : Buffer.from(mem().slice(x.wa_out(), x.wa_out() + n)).toString("utf8"); };
const peak = (blocks) => {
  let pk = 0;
  for (let b = 0; b < blocks; b++) {
    x.wa_render(128);
    const pcm = new Int16Array(x.memory.buffer, x.wa_pcm(), 256);
    for (const s of pcm) pk = Math.max(pk, Math.abs(s));
  }
  return pk / 32768;
};

check(peak(20) === 0, "silent with no notes");
x.wa_midi(0x90, 60, 100);
check(peak(100) > 0.05, "a note sounds");
x.wa_midi(0x80, 60, 0);
const st = JSON.parse(get("state"));
check(st.fmt === "crest" && "flt_cut" in st, "state blob parses");
// 12 is a Peak patch in the full bank; the single-file build (no Peak banks) has fewer
const pi = String(Math.min(12, Number(get("preset_count")) - 1));
set("preset", pi);
check(get("preset") === pi, "preset loads by index");
check(JSON.parse(get("state")).name === get("preset_name"), "state carries the patch name");
x.wa_set_tempo(128, 1);
set("arp_on", "On");
x.wa_midi(0x90, 48, 100);
check(peak(400) > 0.01, "arp plays with the transport running");
// Peak SysEx import (what web/peak_worker.js does): scan a reference bank, convert, level one patch,
// and check the values against the factory conversion of the same bank (presets/peak, patch_vol aside).
const syxPath = path.join(ROOT, "reference_novation", "Peak Factory Ban.syx");
if (fs.existsSync(syxPath)) {
  const syx = fs.readFileSync(syxPath);
  new Uint8Array(x.memory.buffer, x.wa_syx(), syx.length).set(syx);
  check(x.wa_syx_scan(syx.length) === 128, "128 patches in a Peak bank");
  const meta = JSON.parse(fs.readFileSync(path.join(ROOT, "build/ui/ui_meta.json"), "utf8"));
  const bank = JSON.parse(fs.readFileSync(path.join(ROOT, "presets/peak/peak_factory_ban.json"), "utf8")).presets;
  const np = x.wa_nparams();
  check(np === meta.params.length, "parameter count matches the UI metadata");
  let compared = 0, bad = 0;
  for (let i = 0; i < 128; i++) {
    const len = x.wa_syx_patch(i);
    const name = Buffer.from(mem().slice(x.wa_out(), x.wa_out() + len)).toString("latin1");
    const ref = bank.find((p) => p.name === name);
    if (!ref) continue; // duplicates and Init patches are not in the factory bank
    compared++;
    const v = new Float32Array(x.memory.buffer, x.wa_vals(), np);
    meta.params.forEach((p, k) => {
      if (p.k === "patch_vol" || !(p.k in ref.values)) return;
      const want = p.t === "enum" ? p.o.indexOf(ref.values[p.k]) : ref.values[p.k];
      if (Math.abs(v[k] - want) > 2e-4) { if (bad++ < 5) console.log("  ", name, p.k, v[k], want); }
    });
  }
  check(compared > 100 && bad === 0, `converted patches match the factory bank (${compared} compared, ${bad} differ)`);
  x.wa_syx_patch(0);
  const vol = x.wa_level(1);
  check(vol >= 0.02 && vol <= 1, "levelling gives a patch volume (" + vol + ")");
}
console.log(`wasm: ${fails ? "FAILED" : "ok"}`);
process.exit(fails ? 1 : 0);
