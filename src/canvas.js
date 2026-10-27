/* Crest: module-drawn pages on the Move display (Schwung `canvas` params with `as_page`, plan/12).
 *
 *  home    the first page of the root level: patch name, the filter response and the amp envelope,
 *          redrawn live while the eight root knobs (Cut Res Env1 PreOD, A D S R) turn.
 *  browse  the factory-patch browser: which bank you are in, where in it, and the patch name.
 *
 * One overlay serves both pages: the host hands the browser page a `preset` object and the home
 * page none. drawPage gets values, never reads (Schwung docs, "Custom UI pages").
 */
(function () {
  /* BEGIN GENERATED META */
  var META = {"banks":[["Crest",0,11]],"types":["LP24","LP12","BP12","BP6","HP24","HP12"]};
  /* END GENERATED META */

  function num(v) {
    if (v === null || v === undefined || v === "") return null;
    var x = Number(v);
    return isFinite(x) ? x : null;
  }

  function val(values, key, d) {
    if (!values) return d;
    var v = Object.prototype.hasOwnProperty.call(values, key) ? values[key] : undefined;
    if (v === undefined) {
      var suffix = ":" + key;
      for (var k in values) if (k.length > suffix.length && k.slice(-suffix.length) === suffix) { v = values[k]; break; }
    }
    var x = num(v);
    return x === null ? d : x;
  }

  function str(values, key) {
    if (!values) return "";
    if (values[key] !== undefined && values[key] !== null) return String(values[key]);
    var suffix = ":" + key;
    for (var k in values) if (k.slice(-suffix.length) === suffix && values[k] !== null) return String(values[k]);
    return "";
  }

  function tw(ctx, s) {
    if (typeof ctx.measureText === "function") return ctx.measureText(s);
    return typeof ctx.textWidth === "function" ? ctx.textWidth(s) : s.length * 6;
  }

  function fit(ctx, s, maxW) {
    s = String(s);
    if (tw(ctx, s) <= maxW) return s;
    while (s.length > 1 && tw(ctx, s + ".") > maxW) s = s.slice(0, -1);
    return s + ".";
  }

  function line(ctx, x0, y0, x1, y1) {
    if (typeof ctx.line === "function") { ctx.line(Math.round(x0), Math.round(y0), Math.round(x1), Math.round(y1), 1); return; }
    var n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0), 1);
    for (var i = 0; i <= n; i++) ctx.fillRect(Math.round(x0 + (x1 - x0) * i / n), Math.round(y0 + (y1 - y0) * i / n), 1, 1, 1);
  }

  function dotted(ctx, x, y, w) { for (var i = 0; i < w; i += 2) ctx.fillRect(x + i, y, 1, 1, 1); }

  /* ---------------------------------------------------------------- home */

  function filterCurve(ctx, values, x, y, w, h) {
    var cut = val(values, "flt_cut", 0.75), res = val(values, "flt_res", 0.1);
    var type = (META.types || [])[Math.round(val(values, "flt_type", 0))] || "LP24";
    var fc = 440 * Math.pow(2, (20 + 115 * cut - 69) / 12), q = 0.5 + res * res * 8;
    var px = null, py = null;
    for (var i = 0; i < w; i++) {
      var f = 20 * Math.pow(1000, i / (w - 1)), r = f / fc;
      var m = 1 / Math.sqrt(Math.pow(1 - r * r, 2) + Math.pow(r / q, 2));
      if (type.charAt(0) === "H") m *= r * r;
      else if (type.charAt(0) === "B") m *= r / q;
      if (type.slice(2) === "24") m *= type.charAt(0) === "H" ? r * r / Math.sqrt(1 + r * r * r * r) : 1 / Math.sqrt(1 + r * r * r * r);
      var db = 20 * Math.log(Math.max(1e-4, m)) / Math.LN10;
      var yy = y + Math.round((1 - Math.max(0, Math.min(1, (db + 36) / 54))) * (h - 1));
      if (px !== null) line(ctx, px, py, x + i, yy);
      px = x + i; py = yy;
    }
    dotted(ctx, x, y + h, w);
    ctx.print(x, y + h + 2, type, 1);
  }

  function ampEnv(ctx, values, x, y, w, h) {
    var a = val(values, "ae_a", 0.02), d = val(values, "ae_d", 0.45), s = val(values, "ae_s", 0.8), r = val(values, "ae_r", 0.3);
    var la = 0.1 + a, ld = 0.1 + d, ls = 0.4, lr = 0.1 + r, tot = la + ld + ls + lr;
    var X = function (t) { return x + Math.round(t / tot * (w - 1)); };
    var Y = function (v) { return y + Math.round((1 - v) * (h - 1)); };
    var pts = [[0, 0], [la, 1], [la + ld, s], [la + ld + ls, s], [tot, 0]];
    for (var i = 0; i < 4; i++) line(ctx, X(pts[i][0]), Y(pts[i][1]), X(pts[i + 1][0]), Y(pts[i + 1][1]));
    dotted(ctx, x, y + h, w);
    ctx.print(x, y + h + 2, "AMP", 1);
  }

  function drawHome(ctx, o) {
    var W = ctx.width || 128, H = ctx.height || 40;
    var values = o.values || {};
    var name = str(values, "preset_name") || "Crest";
    ctx.print(0, 0, fit(ctx, name, W - 30), 1);
    var tag = "CREST";
    ctx.print(W - tw(ctx, tag), 0, tag, 1);
    ctx.fillRect(0, 9, W, 1, 1);
    var top = 12, gh = Math.max(8, H - top - 10);
    var half = Math.floor((W - 6) / 2);
    filterCurve(ctx, values, 0, top, half, gh);
    ampEnv(ctx, values, half + 6, top, W - half - 6, gh);
  }

  /* ---------------------------------------------------------------- browser */

  function bankOf(index) {
    var banks = META.banks || [];
    for (var i = 0; i < banks.length; i++)
      if (index >= banks[i][1] && index < banks[i][1] + banks[i][2]) return { b: banks[i], i: i };
    return null;
  }

  function drawBrowser(ctx, o) {
    var W = ctx.width || 128, H = ctx.height || 40;
    var p = o.preset || {};
    var index = num(p.index) || 0, count = num(p.count) || 0;
    var bk = bankOf(index);
    var bankName = bk ? bk.b[0] : "Factory";
    var pos = bk ? (index - bk.b[1] + 1) + "/" + bk.b[2] : (index + 1) + "/" + count;
    ctx.print(0, 0, fit(ctx, bankName.toUpperCase(), W - tw(ctx, pos) - 4), 1);
    ctx.print(W - tw(ctx, pos), 0, pos, 1);
    ctx.fillRect(0, 9, W, 1, 1);
    /* the name, framed when the browser is entered (the wheel is browsing) */
    var name = fit(ctx, p.name || "", W - 8);
    var ny = Math.max(12, Math.floor(H / 2) - 6);
    ctx.print(Math.max(0, Math.floor((W - tw(ctx, name)) / 2)), ny + 2, name, 1);
    if (p.entered) {
      ctx.fillRect(2, ny - 1, W - 4, 1, 1);
      ctx.fillRect(2, ny + 11, W - 4, 1, 1);
    }
    /* every bank as a segment of one strip, the current patch as a notch */
    var banks = META.banks || [], total = count || 1, sy = H - 5;
    for (var i = 0; i < banks.length; i++) {
      var x0 = Math.round(banks[i][1] / total * (W - 1)), x1 = Math.round((banks[i][1] + banks[i][2]) / total * (W - 1));
      if (bk && i === bk.i) ctx.fillRect(x0, sy, Math.max(1, x1 - x0 - 1), 3, 1);
      else ctx.fillRect(x0, sy + 1, Math.max(1, x1 - x0 - 1), 1, 1);
    }
    var mx = Math.round(index / total * (W - 1));
    ctx.fillRect(mx, sy - 3, 1, 2, 1);
  }

  globalThis.canvas_overlay = {
    drawPage: function (ctx, o) {
      o = o || {};
      if (o.preset) drawBrowser(ctx, o);
      else drawHome(ctx, o);
    },
  };
})();
