/* Single-file build only (scripts/build_single.py): unpacks the payload the build put in the page
 * and boots the web UI from it. Adapted from Amino's web/embed.js.
 *
 * The payload is gzip(JSON index + file bytes), base64 in a <script> tag: the Peak-free crest.wasm,
 * the engine and worker scripts, the UI modules and their data. Each file becomes a blob: URL under
 * a crest:/ name (the build points app.js's `here` and the UI's imports at crest:/assets/ui/), an
 * import map serves the modules, and fetch, Worker and audioWorklet.addModule resolve the rest. */
(async () => {
  const urls = new Map();
  const types = { js: "text/javascript", json: "application/json", wasm: "application/wasm" };
  const resolve = (url) => urls.get(String(url)) || url;

  try {
    const raw = atob(document.getElementById("crest-payload").textContent.trim());
    const gz = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) gz[i] = raw.charCodeAt(i);
    const all = new Uint8Array(await new Response(
      new Blob([gz]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer());
    const headLen = new DataView(all.buffer).getUint32(0, true);
    const index = JSON.parse(new TextDecoder().decode(all.subarray(4, 4 + headLen)));
    let pos = 4 + headLen;
    for (const [path, length] of index) {
      const type = types[path.split(".").pop()] || "application/octet-stream";
      urls.set("crest:/" + path, URL.createObjectURL(new Blob([all.subarray(pos, pos + length)], { type })));
      pos += length;
    }
  } catch (e) {
    document.getElementById("app").textContent = "Crest could not unpack itself: " + e.message
      + ". It needs a current Chrome, Edge, Firefox or Safari.";
    return;
  }

  const realFetch = window.fetch.bind(window);
  window.fetch = (url, opts) => realFetch(resolve(url), opts);
  const RealWorker = window.Worker;
  window.Worker = class extends RealWorker { constructor(url, opts) { super(resolve(url), opts); } };
  if (window.AudioWorklet) {
    const addModule = AudioWorklet.prototype.addModule;
    AudioWorklet.prototype.addModule = function (url, opts) { return addModule.call(this, resolve(url), opts); };
  }

  const imports = {};
  for (const [name, url] of urls) if (name.endsWith(".js")) imports[name] = url;
  const map = document.createElement("script");
  map.type = "importmap";
  map.textContent = JSON.stringify({ imports });
  document.head.append(map);
  const app = document.createElement("script");
  app.type = "module";
  app.textContent = 'import "crest:/assets/ui/app.js";';
  document.body.append(app);
})();
