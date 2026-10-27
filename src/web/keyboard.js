/* Playing the browser engine: on-screen keys (higher on a key = softer), the computer keyboard
 * (A..K row, Z/X octave), mod wheel and pitch bend strips, Web MIDI input, and the transport that
 * clocks tempo-synced LFOs, the delay and the arpeggiator. Only built in the standalone page. */
import { h } from "./controls.js";
import { noteName } from "./format.js";

const KEYMAP = "awsedftgyhujkolp;'";

export function buildPlayer(link) {
  let octave = 4;
  const down = new Map(); // note -> source
  const keys = [];
  const send = (m) => link.midi(m);
  const noteOn = (n, v, src) => { if (down.has(n)) return; down.set(n, src); send([0x90, n, v]); paint(); };
  const noteOff = (n) => { if (!down.has(n)) return; down.delete(n); send([0x80, n, 0]); paint(); };
  const allOff = () => { for (const n of [...down.keys()]) noteOff(n); };

  const kb = h("div", { class: "keys", role: "group", "aria-label": "Keyboard" });
  function build() {
    kb.textContent = "";
    keys.length = 0;
    const first = 12 * (octave + 1);
    for (let i = 0; i < 25; i++) {
      const n = first + i, black = [1, 3, 6, 8, 10].includes(n % 12);
      const k = h("div", { class: "key" + (black ? " black" : ""), "data-note": n },
        !black && n % 12 === 0 ? h("span", { class: "note" }, noteName(n)) : null,
        i < KEYMAP.length ? h("span", { class: "hint" }, KEYMAP[i].toUpperCase()) : null);
      k.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        k.setPointerCapture(e.pointerId);
        const r = k.getBoundingClientRect();
        const vel = Math.max(20, Math.min(127, Math.round(127 - (e.clientY - r.top) / r.height * 80)));
        noteOn(n, vel, "pointer");
      });
      const up = () => noteOff(n);
      k.addEventListener("pointerup", up);
      k.addEventListener("pointercancel", up);
      kb.append(k);
      keys.push(k);
    }
    paint();
  }
  function paint() { for (const k of keys) k.classList.toggle("down", down.has(Number(k.dataset.note))); }

  const typing = (e) => /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || e.target.isContentEditable;
  window.addEventListener("keydown", (e) => {
    if (e.repeat || e.metaKey || e.ctrlKey || e.altKey || typing(e)) return;
    const i = KEYMAP.indexOf(e.key.toLowerCase());
    if (i >= 0) { noteOn(12 * (octave + 1) + i, 100, "kbd:" + e.key.toLowerCase()); e.preventDefault(); }
    else if (e.key === "z" || e.key === "x") { allOff(); octave = Math.max(0, Math.min(7, octave + (e.key === "x" ? 1 : -1))); octLabel.textContent = "C" + octave; build(); }
  });
  window.addEventListener("keyup", (e) => {
    const i = KEYMAP.indexOf(e.key.toLowerCase());
    if (i >= 0) noteOff(12 * (octave + 1) + i);
  });
  window.addEventListener("blur", allOff);

  /* wheel and bend: two touch strips, the bend springs back */
  const strip = (label, spring, onValue) => {
    const thumb = h("i");
    const s = h("div", { class: "strip", role: "slider", "aria-label": label, tabindex: 0 }, thumb);
    let v = spring ? 0.5 : 0;
    const show = () => { thumb.style.bottom = `calc(${(v * 100).toFixed(1)}% - 3px)`; };
    const at = (e) => { const r = s.getBoundingClientRect(); v = Math.min(1, Math.max(0, 1 - (e.clientY - r.top) / r.height)); show(); onValue(v); };
    s.addEventListener("pointerdown", (e) => { s.setPointerCapture(e.pointerId); at(e); });
    s.addEventListener("pointermove", (e) => { if (s.hasPointerCapture(e.pointerId)) at(e); });
    s.addEventListener("pointerup", () => { if (spring) { v = 0.5; show(); onValue(v); } });
    show();
    return h("div", { class: "strip-wrap" }, s, h("span", { class: "lbl" }, label));
  };
  const bend = strip("Bend", true, (v) => { const b = Math.round(v * 16383); send([0xe0, b & 127, b >> 7]); });
  const wheel = strip("Mod", false, (v) => send([0xb0, 1, Math.round(v * 127)]));

  const octLabel = h("span", { class: "oct" }, "C4");
  const octDown = h("button", { type: "button", class: "chip", "aria-label": "Octave down", onclick: () => { allOff(); octave = Math.max(0, octave - 1); octLabel.textContent = "C" + octave; build(); } }, "−");
  const octUp = h("button", { type: "button", class: "chip", "aria-label": "Octave up", onclick: () => { allOff(); octave = Math.min(7, octave + 1); octLabel.textContent = "C" + octave; build(); } }, "+");

  /* transport */
  const bpm = h("input", { type: "number", min: 40, max: 240, value: 120, "aria-label": "Tempo (BPM)" });
  const run = h("input", { type: "checkbox", "aria-label": "Run the transport" });
  const tempo = () => link.tempo(Number(bpm.value) || 120, run.checked);
  bpm.addEventListener("change", tempo);
  run.addEventListener("change", tempo);

  /* Web MIDI */
  const midiSel = h("select", { "aria-label": "MIDI input" }, h("option", { value: "" }, "No MIDI input"));
  const midiIn = h("label", { class: "sel" }, h("span", { class: "lbl" }, "MIDI"), midiSel);
  if (navigator.requestMIDIAccess) {
    navigator.requestMIDIAccess().then((access) => {
      const fill = () => {
        const cur = midiSel.value;
        midiSel.textContent = "";
        midiSel.append(h("option", { value: "" }, "No MIDI input"), h("option", { value: "*" }, "All inputs"));
        for (const inp of access.inputs.values()) midiSel.append(h("option", { value: inp.id }, inp.name));
        midiSel.value = cur || (access.inputs.size ? "*" : "");
      };
      const onMsg = (e) => {
        if (midiSel.value !== "*" && midiSel.value !== e.target.id) return;
        const d = e.data;
        if (d[0] >= 0xf0) return;
        send([d[0] & 0xf0, d[1] | 0, d[2] | 0]); // all channels into the one instance
      };
      for (const inp of access.inputs.values()) inp.onmidimessage = onMsg;
      access.onstatechange = () => { for (const inp of access.inputs.values()) inp.onmidimessage = onMsg; fill(); };
      fill();
    }).catch(() => { midiSel.disabled = true; midiSel.firstChild.textContent = "MIDI unavailable"; });
  } else {
    midiSel.disabled = true;
    midiSel.firstChild.textContent = "No Web MIDI here";
  }

  build();
  return h("footer", { class: "player" },
    h("div", { class: "strips" }, bend, wheel),
    kb,
    h("div", { class: "player-ctl" },
      h("div", { class: "row" }, octDown, octLabel, octUp),
      h("label", { class: "sel" }, h("span", { class: "lbl" }, "BPM"), bpm),
      h("label", { class: "sel chk" }, run, h("span", {}, "Play transport")),
      midiIn,
      h("p", { class: "hint-text" }, "Keys A–K play, Z / X shift the octave.")));
}
