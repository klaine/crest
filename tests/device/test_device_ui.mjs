// Renders the on-device scripts (src/cards.js, src/canvas.js) against a mock of the Schwung draw
// context: every card key with several values (and the null "no answer" case), the home page and
// the patch browser. Fails on a throw, a draw outside the frame, or an empty picture.
// With --png DIR it also writes 4x previews for eyeballing.
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const meta = JSON.parse(fs.readFileSync(path.join(ROOT, "build/ui/ui_meta.json"), "utf8"));
const cp = JSON.parse(fs.readFileSync(path.join(ROOT, "build/chain_params.json"), "utf8"));
const pngDir = process.argv.includes("--png") ? process.argv[process.argv.indexOf("--png") + 1] : null;
let fails = 0, checks = 0;
const check = (ok, msg) => { checks++; if (!ok) { fails++; console.log("  FAIL", msg); } };

// 5x7-ish font metrics: 6 px per glyph, like the device's proportional font at worst.
function makeCtx(w, h) {
  const px = new Uint8Array(w * h);
  let outside = 0;
  const set = (x, y, c) => {
    x = Math.round(x); y = Math.round(y);
    if (x < 0 || y < 0 || x >= w || y >= h) { outside++; return; }
    px[y * w + x] = c ? 1 : 0;
  };
  const ctx = {
    width: w, height: h,
    fillRect(x, y, ww, hh, c) { for (let j = 0; j < hh; j++) for (let i = 0; i < ww; i++) set(x + i, y + j, c); },
    print(x, y, text, c) { // a 5x7 box per glyph, with a dot pattern so text shows in previews
      const s = String(text);
      for (let k = 0; k < s.length; k++) if (s[k] !== " ") for (let j = 1; j < 6; j++) for (let i = 0; i < 4; i += 1) if ((i + j + s.charCodeAt(k)) % 3) set(x + k * 6 + i, y + j, c);
    },
    textWidth: (s) => String(s).length * 6,
    line(x0, y0, x1, y1, c) {
      const n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0), 1);
      for (let i = 0; i <= n; i++) set(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, c);
    },
  };
  return { ctx, px, outsideCount: () => outside };
}

function png(file, px, w, h, scale = 4) {
  const W = w * scale, H = h * scale;
  const raw = Buffer.alloc((W + 1) * H);
  for (let y = 0; y < H; y++) {
    raw[y * (W + 1)] = 0;
    for (let x = 0; x < W; x++) raw[y * (W + 1) + 1 + x] = px[Math.floor(y / scale) * w + Math.floor(x / scale)] ? 255 : 0;
  }
  const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (b) => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 0;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
}

function load(file) {
  const g = {};
  // build/peak/ has the local Peak-bank canvas.js that scripts/build.sh packages instead of src/'s
  const local = path.join(ROOT, "build/peak", file);
  const src = fs.existsSync(local) ? local : path.join(ROOT, "src", file);
  vm.runInNewContext(fs.readFileSync(src, "utf8"), { globalThis: g, Math, Number, String, Object, isFinite, JSON });
  return g;
}

if (pngDir) fs.mkdirSync(pngDir, { recursive: true });
const byKey = Object.fromEntries(meta.params.map((p) => [p.k, p]));
const defaults = Object.fromEntries(meta.params.map((p) => [p.k, String(p.d)]));

// ---- cards
const cards = load("cards.js");
check(typeof cards.crst === "function", "cards.js exports crst");
const cardKeys = cp.filter((e) => e.card_script).map((e) => e.key);
check(cardKeys.length > 20, "card keys declared (" + cardKeys.length + ")");
for (const key of cardKeys) {
  const p = byKey[key];
  const samples = p.t === "enum" ? [0, p.o.length - 1] : [p.mn, (p.mn + p.mx) / 2, p.mx];
  for (const raw of [...samples.map(String), null]) {
    const { ctx, px, outsideCount } = makeCtx(96, 34);
    const values = { ...defaults, "o1_wave": "3", "mm_s1": "15", "mm_dst": "20" };
    try {
      cards.crst(ctx, { name: p.n, value: raw, raw, values, w: 96, h: 34, nowMs: 0 });
    } catch (err) { check(false, `card ${key}=${raw} threw ${err.message}`); continue; }
    check(outsideCount() === 0, `card ${key}=${raw} draws outside its frame (${outsideCount()} px)`);
    check(px.some((v) => v), `card ${key}=${raw} is empty`);
    if (pngDir && raw === String(samples[1] ?? samples[0])) png(path.join(pngDir, `card_${key}.png`), px, 96, 34);
  }
}

// the LFO rate card reads in the range the Range switch picks (Low 0.02..200 Hz, High 8x)
for (const [range, want] of [["0", "200 Hz"], ["1", "1.6 kHz"]]) {
  const { ctx } = makeCtx(96, 34);
  const printed = [];
  const print = ctx.print;
  ctx.print = (x, y, t, c) => { printed.push(String(t)); print(x, y, t, c); };
  cards.crst(ctx, { name: byKey.l1_rate.n, value: "1", raw: "1", values: { ...defaults, l1_range: range }, w: 96, h: 34, nowMs: 0 });
  check(printed.includes(want), `LFO 1 rate card at Range ${range} reads ${want} (${printed.join(" | ")})`);
}

// ---- pages
const canvas = load("canvas.js");
check(canvas.canvas_overlay && typeof canvas.canvas_overlay.drawPage === "function", "canvas.js exports drawPage");
for (const [w, h] of [[128, 40], [128, 34], [120, 44]]) {
  const home = makeCtx(w, h);
  canvas.canvas_overlay.drawPage(home.ctx, { values: { ...defaults, preset_name: "Lucid Dream", flt_res: "0.7" }, nowMs: 0 });
  check(home.outsideCount() === 0 && home.px.some((v) => v), `home page ${w}x${h} draws inside its band`);
  if (pngDir && w === 128 && h === 40) png(path.join(pngDir, "page_home.png"), home.px, w, h);
  // the bank is 11 patches committed, 540 with the local Peak banks
  const n = meta.names.length, mid = Math.min(200, n - 1);
  for (const index of new Set([0, Math.min(11, n - 1), mid, n - 1])) {
    const br = makeCtx(w, h);
    canvas.canvas_overlay.drawPage(br.ctx, { values: {}, preset: { name: meta.names[index], index, count: n, entered: index === mid } });
    check(br.outsideCount() === 0 && br.px.some((v) => v), `browser ${w}x${h} at ${index} draws inside its band`);
    if (pngDir && w === 128 && h === 40 && index === mid) png(path.join(pngDir, "page_browser.png"), br.px, w, h);
  }
}
// a home page with no values at all (the first frame before any read answered)
const empty = makeCtx(128, 40);
canvas.canvas_overlay.drawPage(empty.ctx, {});
check(empty.outsideCount() === 0, "home page with no values");

console.log(`device ui: ${checks - fails} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
