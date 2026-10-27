/* Crest web UI: boots on a Move (inside Schwung's Remote UI, through schwungRemote) or standalone
 * with the synth compiled to WebAssembly and played from this page. */
import { Store } from "./store.js";
import { DeviceLink, LocalLink } from "./link.js";
import { h } from "./controls.js";
import { reading } from "./format.js";
import { buildPanel } from "./panel.js";
import { scope } from "./viz.js";

const here = new URL(".", import.meta.url).href;

async function fetchJson(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(url.split("/").pop() + ": HTTP " + r.status);
  return r.json();
}

/* ------------------------------------------------------------------ the screen */

function buildScreen(ui) {
  const { store, meta } = ui;
  const top = h("div", { class: "scr-top" });
  const big = h("div", { class: "scr-big" });
  const sub = h("div", { class: "scr-sub" });
  const el = h("div", { class: "screen", role: "status", "aria-live": "polite" }, top, big, sub);
  let focus = null, until = 0, timer = 0;
  const bankOf = (i) => meta.banks.find((b) => i >= b[1] && i < b[1] + b[2]);
  const paint = () => {
    const now = performance.now();
    if (focus && now < until) {
      const p = store.p(focus);
      top.textContent = p.n.toUpperCase();
      big.textContent = reading(p, store.get(focus));
      sub.textContent = store.extra.preset_name || "";
      el.classList.add("param");
      return;
    }
    focus = null;
    el.classList.remove("param");
    const i = Number(store.extra.preset ?? -1);
    const b = bankOf(i);
    top.textContent = b ? `${b[0]} · ${i - b[1] + 1}/${b[2]}` : store.extra.user_bank || "CREST";
    big.textContent = store.extra.preset_name || "Init Saw";
    sub.textContent = ui.link?.kind === "device" ? "Move · " + ((window.schwungRemote && window.schwungRemote.component) || "synth") : "Browser engine";
  };
  ui.showParam = (key, hold = 1600) => {
    focus = key;
    until = performance.now() + hold;
    paint();
    clearTimeout(timer);
    timer = setTimeout(paint, hold + 20);
  };
  store.onAny((keys) => {
    if (focus && keys.includes(focus)) paint();
    else if (keys.includes("preset_name") || keys.includes("preset") || keys.includes("user_bank")) paint();
    else if (store.touched && keys.includes(store.touched)) ui.showParam(store.touched);
  });
  paint();
  return el;
}

/* ------------------------------------------------------------------ user banks (Peak .syx) */

/* Peak SysEx banks converted in the browser by web/peak_worker.js (crest.wasm: the same converter and
 * loudness solve as the factory banks). Kept in this browser's storage as differences from the
 * defaults; the Move loads its own from a folder instead (see the Patches dialog note). */
const USER_KEY = "crest.userBanks.v1";

function userBanks(ui) {
  const { meta } = ui;
  const banks = [];
  try { banks.push(...JSON.parse(localStorage.getItem(USER_KEY) || "[]")); } catch { /* storage blocked */ }
  const save = () => {
    try { localStorage.setItem(USER_KEY, JSON.stringify(banks)); }
    catch { ui.toast("This browser would not store the bank; it stays until the page is closed."); }
  };
  const compact = (values) => {
    const d = [];
    meta.params.forEach((p, i) => { if (!p.vol && Math.abs(values[i] - p.d) > 1e-6) d.push(i, Math.round(values[i] * 1e5) / 1e5); });
    return d;
  };
  /** The patch as a state blob, for ui.loadState. */
  const state = (b, i) => {
    const pt = banks[b].patches[i];
    const v = meta.params.map((p) => p.d);
    for (let j = 0; j < pt.d.length; j += 2) v[pt.d[j]] = pt.d[j + 1];
    const st = { fmt: "crest", v: "1", name: pt.name };
    meta.params.forEach((p, k) => {
      if (p.vol) return;
      let x = p.k === "patch_vol" && pt.vol !== undefined ? pt.vol : v[k];
      st[p.k] = p.t === "float" ? x.toFixed(4) : String(Math.round(x));
    });
    return st;
  };
  let worker = null, wasm = null, onChange = () => {};
  const api = {
    banks, state,
    set onChange(fn) { onChange = fn; },
    remove(b) { banks.splice(b, 1); save(); onChange(); },
    async importFile(file) {
      if (ui.link?.kind === "device") throw new Error("on the Move, load banks from the Peak Banks page");
      const bytes = await file.arrayBuffer();
      wasm = wasm || await fetch(new URL("../engine/crest.wasm", here)).then((r) => {
        if (!r.ok) throw new Error("crest.wasm: HTTP " + r.status);
        return r.arrayBuffer();
      });
      worker = worker || new Worker(new URL("../engine/peak_worker.js", here));
      return new Promise((resolve, reject) => {
        let bank = null, levelled = 0, timer = 0;
        const later = () => { clearTimeout(timer); timer = setTimeout(save, 500); };
        worker.onmessage = (e) => {
          const d = e.data;
          if (d.t === "bank") {
            bank = { name: d.file.replace(/\.syx$/i, ""), patches: d.patches.map((p) => ({ name: p.name, d: compact(p.values) })) };
            banks.push(bank);
            save();
            onChange();
            resolve(banks.length - 1);
          } else if (d.t === "level" && bank) {
            bank.patches[d.i].vol = Math.round(d.vol * 1e4) / 1e4;
            bank.levelled = ++levelled;
            later();
            onChange();
          } else if (d.t === "done") {
            if (bank) { bank.levelled = bank.patches.length; save(); onChange(); }
          } else if (d.t === "error") {
            reject(new Error(d.message));
          }
        };
        worker.onerror = (err) => reject(new Error(err.message || "the import worker failed"));
        const copy = wasm.slice(0); // the worker takes its own copy, the next import still has one
        worker.postMessage({ t: "import", wasm: copy, bytes, file: file.name }, [copy, bytes]);
      });
    },
  };
  return api;
}

/* ------------------------------------------------------------------ patch browser */

function buildBrowser(ui, loadPreset) {
  const { meta, store } = ui;
  const user = ui.user;
  const search = h("input", { type: "search", placeholder: "Search patches", "aria-label": "Search patches" });
  const bankBar = h("div", { class: "banks", role: "tablist", "aria-label": "Banks" });
  const list = h("div", { class: "plist", role: "listbox", "aria-label": "Patches" });
  const close = h("button", { type: "button", class: "chip", "aria-label": "Close", onclick: () => dlg.close() }, "Close");
  const file = h("input", { type: "file", accept: ".syx,application/octet-stream", hidden: true, multiple: true });
  const status = h("span", { class: "dim small", role: "status" });
  const device = ui.link?.kind === "device" || !!window.schwungRemote;
  const load = h("button", { type: "button", class: "chip", onclick: () => file.click(), hidden: device }, "Load Peak .syx…");
  const note = h("p", { class: "dim small" }, device
    ? "Peak banks are converted from Novation Peak SysEx. On the Move, put your own .syx banks anywhere under UserLibrary/Crest and open them from the Peak Banks page."
    : "Peak banks are converted from Novation Peak SysEx. Load your own Peak .syx bank or patch to convert it here; it stays in this browser.");
  const dlg = h("dialog", { class: "browser", "aria-label": "Patches" },
    h("div", { class: "browser-head" }, h("h2", {}, "Patches"), search, load, file, close), bankBar, list,
    h("div", { class: "browser-foot" }, note, status));
  /* tab = ["f", factory bank index] or ["u", user bank index] */
  let tab = ["f", 0], cur = null; // cur: the user patch last loaded, [bank, index]
  file.addEventListener("change", async () => {
    const files = [...file.files];
    file.value = "";
    for (const f of files) {
      status.textContent = "Converting " + f.name + "…";
      try {
        const b = await user.importFile(f);
        tab = ["u", b];
        render();
      } catch (err) {
        status.textContent = "";
        ui.toast("Could not load " + f.name + ": " + err.message);
      }
    }
  });
  user.onChange = () => { if (dlg.open) render(); };
  const item = (num, name, selected, onpick) => h("button", {
    type: "button", role: "option", class: "pitem", "aria-selected": String(selected),
    onclick: () => { onpick(); render(); }, ondblclick: () => dlg.close(),
  }, h("span", { class: "pnum" }, num), name);
  const pickUser = (b, i) => {
    cur = [b, i];
    ui.loadState(user.state(b, i));
    store.ingest({ user_bank: `${user.banks[b].name} · ${i + 1}/${user.banks[b].patches.length}` });
  };
  function render() {
    const q = search.value.trim().toLowerCase();
    const sel = Number(store.extra.preset ?? -1);
    bankBar.textContent = "";
    meta.banks.forEach((b, i) => bankBar.append(h("button", {
      type: "button", role: "tab", class: "tab", "aria-selected": String(!q && tab[0] === "f" && tab[1] === i),
      onclick: () => { tab = ["f", i]; search.value = ""; render(); },
    }, b[0], h("span", { class: "dim" }, " " + b[2]))));
    user.banks.forEach((b, i) => bankBar.append(h("span", { class: "tab-wrap" },
      h("button", {
        type: "button", role: "tab", class: "tab user", "aria-selected": String(!q && tab[0] === "u" && tab[1] === i),
        onclick: () => { tab = ["u", i]; search.value = ""; render(); },
      }, b.name, h("span", { class: "dim" }, " " + b.patches.length)),
      h("button", {
        type: "button", class: "tab-x", "aria-label": "Remove " + b.name, title: "Remove this bank",
        onclick: () => { user.remove(i); if (tab[0] === "u") tab = ["f", 0]; cur = null; render(); },
      }, "×"))));
    list.textContent = "";
    let n = 0;
    if (q) {
      meta.names.forEach((nm, i) => { if (nm.toLowerCase().includes(q)) { n++; list.append(item(String(i), nm, i === sel, () => loadPreset(i))); } });
      user.banks.forEach((b, bi) => b.patches.forEach((p, i) => {
        if (p.name.toLowerCase().includes(q)) { n++; list.append(item(b.name.slice(0, 3) + (i + 1), p.name, !!cur && cur[0] === bi && cur[1] === i, () => pickUser(bi, i))); }
      }));
    } else if (tab[0] === "f") {
      const b = meta.banks[tab[1]];
      for (let i = b[1]; i < b[1] + b[2]; i++) { n++; list.append(item(String(i), meta.names[i], i === sel, () => loadPreset(i))); }
    } else {
      const b = user.banks[tab[1]];
      if (b) b.patches.forEach((p, i) => { n++; list.append(item(String(i + 1), p.name, !!cur && cur[0] === tab[1] && cur[1] === i, () => pickUser(tab[1], i))); });
    }
    if (!n) list.append(h("p", { class: "dim" }, "No patch matches."));
    const busy = user.banks.find((b) => b.levelled !== undefined && b.levelled < b.patches.length);
    status.textContent = busy ? `Levelling ${busy.name}: ${busy.levelled} of ${busy.patches.length}` : "";
  }
  search.addEventListener("input", render);
  ui.openBrowser = () => {
    const sel = Number(store.extra.preset ?? 0);
    if (sel >= 0) {
      const b = meta.banks.findIndex((x) => sel >= x[1] && sel < x[1] + x[2]);
      if (b >= 0) tab = ["f", b];
    }
    render();
    dlg.showModal();
    list.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: "center" });
    search.focus();
  };
  return dlg;
}

/* ------------------------------------------------------------------ header */

function buildHeader(ui, loadPreset, screen) {
  const { store, meta } = ui;
  const step = (d) => {
    const cur = Number(store.extra.preset ?? 0);
    loadPreset((cur + d + meta.names.length) % meta.names.length);
  };
  const undo = h("button", { type: "button", class: "chip", title: "Undo (⌘Z)", onclick: () => store.undo() }, "Undo");
  const redo = h("button", { type: "button", class: "chip", title: "Redo (⇧⌘Z)", onclick: () => store.redo() }, "Redo");
  store.onAny(() => { undo.disabled = !store.undoStack.length; redo.disabled = !store.redoStack.length; });
  undo.disabled = redo.disabled = true;

  const file = h("input", { type: "file", accept: ".json,application/json", hidden: true });
  file.addEventListener("change", async () => {
    const f = file.files[0];
    file.value = "";
    if (!f) return;
    try {
      const text = await f.text();
      const st = JSON.parse(text);
      if (st.fmt !== "crest") throw new Error("not a Crest patch");
      ui.loadState(st);
    } catch (err) { ui.toast("Could not load the patch: " + err.message); }
  });
  const save = () => {
    const name = (store.extra.preset_name || "Crest").replace(/[^\w\- ]+/g, "").trim() || "Crest";
    const a = h("a", { download: name + ".crest.json", href: URL.createObjectURL(new Blob([store.stateBlob()], { type: "application/json" })) });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  };
  const init = () => ui.loadState({ fmt: "crest", name: "Init" });

  return h("header", { class: "top" },
    h("div", { class: "brand" }, h("span", { class: "logo" }, "CREST"), h("span", { class: "tag" }, "hybrid polysynth")),
    h("div", { class: "nav" },
      h("button", { type: "button", class: "chip", "aria-label": "Previous patch", onclick: () => step(-1) }, "◀"),
      screen,
      h("button", { type: "button", class: "chip", "aria-label": "Next patch", onclick: () => step(1) }, "▶")),
    h("div", { class: "actions" },
      h("button", { type: "button", class: "chip primary", onclick: () => ui.openBrowser() }, "Patches"),
      h("button", { type: "button", class: "chip", onclick: init }, "Init"),
      undo, redo,
      h("button", { type: "button", class: "chip", onclick: save, title: "Download this patch" }, "Save"),
      h("button", { type: "button", class: "chip", onclick: () => file.click(), title: "Load a patch file" }, "Load"), file),
    h("div", { class: "perf" },
      ui.ledPerf("anim1", "Animate 1"), ui.ledPerf("anim2", "Animate 2"), ui.ledPerf("arp_on", "Arp"),
      ui.knobPerf("patch_vol", "Volume")));
}

/* ------------------------------------------------------------------ boot */

async function boot() {
  const root = document.getElementById("app");
  let meta;
  try {
    meta = await fetchJson(here + "ui_meta.json");
  } catch (err) {
    root.textContent = "Could not load Crest's UI data (" + err.message + ").";
    return;
  }
  const store = new Store(meta);
  const remote = window.schwungRemote || null;
  const ui = {
    store, meta, link: null,
    touch(key) { ui.showParam?.(key); },
    hover(key) { if (key) ui.showParam?.(key, 1200); },
    toast(msg) {
      const t = h("div", { class: "toast", role: "alert" }, msg);
      document.body.append(t);
      setTimeout(() => t.remove(), 4000);
    },
  };
  const { led, knob } = await import("./controls.js");
  ui.ledPerf = (k, label) => led(ui, k, { label, class: "big" });
  ui.knobPerf = (k, label) => knob(ui, k, { label, size: "s" });

  const PATCH_HOLD_MS = 1500; // how long a patch load ignores the Move's (possibly stale) reports
  let presets = null; // ui_presets.json, fetched on first use
  const presetValues = async (i) => {
    presets = presets || await fetchJson(here + "ui_presets.json");
    const vals = {};
    for (const p of meta.params) if (!p.vol) vals[p.k] = p.d;
    const flat = presets[i] || [];
    for (let j = 0; j < flat.length; j += 2) vals[meta.params[flat[j]].k] = flat[j + 1];
    return vals;
  };
  const loadPreset = async (i) => {
    if (ui.link?.kind === "device") store.holdPatch(PATCH_HOLD_MS);
    store.command("preset", i);
    store.undoStack.length = store.redoStack.length = 0;
    if (ui.link?.kind === "device") store.apply(await presetValues(i), { send: false });
    store.ingest({ preset: String(i), preset_name: meta.names[i], user_bank: "" }, false, true);
    if (ui.link?.kind === "device") store.holdPatch(PATCH_HOLD_MS);
  };
  ui.loadState = (st) => {
    const vals = {};
    for (const p of meta.params) if (!p.vol) vals[p.k] = st[p.k] !== undefined ? Number(st[p.k]) : p.d;
    if (ui.link?.kind === "device") store.holdPatch(PATCH_HOLD_MS);
    store.command("state", JSON.stringify(st));
    store.apply(vals, { send: false });
    store.ingest({ preset_name: st.name || "Init", preset: "-1", user_bank: "" }, false, true);
    if (ui.link?.kind === "device") store.holdPatch(PATCH_HOLD_MS);
  };

  ui.user = userBanks(ui);
  const screen = buildScreen(ui);
  const header = buildHeader(ui, loadPreset, screen);
  const panel = buildPanel(ui);
  const browser = buildBrowser(ui, loadPreset);
  root.textContent = "";
  root.append(header, panel, browser);

  window.addEventListener("keydown", (e) => {
    if (!(e.metaKey || e.ctrlKey) || e.key.toLowerCase() !== "z") return;
    if (/^(INPUT|TEXTAREA)$/.test(e.target.tagName)) return;
    e.preventDefault();
    if (e.shiftKey) store.redo(); else store.undo();
  });

  if (remote) {
    ui.link = new DeviceLink(remote);
    store.connect(ui.link);
    document.documentElement.classList.add("on-device");
    return;
  }

  /* standalone: the engine needs a click before audio may start */
  document.documentElement.classList.add("standalone");
  const err = h("p", { class: "err", role: "alert" });
  const start = h("button", { type: "button", class: "start" }, "Start Crest");
  const gate = h("div", { class: "gate" }, h("div", { class: "gate-card" },
    h("span", { class: "logo" }, "CREST"),
    h("p", {}, "An 8-voice hybrid polysynth for Ableton Move, after the Novation Peak. This is the same engine, compiled to WebAssembly and running in your browser."),
    h("p", { class: "dim" }, "Play it with the keys below, your computer keyboard or a MIDI keyboard."),
    start, err));
  document.body.append(gate);
  start.addEventListener("click", async () => {
    start.disabled = true;
    start.textContent = "Starting…";
    try {
      const { Engine } = await import(new URL("../engine/engine.js", here).href);
      const engine = await Engine.start(new URL("../engine/", here).href);
      const link = new LocalLink(engine);
      ui.link = link;
      store.connect(link);
      await link.init();
      const { buildPlayer } = await import("./keyboard.js");
      document.body.append(buildPlayer(link));
      screen.prepend(scope(engine.analyser));
      const meter = h("i", { class: "meter-fill" });
      screen.append(h("span", { class: "meter", "aria-hidden": "true" }, meter));
      engine.onPeak = (v) => { meter.style.width = Math.min(100, v * 100).toFixed(1) + "%"; meter.classList.toggle("hot", v > 0.95); };
      gate.remove();
    } catch (e) {
      start.disabled = false;
      start.textContent = "Start Crest";
      err.textContent = "The engine could not start: " + e.message;
    }
  });
}

boot();
