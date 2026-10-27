// Checks for the web UI's data and pure modules: ui_meta.json / ui_presets.json are consistent with
// each other, every parameter has a reading, the readings match the DSP curves, and the store's
// undo / throttling logic works without a browser.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
let fails = 0, checks = 0;
const check = (ok, msg) => { checks++; if (!ok) { fails++; console.log("  FAIL", msg); } };

globalThis.requestAnimationFrame = (fn) => setTimeout(fn, 0);
globalThis.cancelAnimationFrame = (id) => clearTimeout(id);
globalThis.performance ??= { now: () => Date.now() };
const fmt = await import(pathToFileURL(path.join(ROOT, "src/web/format.js")).href);
const { Store } = await import(pathToFileURL(path.join(ROOT, "src/web/store.js")).href);

const meta = JSON.parse(fs.readFileSync(path.join(ROOT, "build/ui/ui_meta.json"), "utf8"));
const presets = JSON.parse(fs.readFileSync(path.join(ROOT, "build/ui/ui_presets.json"), "utf8"));

// ---- data
check(meta.names.length === presets.length, "one preset row per name");
check(meta.banks.reduce((n, b) => n + b[2], 0) === meta.names.length, "banks cover every patch");
check(meta.banks.every((b, i) => i === 0 || b[1] === meta.banks[i - 1][1] + meta.banks[i - 1][2]), "banks are contiguous");
check(meta.rhythms.length === 33, "33 arp rhythms");
check(meta.wt.length === meta.tables.length, "a preview per wavetable");
for (let i = 0; i < presets.length; i++) {
  const f = presets[i];
  for (let j = 0; j < f.length; j += 2) {
    const p = meta.params[f[j]];
    const v = f[j + 1];
    const lo = p.t === "enum" ? 0 : p.mn, hi = p.t === "enum" ? p.o.length - 1 : p.mx;
    if (!(p && v >= lo - 1e-6 && v <= hi + 1e-6 && !p.vol)) { check(false, `preset ${i} ${p && p.k}=${v}`); break; }
  }
}

// ---- readings
for (const p of meta.params) {
  const vals = p.t === "enum" ? [0, p.o.length - 1] : [p.mn, (p.mn + p.mx) / 2, p.mx, p.d];
  for (const v of vals) {
    const r = fmt.reading(p, v);
    check(typeof r === "string" && r.length > 0 && !/NaN|undefined|Infinity/.test(r), `${p.k}=${v} reads ${r}`);
  }
}
const P = Object.fromEntries(meta.params.map((p) => [p.k, p]));
const eq = (k, v, want) => check(fmt.reading(P[k], v) === want, `${k}=${v} reads "${fmt.reading(P[k], v)}", want "${want}"`);
eq("flt_cut", (69 - 20) / 115, "440 Hz");            // note 69
eq("ae_a", 0, "0.5 ms");                              // 0.5 ms * 40000^0
eq("ae_a", 1, "20 s");
eq("ae_d", 1, "22 s");                                // the Peak's longest decay
eq("ae_r", 1, "25 s");
eq("ae_hold", 1, "500 ms");
eq("ae_rep", 0, "Off");
eq("ae_rep", 3, "×3");
eq("ae_rep", 127, "Loop");
eq("l1_rep", 1, "×1");
eq("l1_fade", 0, "Off");
eq("l1_fade", 0.5, "5 s");
eq("l1_rate", 1, "200 Hz");                           // Low range: 0.02 * 10000
eq("l3_rate", 1, "1.6 kHz");                          // LFO 3/4: one wide range
fmt.setValueSource((k) => (k === "l1_range" ? 1 : undefined));
eq("l1_rate", 1, "1.6 kHz");                          // High range
eq("l1_rate", 0, "0.16 Hz");
fmt.setValueSource(() => undefined);
eq("o1_penv", 1, "+96 st");
eq("o1_plfo", -1, "−60 st");                          // LFO 2 depth: +-5 octaves
eq("flt_lfo", 1, "+96 st");                           // filter LFO 1 depth: 8 octaves
eq("o1_fixed", 69, "A4");
eq("o1_fixed", 0, "Off");
eq("dly_time", 1, "2 s");
eq("v_glide", 0, "Off");
eq("mx_o1", 0.5, "50%");
eq("mm01_dep", -0.25, "−25%");
eq("o1_coarse", 7, "+7 st");
eq("fx_route", 0, "Parallel");

// ---- store: gestures are one undo step, undo/redo restore, writes reach the link
const sent = [];
const s = new Store(meta);
s.connect({ onValues() {}, send: (k, v) => sent.push([k, v]) });
s.beginGesture();
for (const v of [0.1, 0.2, 0.3]) s.set("flt_cut", v);
s.endGesture();
check(s.undoStack.length === 1, "a drag is one undo step");
s.undo();
check(s.get("flt_cut") === P.flt_cut.d, "undo restores the value before the drag");
s.redo();
check(s.get("flt_cut") === 0.3, "redo");
s.set("v_unison", 3.6);
check(s.get("v_unison") === 4, "ints round");
s.set("o1_wave", 99);
check(s.get("o1_wave") === P.o1_wave.o.length - 1, "enums clamp");
s.set("mm_sel", 3);
check(s.undoStack.every((e) => !("mm_sel" in e.before)), "selectors are not undoable");
await new Promise((r) => setTimeout(r, 80));
check(sent.some(([k, v]) => k === "flt_cut" && v === "0.3000"), "the final value of a drag is sent");
const blob = JSON.parse(s.stateBlob("T"));
check(blob.fmt === "crest" && blob.name === "T" && !("mm_sel" in blob) && blob.o1_wave === "4", "stateBlob is a Crest state");

console.log(`web ui: ${checks - fails} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
