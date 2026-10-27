/* Main-thread side of the browser engine: owns the AudioContext and the worklet node running
 * crest.wasm, and wraps the worklet's message protocol (web/worklet.js) in a small API.
 * Loaded by the web UI only when it runs without a Move (no window.schwungRemote). */

export class Engine {
  constructor(ctx, node) {
    this.ctx = ctx;
    this.node = node;
    this.pending = new Map();
    this.nextId = 1;
    this.onPeak = null;
    node.port.onmessage = (e) => {
      const d = e.data;
      if (d.t === "got") {
        const done = this.pending.get(d.id);
        this.pending.delete(d.id);
        if (done) done(d.val);
      } else if (d.t === "peak" && this.onPeak) this.onPeak(d.v);
    };
  }

  /** Must run inside a user gesture so the AudioContext may start. base = folder of this file. */
  static async start(base) {
    const ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
    const [wasm] = await Promise.all([
      fetch(base + "crest.wasm").then((r) => {
        if (!r.ok) throw new Error("crest.wasm: HTTP " + r.status);
        return r.arrayBuffer();
      }),
      ctx.audioWorklet.addModule(base + "worklet.js"),
    ]);
    const node = new AudioWorkletNode(ctx, "crest", {
      numberOfInputs: 0,
      outputChannelCount: [2],
      processorOptions: { wasm },
    });
    const info = await new Promise((resolve) => {
      node.port.onmessage = (e) => { if (e.data.t === "ready") resolve(e.data); };
    });
    if (!info.ok) throw new Error("engine failed to start: " + (info.error || "wa_init"));
    const analyser = ctx.createAnalyser();
    analyser.fftSize = 2048;
    analyser.smoothingTimeConstant = 0.6;
    node.connect(ctx.destination);
    node.connect(analyser);
    if (ctx.state !== "running") await ctx.resume();
    const engine = new Engine(ctx, node);
    engine.analyser = analyser;
    return engine;
  }

  set(key, val) { this.node.port.postMessage({ t: "set", key, val: String(val) }); }

  get(key) {
    const id = this.nextId++;
    return new Promise((resolve) => {
      this.pending.set(id, resolve);
      this.node.port.postMessage({ t: "get", id, key });
    });
  }

  midi(m) { this.node.port.postMessage({ t: "midi", m }); }

  tempo(bpm, run) { this.node.port.postMessage({ t: "tempo", bpm, run }); }
}
