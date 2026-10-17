// check_palette.cjs: painted-contrast proof for the field-office palette and
// the ink-stamp message actions (#282). Drives headless system Chrome over
// CDP with the Node standard library only. Every number is read from the
// rendered page (computed colours, real boxes, hit tests), never from source.
//
// Walks a real signup (Begin -> Create -> I saved it -> vibe -> Meet Major ->
// hive) on a fresh relay, then measures each surface at 1440x900 and 390x844
// with data-theme="field-office":
//   gate cards (splash, landing, backup, vibe, Meet Major), main thread with
//   notes, replies (Thread) and a code note (Download / Canvas), thread pane,
//   Settings, Kit menu, Profile, New channel, every other hive dialog
//   (Manage Channel, robot, invite, providers, relay, leave, dev log, seed,
//   avatar, skill, forge, inventory), and the BOARDS drawer at 390.
// Pins:
//   C1 every visible text run >= 4.5:1 against its effective background
//      (opacity and translucent layers composited; disabled controls exempt);
//      input placeholders too.
//   C2 controls (buttons, inputs, radios, checkboxes, switch tracks) show a border or fill
//      >= 3:1 against what is behind them, unless they are text-only.
//   C3 keyboard focus ring (after a real Tab press) on the message actions,
//      chrome buttons, thread composer, Settings switches / theme radio /
//      Close, New channel and Manage Channel fields is >= 2px and >= 3:1
//      against the background behind it.
//   C4 no near-white paint: no visible text colour or opaque background with
//      relative luminance >= 0.80.
//   R1 the Settings theme radio's computed accent-color equals the theme's
//      own --accent in all 8 themes (main: auto, native blue), and at both
//      widths the checked fill (that accent) is >= 3:1 against the background
//      behind each Settings radio (theme + approval) in all 8 themes (#309).
//   R2 christmas: on each surface view above, re-measured with
//      data-theme="christmas", every text run whose paint changes when
//      --accent / --accent-ink are swapped for sentinels is >= 4.5:1, and
//      every such control is >= 3:1 (#309).
//   R3 christmas: every visible .thread-btn / .chan-voice / .robot-call, under a
//      real mouse hover in each R2 view, keeps its accent text >= 4.5:1 on the
//      hover fill; at least one .thread-btn is hovered at each width (#309 r2).
//   R4 christmas: the hovered .thread-btn (stamp) outline is >= 3:1 against
//      what is behind it (#309 r2). R2 also swaps christmas --accent-fill.
//   R3 also re-opens the BOARDS drawer with /api/status rewritten to
//      "whisper":true (CDP Fetch) so .chan-voice is drawn and hovered; R3s pins
//      .robot-call statically (its only host, #robot-list, is display:none).
//   R5 christmas: every visible border side and outer box-shadow ring (no
//      blur, spread >= 1px) painted from --accent-dim or --accent (sentinel
//      swap) is >= 3:1 against what is behind it, at rest, with each visible
//      field focused, and with the mouse on each .prov-row. Extra christmas
//      views: the Providers hub opened by its real button, and the main thread
//      with a .partial preview fixture. Floor: each claimed rule (R5_CLAIMS)
//      has >= 1 measured instance in its state. The avatar window is opened
//      through its real buttons with a tile picked by a real click, so the
//      selected tile's ring (.avatar-cell.on) is measured too (#309 r5).
//   R6 christmas: each visible switch track is >= 3:1 against its panel and
//      its knob (::before) >= 3:1 against the track (R6k), off and on
//      (checked flipped on the input with no event, then restored).
//   R7 christmas: each visible primary button (#install, #thread-send,
//      .btn not ghost/danger) computes background == --accent-fill; >= 1 per
//      width.
//   R8 the #avatar-preview outer ring (its first box-shadow: the 3px spread
//      ring, or field-office's own 2px offset print shadow) is solid (no blur)
//      and >= 3:1 against the panel behind it, in all 8 themes at both widths,
//      in the real avatar window with a tile picked (#309 r5).
//   R9 the selected avatar tile's (.avatar-cell.on) outer ring, measured the
//      same way, is solid and >= 3:1 against the grid behind it, in all 8
//      themes at both widths, so the picked tile is unmistakable (#309 r6).
//   B1 Thread, Download and Stop are 20-24px tall at both widths; at 390 a
//      point 20px above and below the centre still hits the control (44px).
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// HUSH_TEST_WAIT_S (DevTools ready deadline, default 30), PALETTE_ART (dir:
// writes palette-report.json there when set).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

const RELAY = process.env.HUSH_RELAY_BIN || path.join(__dirname, '..', 'hush-relay');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const liveProcs = [];
const tmpDirs = [];
const failures = [];

/* Every exit path (pass, fail, die, signal) stops the browser and relay this
 * run started and deletes the temp Chrome profile and relay homes it made.
 * Chrome runs in its own process group so its helpers go with it. */
function cleanup() {
  for (const p of liveProcs.splice(0)) {
    try { if (p.pid && p.spawnargs.some((a) => a.startsWith('--user-data-dir='))) process.kill(-p.pid, 'SIGKILL'); else p.kill('SIGKILL'); }
    catch (e) { try { p.kill('SIGKILL'); } catch (e2) { /* gone */ } }
  }
  for (const d of tmpDirs.splice(0)) {
    try { fs.rmSync(d, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 }); } catch (e) { /* best effort */ }
  }
}
process.on('exit', cleanup);
for (const sig of ['SIGINT', 'SIGTERM', 'SIGHUP']) process.on(sig, () => process.exit(1));

function die(msg) {
  for (const f of [...new Set(failures)].slice(0, 40)) console.error('palette check failed: ' + f);
  console.error('palette check failed: ' + msg);
  process.exit(1);
}

function freePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.once('error', reject);
    srv.listen(0, '127.0.0.1', () => {
      const chosen = srv.address().port;
      srv.close((err) => (err ? reject(err) : resolve(chosen)));
    });
  });
}

function resolveChrome() {
  const cands = [process.env.HUSH_CHROME_BIN, 'google-chrome', 'chromium', 'chromium-browser'];
  for (const c of cands) {
    if (!c)
      continue;
    if (c.includes('/') && fs.existsSync(c))
      return c;
    for (const d of (process.env.PATH || '').split(':')) {
      const p = path.join(d, c);
      try { fs.accessSync(p, fs.constants.X_OK); return p; } catch (e) { /* next */ }
    }
  }
  die('no Chrome on PATH (need google-chrome or chromium) and HUSH_CHROME_BIN unset');
}

function waitMs() {
  const raw = process.env.HUSH_TEST_WAIT_S;
  const sec = raw === undefined || raw === '' ? 30 : Number(raw);
  if (!Number.isFinite(sec) || sec <= 0)
    die('HUSH_TEST_WAIT_S must be a positive number of seconds');
  return Math.floor(sec * 1000);
}

/* Starts headless Chrome and polls /json/version until it answers. One
 * relaunch on a deadline miss; an early exit is final. */
async function launchChrome(chrome, deadlineMs) {
  for (let attempt = 1; attempt <= 2; attempt++) {
    const port = await freePort();
    const userDir = fs.mkdtempSync(path.join(os.tmpdir(), 'palette-chrome-'));
    tmpDirs.push(userDir);
    const env = Object.assign({}, process.env);
    delete env.DBUS_SESSION_BUS_ADDRESS;
    const proc = spawn(chrome, ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
      '--disable-dev-shm-usage', '--disable-software-rasterizer', '--hide-scrollbars',
      `--remote-debugging-port=${port}`, `--user-data-dir=${userDir}`, 'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'], env, detached: true });
    liveProcs.push(proc);
    let exited = null;
    proc.once('exit', (code, signal) => { exited = { code, signal }; });
    proc.stderr.on('data', () => {});
    const t0 = Date.now();
    while (Date.now() - t0 < deadlineMs && !exited) {
      try {
        const r = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(400) });
        if (r.ok) {
          console.log(`palette chrome: pid ${proc.pid} port ${port} profile ${userDir}`);
          return { proc, port, userDir };
        }
      } catch (e) { /* not yet */ }
      await sleep(100);
    }
    try { proc.kill('SIGTERM'); } catch (e) { /* gone */ }
    if (exited)
      die(`Chrome exited early code=${exited.code} signal=${exited.signal}`);
  }
  die('Chrome DevTools did not answer before the deadline');
}

class Cdp {
  constructor(url) { this.url = url; this.id = 0; this.waiters = new Map(); }
  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);
      this.ws.onopen = () => resolve();
      this.ws.onerror = (e) => reject(new Error('CDP connect failed: ' + e.message));
      this.ws.onmessage = (ev) => {
        const msg = JSON.parse(ev.data.toString());
        if (msg.id && this.waiters.has(msg.id)) {
          const w = this.waiters.get(msg.id);
          this.waiters.delete(msg.id);
          w(msg.result || {});
        } else if (msg.method === 'Fetch.requestPaused' && this.onPaused) {
          this.onPaused(msg.params);
        } else if (msg.method === 'Page.javascriptDialogOpening') {
          this.send('Page.handleJavaScriptDialog', { accept: true }).catch(() => {});
        }
      };
    });
  }
  send(method, params) {
    const id = ++this.id;
    return new Promise((resolve) => {
      this.waiters.set(id, resolve);
      this.ws.send(JSON.stringify({ id, method, params: params || {} }));
    });
  }
  async eval(expr, awaitPromise) {
    const r = await this.send('Runtime.evaluate',
      { expression: expr, awaitPromise: !!awaitPromise, returnByValue: true });
    if (r.exceptionDetails)
      throw new Error('page eval threw: ' + expr.slice(0, 120) + ' :: ' +
        String((r.exceptionDetails.exception || {}).description || '').slice(0, 300));
    return r.result ? r.result.value : null;
  }
  async waitFor(expr, label, timeout) {
    const t0 = Date.now();
    for (;;) {
      let v = null;
      try { v = await this.eval(expr); } catch (e) { /* page busy */ }
      if (v)
        return v;
      if (Date.now() - t0 > (timeout || 20000))
        die('timeout waiting for ' + (label || expr));
      await sleep(200);
    }
  }
  async click(sel) {
    await this.waitFor(`!!document.querySelector(${JSON.stringify(sel)})`, sel);
    await this.eval(`document.querySelector(${JSON.stringify(sel)}).click()`);
  }
}

/* In-page measuring kit, installed once per document as window.__pal. */
const KIT = `(() => {
  if (window.__pal) return true;
  const lin = (c) => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
  const probe = document.createElement('canvas').getContext('2d', { willReadFrequently: true });
  // Any CSS colour (rgb, oklch, color-mix, ...) -> [r, g, b, a] in sRGB 0-255 via a 1px canvas.
  const cache = new Map();
  const rgba = (s) => {
    if (!s || s === 'transparent') return [0, 0, 0, 0];
    if (cache.has(s)) return cache.get(s);
    const m = s.match(/^rgba?\\(([\\d.]+),\\s*([\\d.]+),\\s*([\\d.]+)(?:,\\s*([\\d.]+))?\\)$/);
    let out;
    if (m) out = [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : +m[4]];
    else {
      probe.clearRect(0, 0, 1, 1); probe.fillStyle = '#000'; probe.fillStyle = s;
      probe.fillRect(0, 0, 1, 1); const d = probe.getImageData(0, 0, 1, 1).data;
      out = [d[0], d[1], d[2], d[3] / 255];
    }
    cache.set(s, out); return out;
  };
  const over = (top, under) => { const a = top[3];
    return [top[0] * a + under[0] * (1 - a), top[1] * a + under[1] * (1 - a), top[2] * a + under[2] * (1 - a), 1]; };
  const lum = (c) => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
  const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
  // Effective background behind el: translucent layers composited onto the first opaque one.
  const bgOf = (el) => {
    const layers = [];
    for (let n = el; n && n.nodeType === 1; n = n.parentElement) {
      const c = rgba(getComputedStyle(n).backgroundColor);
      if (c[3] > 0) { layers.push(c); if (c[3] >= 1) break; }
    }
    let base = [255, 255, 255, 1];
    if (!layers.length || layers[layers.length - 1][3] < 1) layers.push(rgba(getComputedStyle(document.documentElement).backgroundColor));
    for (let i = layers.length - 1; i >= 0; i--) base = layers[i][3] >= 1 ? layers[i] : over(layers[i], base);
    return base;
  };
  const opacityOf = (el) => { let o = 1; for (let n = el; n && n.nodeType === 1; n = n.parentElement) o *= parseFloat(getComputedStyle(n).opacity); return o; };
  const disabled = (el) => !!el.closest(':disabled, [aria-disabled="true"], .is-off');
  const hex = (c) => '#' + c.slice(0, 3).map((v) => Math.round(v).toString(16).padStart(2, '0')).join('');
  const label = (el) => (el.id ? '#' + el.id : el.tagName.toLowerCase() + (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\\s+/).join('.') : ''));
  const path = (el) => { const p = []; for (let n = el; n && n.nodeType === 1 && p.length < 4; n = n.parentElement) p.unshift(label(n)); return p.join(' > '); };
  const onTop = (el, r) => {
    const x = Math.min(Math.max(r.left + Math.min(r.width / 2, 12), 0), innerWidth - 1);
    const y = Math.min(Math.max(r.top + r.height / 2, 0), innerHeight - 1);
    const hit = document.elementFromPoint(x, y);
    return !!hit && (hit === el || el.contains(hit) || hit.contains(el));
  };
  const textRect = (node) => { const rg = document.createRange(); rg.selectNodeContents(node); const rs = [...rg.getClientRects()].filter((q) => q.width > 1 && q.height > 1); return rs[0] || null; };
  window.__pal = { rgba, over, lum, ratio, bgOf, opacityOf, disabled, hex, path, onTop, textRect };
  return true;
})()`;

/* Measures the current viewport. Returns { text, ctl, white, counts }. */
const MEASURE = `(() => {
  const P = window.__pal; const out = { text: [], ctl: [], white: [], n: { text: 0, ctl: 0, exempt: 0, hidden: 0 } };
  const vis = (el) => { const cs = getComputedStyle(el); return cs.visibility === 'visible' && cs.display !== 'none'; };
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  const seen = new Set();
  for (let t = walker.nextNode(); t; t = walker.nextNode()) {
    if (!t.nodeValue.trim()) continue;
    const el = t.parentElement; if (!el || seen.has(el)) continue; seen.add(el);
    if (el.closest('script,style,noscript,template,option')) continue;
    const r = P.textRect(t); if (!r || r.bottom <= 0 || r.top >= innerHeight || r.right <= 0 || r.left >= innerWidth) continue;
    if (!vis(el)) continue;
    const op = P.opacityOf(el); if (op < 0.05) continue;
    if (!P.onTop(el, r)) { out.n.hidden++; continue; }
    if (P.disabled(el)) { out.n.exempt++; continue; }
    const bg = P.bgOf(el); let fg = P.rgba(getComputedStyle(el).color);
    fg = P.over([fg[0], fg[1], fg[2], fg[3] * op], bg);
    const ratio = P.ratio(fg, bg); out.n.text++;
    out.text.push({ ratio: Math.round(ratio * 100) / 100, fg: P.hex(fg), bg: P.hex(bg), el: P.path(el), t: t.nodeValue.trim().slice(0, 40) });
    if (P.lum(fg) >= 0.8) out.white.push({ kind: 'text', c: P.hex(fg), el: P.path(el) });
  }
  // Placeholders of empty visible inputs.
  for (const el of document.querySelectorAll('input[placeholder], textarea[placeholder]')) {
    if (el.value || !el.placeholder) continue; const r = el.getBoundingClientRect();
    if (r.width < 2 || r.height < 2 || r.bottom <= 0 || r.top >= innerHeight || !vis(el) || !P.onTop(el, r) || P.disabled(el)) continue;
    const bg = P.bgOf(el); const ph = getComputedStyle(el, '::placeholder'); let fg = P.rgba(ph.color);
    fg = P.over([fg[0], fg[1], fg[2], fg[3] * P.opacityOf(el) * (parseFloat(ph.opacity) || 1)], bg);
    out.n.text++; out.text.push({ ratio: Math.round(P.ratio(fg, bg) * 100) / 100, fg: P.hex(fg), bg: P.hex(bg), el: P.path(el) + '::placeholder', t: el.placeholder.slice(0, 40) });
  }
  // Controls: a border or fill >= 3:1 against what is behind them, unless text-only.
  /* Radios and checkboxes count too: a native one paints the browser's white
   * disc with no CSS border, so it reads as 0:1 here. */
  const ctlSel = 'button, input[type="text"], input[type="password"], input[type="email"], input:not([type]), textarea, select, .slider, input[type="radio"], input[type="checkbox"]';
  for (const el of document.querySelectorAll(ctlSel)) {
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4 || r.bottom <= 0 || r.top >= innerHeight || r.right <= 0 || r.left >= innerWidth || !vis(el)) continue;
    if (P.opacityOf(el) < 0.05 || !P.onTop(el, r) || P.disabled(el) || el.closest('.switch input:disabled ~ *')) continue;
    /* A borderless composer field is bounded by its .composer-box: measure the box. */
    const host = (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT') && el.closest('.composer-box') ? el.closest('.composer-box') : el;
    const cs = getComputedStyle(host); const behind = host.parentElement ? P.bgOf(host.parentElement) : [255, 255, 255, 1];
    const own = P.rgba(cs.backgroundColor); const fill = own[3] > 0 ? P.over(own, behind) : null;
    const bw = parseFloat(cs.borderTopWidth) || 0; const bc = P.rgba(cs.borderTopColor);
    const border = bw >= 1 && cs.borderTopStyle !== 'none' && bc[3] > 0 ? P.over(bc, behind) : null;
    const rb = border ? P.ratio(border, behind) : 0, rf = fill ? P.ratio(fill, behind) : 0;
    /* A state bar drawn as an inset shadow >= 2px (e.g. the selected channel) counts as the indicator. */
    let ri = 0, bar = null;
    for (const m of (cs.boxShadow || '').matchAll(/(rgba?\\([^)]*\\))\\s+(-?[\\d.]+)px\\s+(-?[\\d.]+)px[^,]*inset/g)) {
      const c = P.rgba(m[1]); if (c[3] <= 0 || Math.max(Math.abs(+m[2]), Math.abs(+m[3])) < 2) continue;
      const under = fill || behind; const r = P.ratio(P.over(c, under), under); if (r > ri) { ri = r; bar = P.hex(P.over(c, under)); }
    }
    const textOnly = !border && !bar && (!fill || rf < 1.05) && el.tagName === 'BUTTON' && el.textContent.trim().length > 0;
    if (textOnly) continue;
    out.n.ctl++;
    out.ctl.push({ ratio: Math.round(Math.max(rb, rf, ri) * 100) / 100, border: border ? P.hex(border) : null, fill: fill ? P.hex(fill) : null, bar, behind: P.hex(behind), el: P.path(host) });
  }
  // Near-white opaque backgrounds.
  for (const el of document.querySelectorAll('body *')) {
    const c = P.rgba(getComputedStyle(el).backgroundColor); if (c[3] < 1) continue;
    const r = el.getBoundingClientRect(); if (r.width * r.height < 16 || r.bottom <= 0 || r.top >= innerHeight || !vis(el) || P.opacityOf(el) < 0.05) continue;
    if (P.lum(c) >= 0.8) out.white.push({ kind: 'bg', c: P.hex(c), el: P.path(el) });
  }
  return out;
})()`;

async function main() {
  const chrome = resolveChrome();
  const { proc: chromeProc, port: cport } = await launchChrome(chrome, waitMs());
  const home = fs.mkdtempSync(path.join(os.tmpdir(), 'palette-home-'));
  const cfg = fs.mkdtempSync(path.join(os.tmpdir(), 'palette-cfg-'));
  tmpDirs.push(home, cfg);
  const port = await freePort();
  const relay = spawn(RELAY, ['--no-open', String(port)], { stdio: 'ignore',
    env: Object.assign({}, process.env, { HUSH_HOME: home, HUSH_CONFIG_DIR: cfg, HUSH_PASS_HELPER: '/nonexistent-hush-pass-helper' }) });
  liveProcs.push(relay);
  relay.once('error', (e) => die('relay did not start: ' + e.message));
  const base = `http://127.0.0.1:${port}`;
  let up = false;
  for (let i = 0; i < 200 && !up; i++) {
    try { up = (await fetch(base + '/api/session', { headers: { 'X-Hush-Token': (() => { try { return fs.readFileSync(path.join(home, 'session.token'), 'utf8').trim(); } catch (e) { return ''; } })() } })).ok; } catch (e) { /* not up */ }
    if (!up) await sleep(100);
  }
  if (!up) die('relay did not come up on ' + port);
  const targets = await (await fetch(`http://127.0.0.1:${cport}/json/list`)).json();
  const page = targets.find((t) => t.type === 'page');
  if (!page) die('no page target');
  const cdp = new Cdp(page.webSocketDebuggerUrl);
  await cdp.connect();
  await cdp.send('Page.enable', {});
  await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: "try { localStorage.setItem('hush-theme', 'field-office'); } catch (e) {}" });

  const WIDTHS = [[1440, 900], [390, 844]];
  const report = [];
  const view = async (w, h) => {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
    await sleep(250);
  };
  /* Signup can flip the theme (#241 A03); the palette under test is field-office. */
  const pinTheme = () => cdp.eval(`(() => { const h = document.documentElement; const was = h.getAttribute('data-theme');
    h.setAttribute('data-theme', 'field-office'); return was; })()`);
  const measure = async (surface, prep) => {
    for (const [w, h] of WIDTHS) {
      await view(w, h);
      if (prep) await prep(w);
      await pinTheme();
      await cdp.eval(`(() => { const a = document.activeElement; if (a && a !== document.body) a.blur(); })()`);
      await sleep(150);
      await cdp.eval(KIT);
      const m = await cdp.eval(MEASURE);
      report.push(Object.assign({ surface, w }, m));
      for (const t of m.text) if (t.ratio < 4.5) failures.push(`C1 ${surface}@${w} text ${t.ratio}:1 ${t.fg} on ${t.bg} ${t.el} "${t.t}"`);
      for (const c of m.ctl) if (c.ratio < 3) failures.push(`C2 ${surface}@${w} control ${c.ratio}:1 border ${c.border} fill ${c.fill} behind ${c.behind} ${c.el}`);
      for (const x of m.white) failures.push(`C4 ${surface}@${w} near-white ${x.kind} ${x.c} ${x.el}`);
      if (m.n.text < 3) failures.push(`${surface}@${w} measured only ${m.n.text} text runs (surface not on screen?)`);
      console.log(`palette ${surface}@${w}: ${m.n.text} text (min ${Math.min(...m.text.map((t) => t.ratio)).toFixed(2)}), ` +
        `${m.n.ctl} controls (min ${m.ctl.length ? Math.min(...m.ctl.map((c) => c.ratio)).toFixed(2) : '-'}), ${m.white.length} near-white, ${m.n.exempt} disabled exempt, ${m.n.hidden} covered`);
      await accentTouch(surface, w);
    }
  };

  /* R2 (#309): the same view in christmas. Measure, swap --accent and
   * --accent-ink for sentinels, measure again; a text run or control whose
   * paint changed is painted from those tokens. Transitions are off while
   * measuring so a token swap is read settled. */
  const R2_SWAP = `(() => { const h = document.documentElement; h.setAttribute('data-theme', 'christmas');
    let st = document.getElementById('pal-r2-still'); if (!st) { st = document.createElement('style'); st.id = 'pal-r2-still';
      st.textContent = '*, *::before, *::after { transition: none !important; animation: none !important; }'; document.head.appendChild(st); }
    return true; })()`;
  const r3 = []; const r5 = []; const r6 = []; const r7 = [];
  /* R5 floor (#309 r4): each christmas border rule the PR claims must have at
   * least one measured instance in its state. [name, element selector, state] */
  const R5_CLAIMS = [['.thread-btn', '.thread-btn', 'rest:'], ['.chan-voice', '.chan-voice', 'rest:'], ['#badge.ok', '#badge.ok', 'rest:'],
    ['.composer-box:focus-within', '.composer-box', 'focus:'], ['.card input:focus', '.card input', 'focus:'], ['.card textarea:focus', '.card textarea', 'focus:'],
    ['.prov-row:hover', '.prov-row', 'hover:'], ['.iconbtn.on', '.iconbtn.on', 'rest:'], ['.partial', '.partial', 'rest:'], ['.fo-feed-line', '.fo-feed-line', 'rest:'],
    ['.composer-box:focus-within ring', '.composer-box', 'focus:', 'Ring'], ['.avatar-preview ring', '.avatar-preview', 'rest:', 'Ring'],
    ['.avatar-cell.on ring', '.avatar-cell.on', 'rest:', 'Ring']];
  const accentTouch = async (surface, w) => {
    await cdp.eval(R2_SWAP);
    await sleep(60);
    const a = await cdp.eval(MEASURE);
    /* R3 (#309 r2): christmas accent-text buttons on real mouse hover. The
     * mouse is moved to each visible one, so :hover is the browser's own; its
     * text must hold 4.5:1 on the hover fill. R4: the stamp (.thread-btn)
     * outline must hold 3:1 against what is behind it. */
    const hov = await cdp.eval(`(() => { const P = window.__pal; return [...document.querySelectorAll('.thread-btn, .chan-voice, .robot-call')].map((el, i) => {
      const r = el.getBoundingClientRect(); if (r.width < 4 || r.height < 4 || r.bottom <= 0 || r.top >= innerHeight || r.right <= 0 || r.left >= innerWidth) return null;
      if (!P.onTop(el, r) || P.opacityOf(el) < 0.05) return null; return { i, x: r.left + r.width / 2, y: r.top + r.height / 2, el: P.path(el) }; }).filter(Boolean); })()`);
    for (const t of hov) {
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: t.x, y: t.y });
      await sleep(40);
      const m = await cdp.eval(`(() => { const P = window.__pal; const el = [...document.querySelectorAll('.thread-btn, .chan-voice, .robot-call')][${t.i}];
        if (!el || !el.matches(':hover')) return null; const cs = getComputedStyle(el); const bg = P.bgOf(el); const fg = P.over(P.rgba(cs.color), bg);
        const behind = P.bgOf(el.parentElement); const bc = P.over(P.rgba(cs.borderTopColor), behind);
        return { hover: Math.round(P.ratio(fg, bg) * 100) / 100, fg: P.hex(fg), bg: P.hex(bg), stamp: el.classList.contains('thread-btn'), outline: Math.round(P.ratio(bc, behind) * 100) / 100, border: P.hex(bc), behind: P.hex(behind) }; })()`);
      if (!m) { failures.push(`R3 christmas ${surface}@${w} ${t.el} did not take :hover under the mouse`); continue; }
      r3.push(Object.assign({ surface, w, el: t.el }, m));
      if (m.hover < 4.5) failures.push(`R3 christmas ${surface}@${w} hover text ${m.hover}:1 ${m.fg} on ${m.bg} ${t.el}`);
      if (m.stamp && m.outline < 3) failures.push(`R4 christmas ${surface}@${w} stamp outline ${m.outline}:1 ${m.border} behind ${m.behind} ${t.el}`);
    }
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 0, y: 0 });
    if (hov.length) console.log(`palette R3/R4 christmas ${surface}@${w}: ${hov.length} hovered (min hover ${Math.min(...r3.filter((x) => x.surface === surface && x.w === w).map((x) => x.hover)).toFixed(2)}, min stamp outline ${r3.some((x) => x.surface === surface && x.w === w && x.stamp) ? Math.min(...r3.filter((x) => x.surface === surface && x.w === w && x.stamp).map((x) => x.outline)).toFixed(2) : '-'})`);

    /* R5 (#309 r3): christmas borders painted from --accent-dim or --accent.
     * Every visible border side whose colour changes when those are swapped
     * for sentinels must hold 3:1 against what is behind the element; read
     * at rest, with each visible field focused (its own and its
     * .composer-box border), and with the mouse on each .prov-row. */
    const BORD = `((CL) => { const P = window.__pal; const out = {}; const sides = ['Top', 'Right', 'Bottom', 'Left'];
      const claims = (el) => CL.filter((c) => el.matches(c[1])).map((c) => c[0]);
      /* Same-path siblings (e.g. every .prov-row) are told apart by their order. */
      const nth = new Map(); const kid = (el, tag) => { const k = tag + P.path(el); const i = nth.get(k) || 0; nth.set(k, i + 1); return k + '#' + i; };
      const scan = (el, tag) => { const id = kid(el, tag); const r = el.getBoundingClientRect(); if (r.width < 2 || r.height < 2 || r.bottom <= 0 || r.top >= innerHeight || r.right <= 0 || r.left >= innerWidth) return;
        const cs = getComputedStyle(el); if (cs.visibility === 'hidden' || P.opacityOf(el) < 0.05) return;
        for (const sd of sides) { const bw = parseFloat(cs['border' + sd + 'Width']) || 0; if (bw < 1 || cs['border' + sd + 'Style'] === 'none') continue;
          const c = P.rgba(cs['border' + sd + 'Color']); if (c[3] <= 0) continue; out[id + '|' + sd] = { c: cs['border' + sd + 'Color'], el: P.path(el), id, sd, state: tag,
            behind: P.hex(P.bgOf(el.parentElement || el)), col: P.hex(P.over(c, P.bgOf(el.parentElement || el))), ratio: Math.round(P.ratio(P.over(c, P.bgOf(el.parentElement || el)), P.bgOf(el.parentElement || el)) * 100) / 100, m: claims(el) }; }
        /* A focus ring drawn as an outer box-shadow (spread >= 1px, no blur) is measured like a border. */
        for (const mm of (cs.boxShadow || '').matchAll(/(rgba?\\([^)]*\\))\\s+0px\\s+0px\\s+0px\\s+([\\d.]+)px(?!\\s*inset)/g)) {
          if (+mm[2] < 1) continue; const c = P.rgba(mm[1]); if (c[3] <= 0) continue; const bh = P.bgOf(el.parentElement || el);
          out[id + '|Ring'] = { c: mm[1], el: P.path(el), id, sd: 'Ring', state: tag, behind: P.hex(bh), col: P.hex(P.over(c, bh)), ratio: Math.round(P.ratio(P.over(c, bh), bh) * 100) / 100, m: claims(el) }; } };
      for (const el of document.querySelectorAll('body *')) scan(el, 'rest:');
      for (const f of document.querySelectorAll('input:not([type=radio]):not([type=checkbox]):not([type=hidden]), textarea')) {
        const r = f.getBoundingClientRect(); if (r.width < 4 || r.height < 4 || r.bottom <= 0 || r.top >= innerHeight || f.disabled || !P.onTop(f, r)) continue;
        f.focus({ preventScroll: true }); if (document.activeElement !== f) continue; scan(f, 'focus:'); const box = f.closest('.composer-box'); if (box) scan(box, 'focus:'); f.blur(); }
      return out; })(${JSON.stringify(R5_CLAIMS)})`;
    const dimSides = async () => {
      const a0 = await cdp.eval(BORD);
      await cdp.eval(`(() => { const s = document.documentElement.style; s.setProperty('--accent-dim', '#00ffff'); s.setProperty('--accent', '#00ff00'); })()`); await sleep(40);
      const b0 = await cdp.eval(BORD);
      await cdp.eval(`(() => { const s = document.documentElement.style; s.removeProperty('--accent-dim'); s.removeProperty('--accent'); })()`); await sleep(40);
      return Object.entries(a0).filter(([k, x]) => b0[k] && b0[k].c !== x.c).map(([, x]) => x);
    };
    const dims = await dimSides();
    const rows = await cdp.eval(`(() => [...document.querySelectorAll('.prov-row')].map((el) => { const r = el.getBoundingClientRect();
      return r.width > 4 && r.top >= 0 && r.bottom <= innerHeight && window.__pal.onTop(el, r) ? { x: r.left + r.width / 2, y: r.top + r.height / 2 } : null; }).filter(Boolean))()`);
    for (const rw of rows) {
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: rw.x, y: rw.y }); await sleep(40);
      for (const x of await dimSides()) if (x.el.includes('prov-row')) dims.push(Object.assign(x, { state: 'hover:' }));
    }
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 0, y: 0 });
    const seen5 = new Set();
    for (const x of dims) { const k = x.state + x.id + x.sd; if (seen5.has(k)) continue; seen5.add(k); r5.push(Object.assign({ surface, w }, x));
      if (x.ratio < 3) failures.push(`R5 christmas ${surface}@${w} ${x.state} accent border-${x.sd.toLowerCase()} ${x.ratio}:1 ${x.col} behind ${x.behind} ${x.el}`); }
    /* R6 (#309 r3): christmas switch track >= 3:1 against its panel, off and
     * on. The checked state is flipped on the input with no event (nothing is
     * saved), read, and put back. */
    const sw = await cdp.eval(`(() => { const P = window.__pal; const out = [];
      for (const lab of document.querySelectorAll('.switch')) { const inp = lab.querySelector('input'); const sl = lab.querySelector('.slider'); if (!inp || !sl) continue;
        const r = sl.getBoundingClientRect(); if (r.width < 4 || r.height < 4 || r.bottom <= 0 || r.top >= innerHeight || P.opacityOf(sl) < 0.05) continue;
        const was = inp.checked;
        for (const st of [was, !was]) { inp.checked = st; const behind = P.bgOf(lab.parentElement); const t = P.over(P.rgba(getComputedStyle(sl).backgroundColor), behind);
          const k = P.over(P.rgba(getComputedStyle(sl, '::before').backgroundColor), t);
          out.push({ id: inp.id, on: st, track: P.hex(t), behind: P.hex(behind), ratio: Math.round(P.ratio(t, behind) * 100) / 100, knob: P.hex(k), kratio: Math.round(P.ratio(k, t) * 100) / 100 }); }
        inp.checked = was; }
      return out; })()`);
    for (const x of sw) { r6.push(Object.assign({ surface, w }, x)); if (x.ratio < 3) failures.push(`R6 christmas ${surface}@${w} switch #${x.id} ${x.on ? 'on' : 'off'} track ${x.ratio}:1 ${x.track} behind ${x.behind}`);
      if (x.kratio < 3) failures.push(`R6k christmas ${surface}@${w} switch #${x.id} ${x.on ? 'on' : 'off'} knob ${x.kratio}:1 ${x.knob} on track ${x.track}`); }
    /* R7 (#309 r4): christmas primary buttons are filled with --accent-fill. */
    const pf = await cdp.eval(`(() => { const P = window.__pal; const probe = document.createElement('i'); probe.style.color = 'var(--accent-fill)'; document.body.appendChild(probe);
      const want = getComputedStyle(probe).color; probe.remove(); const out = [];
      for (const el of document.querySelectorAll('#install, #thread-send, .btn:not(.ghost):not(.danger)')) { const r = el.getBoundingClientRect();
        if (r.width < 4 || r.height < 4 || r.bottom <= 0 || r.top >= innerHeight || r.right <= 0 || r.left >= innerWidth || P.opacityOf(el) < 0.05 || !P.onTop(el, r)) continue;
        out.push({ el: P.path(el), bg: getComputedStyle(el).backgroundColor, want }); }
      return out; })()`);
    for (const x of pf) { r7.push(Object.assign({ surface, w }, x)); if (x.bg !== x.want) failures.push(`R7 christmas ${surface}@${w} primary ${x.el} background ${x.bg}, want --accent-fill ${x.want}`); }
    if (dims.length || sw.length || rows.length) console.log(`palette R5/R6 christmas ${surface}@${w}: ${seen5.size} accent / accent-dim border sides incl. ${rows.length} hovered .prov-row (min ${dims.length ? Math.min(...dims.map((x) => x.ratio)).toFixed(2) : '-'}), ${sw.length} switch states (min ${sw.length ? Math.min(...sw.map((x) => x.ratio)).toFixed(2) : '-'}, knob min ${sw.length ? Math.min(...sw.map((x) => x.kratio)).toFixed(2) : '-'})`);
    await cdp.eval(`(() => { const s = document.documentElement.style; s.setProperty('--accent', '#00ff00'); s.setProperty('--accent-ink', '#ff00ff'); s.setProperty('--accent-fill', '#0000ff'); })()`);
    await sleep(60);
    const b = await cdp.eval(MEASURE);
    await cdp.eval(`(() => { const h = document.documentElement; h.style.removeProperty('--accent'); h.style.removeProperty('--accent-ink'); h.style.removeProperty('--accent-fill');
      const st = document.getElementById('pal-r2-still'); if (st) st.remove(); h.setAttribute('data-theme', 'field-office'); })()`);
    /* Same DOM walk both times; repeated paths (e.g. several .slider) are told apart by their order. */
    const keyed = (list) => { const n = new Map(); return list.map((x) => { const k = x.el + '|' + (x.t || ''); const i = n.get(k) || 0; n.set(k, i + 1); return [k + '#' + i, x]; }); };
    const bt = new Map(keyed(b.text)); const bc = new Map(keyed(b.ctl));
    const text = keyed(a.text).filter(([k, x]) => { const y = bt.get(k); return !y || y.fg !== x.fg || y.bg !== x.bg; }).map(([, x]) => x);
    const ctl = keyed(a.ctl).filter(([k, x]) => { const y = bc.get(k); return !y || y.border !== x.border || y.fill !== x.fill || y.bar !== x.bar || y.behind !== x.behind; }).map(([, x]) => x);
    report.push({ surface, w, r2: { text, ctl } });
    for (const t of text) if (t.ratio < 4.5) failures.push(`R2 christmas ${surface}@${w} accent text ${t.ratio}:1 ${t.fg} on ${t.bg} ${t.el} "${t.t}"`);
    for (const c of ctl) if (c.ratio < 3) failures.push(`R2 christmas ${surface}@${w} accent control ${c.ratio}:1 border ${c.border} fill ${c.fill} behind ${c.behind} ${c.el}`);
    console.log(`palette R2 christmas ${surface}@${w}: ${text.length} accent text (min ${text.length ? Math.min(...text.map((t) => t.ratio)).toFixed(2) : '-'}), ` +
      `${ctl.length} accent controls (min ${ctl.length ? Math.min(...ctl.map((c) => c.ratio)).toFixed(2) : '-'})`);
  };

  /* C3: keyboard ring >= 2px and >= 3:1 against what is behind it. A real Tab
   * key press first, so later programmatic focus matches :focus-visible the
   * way keyboard focus does. Switch inputs draw the ring on their .slider. */
  const ringCheck = async (where, sels) => {
    for (const [w, h] of WIDTHS) {
      await view(w, h);
      await pinTheme();
      await cdp.eval(KIT);
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
      const rings = await cdp.eval(`(() => { const P = window.__pal; const out = [];
        for (const sel of ${JSON.stringify(sels)}) {
          const b = document.querySelector(sel); if (!b) { out.push({ sel, missing: true }); continue; }
          b.focus(); b.scrollIntoView({ block: 'center' });
          const ringEl = b.matches('.switch input') ? b.nextElementSibling : b; const cs = getComputedStyle(ringEl);
          const host = b.matches('.switch input') ? b.closest('.switch') : ringEl;
          const behind = P.bgOf(host.parentElement); const c = P.rgba(cs.outlineColor);
          out.push({ sel, fv: b.matches(':focus-visible'), style: cs.outlineStyle, width: parseFloat(cs.outlineWidth) || 0,
            ratio: Math.round(P.ratio(P.over(c, behind), behind) * 100) / 100, color: P.hex(c), behind: P.hex(behind) });
          b.blur(); }
        return out; })()`);
      for (const r of rings) {
        console.log(`palette C3 ${where}@${w}: ${JSON.stringify(r)}`);
        report.push({ surface: 'ring:' + where, w, ring: r });
        if (r.missing) { failures.push(`C3 ${where}@${w} ${r.sel} not found`); continue; }
        if (!r.fv || r.style === 'none' || r.width < 2 || r.ratio < 3)
          failures.push(`C3 ${where}@${w} focus ring on ${r.sel}: ${JSON.stringify(r)}`);
      }
    }
  };

  await view(1440, 900);
  await cdp.send('Page.navigate', { url: base + '/' });
  await cdp.waitFor(`!!document.querySelector('#gate.show #begin')`, 'splash');
  await measure('gate-splash');
  await cdp.click('#begin');
  await cdp.waitFor(`!!document.querySelector('#create-id')`, 'landing');
  await measure('gate-landing');
  await cdp.click('#create-id');
  await cdp.waitFor(`!!document.querySelector('#ack-key')`, 'backup');
  await measure('gate-backup');
  await cdp.click('#ack-key');
  await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe');
  await measure('gate-vibe');
  await cdp.eval(`document.querySelector('#vibe-name').value = 'FIELD HQ'`);
  await cdp.click('#do-vibe');
  await cdp.waitFor(`!!document.querySelector('#meet-payne')`, 'Meet Major');
  await measure('gate-meet-major');
  await cdp.click('#meet-payne');
  await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive');
  await cdp.send('Page.reload', {});
  await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive after reload');

  // Main thread in #welcome: Major's welcome note, two notes of mine (one with a
  // code fence, so Download / Canvas render), replies so Thread shows.
  await cdp.eval(`[...document.querySelectorAll('.chan')].find((b) => b.textContent.includes('#welcome')).click()`);
  await cdp.waitFor(`document.querySelectorAll('#stream .note').length >= 1`, 'welcome note');
  const post = (body) => cdp.eval(`(async () => (await fetch('/api/event', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(${JSON.stringify(body)}) })).status)()`, true);
  if (await post({ content: 'Morning, Major. Status on the supply manifest?', kind: 1, channel: 'welcome' }) !== 200) die('post note 1');
  if (await post({ content: 'Manifest attached.\n```txt\nrations 40\nfuel 12\n```', kind: 1, channel: 'welcome' }) !== 200) die('post note 2');
  const evs = JSON.parse(await cdp.eval(`(async () => await (await fetch('/api/events?channel=welcome')).text())()`, true));
  const roots = (Array.isArray(evs) ? evs : (evs.events || [])).filter((e) => !e.reply_to && e.kind === 1 && e.channel === 'welcome');
  if (roots.length < 3) die('expected 3 root notes in #welcome, got ' + roots.length);
  for (const [id, text] of [[roots[0].id, 'Copy that. Reading in now.'], [roots[roots.length - 1].id, 'Received. Stamped and filed.']])
  {
    const st = await post({ content: text, kind: 1, channel: 'welcome', reply_to: id });
    if (st !== 200) die('post reply status ' + st + ' to ' + id + ' roots ' + JSON.stringify(roots.map((e) => [e.id, e.content.slice(0, 20)])));
  }
  await cdp.waitFor(`document.querySelectorAll('#stream .thread-btn').length >= 2 && document.querySelectorAll('#stream .note-files button').length >= 2`, 'Thread + Download', 15000);
  /* Stop renders only while a robot runs, so a styled .think chip is added to
   * a note to measure the CSS (same markup as paintThink). */
  const STOP_FIXTURE = `(() => { const n = document.querySelector('#stream .note'); if (!n || n.querySelector('.think')) return;
    const chip = document.createElement('div'); chip.className = 'think'; const d = document.createElement('span'); d.className = 'think-dot';
    chip.appendChild(d); chip.appendChild(document.createTextNode('Major is thinking'));
    const s = document.createElement('button'); s.type = 'button'; s.className = 'think-stop'; s.textContent = 'Stop'; chip.appendChild(s); n.appendChild(chip); })()`;
  await measure('main-thread', async () => { await cdp.eval(STOP_FIXTURE); await cdp.eval(`(() => { const s = document.querySelector('#stream'); s.scrollTop = 0; })()`); });

  // B1 button size + touch hit, C3 focus rings.
  for (const [w, h] of WIDTHS) {
    await view(w, h);
    await pinTheme();
    await cdp.eval(STOP_FIXTURE);
    await cdp.eval(KIT);
    const sizes = await cdp.eval(`(() => { const out = [];
      for (const sel of ['#stream .thread-btn', '#stream .note-files button', '#stream .think-stop']) {
        const b = document.querySelector(sel); if (!b) { out.push({ sel, missing: true }); continue; }
        b.scrollIntoView({ block: 'center' }); const r = b.getBoundingClientRect(); const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
        const els = [-20, 20].map((dy) => document.elementFromPoint(cx, cy + dy)); const hits = els.map((e) => !!e && (e === b || b.contains(e)));
        out.push({ sel, h: b.offsetHeight, w: b.offsetWidth, hitUp: hits[0], hitDown: hits[1], miss: els.map((e, i) => hits[i] || !e ? null : window.__pal.path(e)).filter(Boolean) }); }
      return out; })()`);
    for (const s of sizes) {
      console.log(`palette B1@${w}: ${JSON.stringify(s)}`);
      if (s.missing) { failures.push(`B1@${w} ${s.sel} not rendered`); continue; }
      if (s.h < 20 || s.h > 24) failures.push(`B1@${w} ${s.sel} is ${s.h}px tall, want 20-24px (ink stamp, not the 32px plate)`);
      if (w < 641 && !(s.hitUp && s.hitDown)) failures.push(`B1@${w} ${s.sel} touch target under 44px: ${JSON.stringify(s)}`);
    }
  }

  await ringCheck('main-thread', ['#stream .thread-btn', '#stream .note-files button', '#stream .think-stop', '#nav-toggle', '#rail-toggle']);

  await measure('thread-pane', async () => {
    await cdp.eval(`(() => { const p = document.querySelector('#thread-pane'); if (!p.classList.contains('show')) document.querySelector('#stream .thread-btn').click(); })()`);
    await cdp.waitFor(`document.querySelector('#thread-pane').classList.contains('show') && document.querySelectorAll('#thread-stream .note').length >= 2`, 'thread pane');
  });
  await ringCheck('thread-pane', ['#thread-msg', '#thread-close']);
  await cdp.eval(`(() => { const x = document.querySelector('#thread-close'); if (x) x.click(); })()`);
  await sleep(300);
  await measure('settings', async () => {
    await cdp.eval(`document.querySelector('#settings').classList.remove('show')`);
    await cdp.eval(`document.querySelector('#settings-btn').click()`);
    await cdp.waitFor(`document.querySelector('#settings').classList.contains('show')`, 'settings');
  });
  await ringCheck('settings', ['#turn-on', '#vibe-public', '#settings input[name="theme"]:checked', '#settings-close']);
  /* R1: theme radios take the theme's own --accent (no native blue), in every
   * theme. Both sides go through computed colour so oklch / hex compare equal.
   * The checked fill is that accent; it must hold 3:1 against the background
   * behind every Settings radio, in every theme, at both widths (#309). */
  {
    const themes = ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office'];
    for (const [w, h] of WIDTHS) {
      await view(w, h);
      await cdp.eval(KIT);
      const r1 = await cdp.eval(`(() => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
        const probe = document.createElement('i'); document.body.appendChild(probe);
        const radios = [...document.querySelectorAll('#settings input[type="radio"]')].filter((r) => r.getBoundingClientRect().width > 0);
        for (const t of ${JSON.stringify(themes)}) {
          h.setAttribute('data-theme', t);
          const radio = document.querySelector('#settings input[name="theme"]:checked') || document.querySelector('#settings input[name="theme"]');
          probe.style.color = 'var(--accent)'; const want = getComputedStyle(probe).color;
          const raw = getComputedStyle(radio).accentColor; probe.style.color = raw === 'auto' ? '' : raw;
          const got = raw === 'auto' ? 'auto' : getComputedStyle(probe).color;
          const fills = radios.map((r) => { const a = getComputedStyle(r).accentColor; probe.style.color = a === 'auto' ? '' : a;
            const f = a === 'auto' ? null : P.rgba(getComputedStyle(probe).color); const behind = P.bgOf(r.parentElement);
            return { radio: (r.name || '') + '=' + (r.value || r.id), behind: P.hex(behind), ratio: f ? Math.round(P.ratio(f, behind) * 100) / 100 : 0 }; });
          const min = fills.reduce((m, f) => (m && m.ratio <= f.ratio ? m : f), null);
          out.push({ theme: t, accentColor: raw, got, want, match: got === want, radios: fills.length,
            behinds: [...new Set(fills.map((f) => f.behind))], ratio: min ? min.ratio : 0, minRadio: min ? min.radio : null });
        }
        probe.remove(); h.setAttribute('data-theme', was); return out; })()`);
      for (const r of r1) {
        console.log(`palette R1 ${r.theme}@${w}: ${JSON.stringify(r)}`);
        report.push({ surface: 'R1:' + r.theme, w, r1: r });
        if (w === 1440 && !r.match) failures.push(`R1 theme radio accent-color in ${r.theme} is ${r.accentColor}, want the theme's --accent ${r.want}`);
        if (r.radios < 2) failures.push(`R1 ${r.theme}@${w} found ${r.radios} Settings radios, want the theme and approval radios`);
        else if (r.ratio < 3) failures.push(`R1 ${r.theme}@${w} checked radio fill ${r.ratio}:1 behind ${r.minRadio} (${r.behinds.join(', ')}), want >= 3`);
      }
      if (r1.length !== 8) failures.push('R1 measured ' + r1.length + ' themes at ' + w + ', want 8');
    }
  }
  await cdp.eval(`document.querySelector('#settings-close').click()`);
  await measure('kit-menu', async () => {
    await cdp.eval(`(() => { const k = document.querySelector('#rail-toggle'); if (k.getAttribute('aria-expanded') !== 'true') k.click(); })()`);
    await cdp.waitFor(`document.querySelector('#rail-toggle').getAttribute('aria-expanded') === 'true'`, 'kit menu');
  });
  await cdp.eval(`(() => { const k = document.querySelector('#rail-toggle'); if (k.getAttribute('aria-expanded') === 'true') k.click(); })()`);
  await measure('profile', async () => {
    await cdp.eval(`(() => { const d = document.querySelector('#profile'); if (!d || !d.classList.contains('show')) document.querySelector('#profile-btn').click(); })()`);
    await cdp.waitFor(`!!document.querySelector('#profile.show, .drawer.show')`, 'profile drawer');
  });
  await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); })()`);
  await measure('new-channel', async () => {
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); document.querySelector('#new-chan-drawer').classList.add('show'); })()`);
    await cdp.waitFor(`document.querySelector('#new-chan-drawer').classList.contains('show')`, 'new channel');
  });
  await ringCheck('new-channel', ['#new-chan', '#new-chan-prompt', '#new-chan-save', '#new-chan-close']);
  /* Every other post-signup dialog, shown on its own over the hive. */
  for (const id of ['manage-chan', 'agent-drawer', 'member-drawer', 'providers-hub', 'provider-drawer', 'relay-drawer',
    'hive-leave', 'dev-log-drawer', 'seed-drawer', 'avatar-drawer', 'skill-drawer', 'forge-drawer', 'inv-expand-drawer']) {
    await measure('dialog-' + id, async () => {
      await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
        const d = document.getElementById(${JSON.stringify(id)}); d.classList.add('show'); const p = d.querySelector('.panel'); if (p) p.scrollTop = 0; })()`);
      await sleep(150);
    });
  }
  await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); })()`);
  await cdp.eval(`document.getElementById('manage-chan').classList.add('show')`);
  await ringCheck('manage-channel', ['#manage-prompt', '#manage-save', '#manage-close']);
  await cdp.eval(`document.getElementById('manage-chan').classList.remove('show')`);
  await cdp.eval(`document.querySelector('#new-chan-drawer').classList.remove('show')`);
  // BOARDS drawer at phone width.
  await view(390, 844);
  await cdp.eval(`(() => { if (!document.querySelector('#hive').classList.contains('nav-open')) document.querySelector('#nav-toggle').click(); })()`);
  await cdp.waitFor(`document.querySelector('#hive').classList.contains('nav-open')`, 'boards drawer');
  await sleep(400);
  await pinTheme(); await cdp.eval(KIT);
  {
    const m = await cdp.eval(MEASURE);
    report.push(Object.assign({ surface: 'boards-drawer', w: 390 }, m));
    for (const t of m.text) if (t.ratio < 4.5) failures.push(`C1 boards-drawer@390 text ${t.ratio}:1 ${t.fg} on ${t.bg} ${t.el} "${t.t}"`);
    for (const c of m.ctl) if (c.ratio < 3) failures.push(`C2 boards-drawer@390 control ${c.ratio}:1 ${c.el}`);
    for (const x of m.white) failures.push(`C4 boards-drawer@390 near-white ${x.kind} ${x.c} ${x.el}`);
    console.log(`palette boards-drawer@390: ${m.n.text} text (min ${Math.min(...m.text.map((t) => t.ratio)).toFixed(2)}), ${m.n.ctl} controls`);
  }
  await accentTouch('boards-drawer', 390);

  /* R5 extra views (#309 r4), christmas only, after the field-office passes:
   * the Providers hub opened through its real button (openProvidersHub paints
   * one .prov-row per provider), and the main thread with a live .partial
   * preview added beside the Stop fixture (same markup paintThink makes). */
  for (const [w, h] of WIDTHS) {
    await view(w, h);
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); document.querySelector('#hive').classList.remove('nav-open'); document.querySelector('#providers-btn').click(); })()`);
    await cdp.waitFor(`document.querySelector('#providers-hub').classList.contains('show')`, 'providers hub', 15000);
    await sleep(600); /* rows paint after loadProviders; the R5 floor fails if none appear */ await cdp.eval(KIT);
    await accentTouch('providers-hub-rows', w);
    await cdp.eval(`document.querySelector('#providers-hub').classList.remove('show')`);
    await cdp.eval(`(() => { const n = document.querySelector('#stream .note'); if (!n) return; let p = n.querySelector('.partial'); if (!p) { p = document.createElement('div'); p.className = 'partial'; n.appendChild(p); }
      p.textContent = 'Drafting the supply plan'; document.querySelector('#stream').scrollTop = 0; })()`);
    await sleep(150); await cdp.eval(KIT);
    await accentTouch('main-thread-partial', w);
  }
  /* Avatar window (#309 r5), after the field-office passes: opened through
   * the real Raise robot and enlarge buttons, then a tile is picked with a
   * real mouse click, so .avatar-cell.on and the filled preview are painted.
   * The agent drawer underneath is closed so only the window is on screen.
   * R5 (christmas, accentTouch) measures the selected tile's ring. R8: the
   * preview's outer ring (first box-shadow) in all 8 themes. R9: the same
   * for the selected tile (.avatar-cell.on) against the grid behind it. */
  const r8 = [];
  const r9 = [];
  /* [pin, name, selector, results]: the element's first box-shadow is its ring. */
  const RING_PINS = [['R8', 'avatar-preview', '#avatar-preview', r8], ['R9', 'avatar-cell.on', '#avatar-grid .avatar-cell.on', r9]];
  const R8_THEMES = ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office'];
  for (const [w, h] of WIDTHS) {
    await view(w, h);
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); document.querySelector('#hive').classList.remove('nav-open'); document.querySelector('#raise-agent').click(); })()`);
    await cdp.waitFor(`document.querySelector('#agent-drawer').classList.contains('show')`, 'agent drawer');
    await cdp.eval(`document.querySelector('#agent-pic-enlarge').click()`);
    await cdp.waitFor(`document.querySelector('#avatar-drawer').classList.contains('show') && document.querySelectorAll('#avatar-grid .avatar-cell').length > 0`, 'avatar window');
    await cdp.eval(`(() => { document.querySelector('#agent-drawer').classList.remove('show'); document.querySelector('#avatar-grid').scrollTop = 0;
      const p = document.querySelector('#avatar-drawer .panel'); if (p) p.scrollTop = 0; })()`);
    await pinTheme(); await sleep(200);
    const at = await cdp.eval(`(() => { const r = document.querySelector('#avatar-grid .avatar-cell').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: at.x, y: at.y, button: 'left', clickCount: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: at.x, y: at.y, button: 'left', clickCount: 1 });
    await cdp.waitFor(`!!document.querySelector('#avatar-grid .avatar-cell.on') && !document.querySelector('#avatar-preview').classList.contains('empty')`, 'avatar tile picked');
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 0, y: 0 });
    await cdp.eval(`(() => { const a = document.activeElement; if (a && a !== document.body) a.blur(); })()`);
    await sleep(150); await cdp.eval(KIT);
    await accentTouch('avatar-window', w);
    for (const [pin, name, sel, acc] of RING_PINS) {
      const rr = await cdp.eval(`((T, S) => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme');
        const st = document.createElement('style'); st.textContent = '*, *::before, *::after { transition: none !important; animation: none !important; }'; document.head.appendChild(st);
        const el = document.querySelector(S); const out = [];
        for (const t of T) { h.setAttribute('data-theme', t); if (!el) { out.push({ theme: t, shadow: 'no ' + S, ratio: 0 }); continue; }
          const cs = getComputedStyle(el); const behind = P.bgOf(el.parentElement);
          const mm = /((?:rgba?|oklch|oklab|lab|lch|hsla?|color)\\([^)]*\\))\\s+(-?[\\d.]+)px\\s+(-?[\\d.]+)px\\s+([\\d.]+)px(?:\\s+(-?[\\d.]+)px)?(?!\\s*inset)/.exec(cs.boxShadow || '');
          if (!mm) { out.push({ theme: t, shadow: cs.boxShadow, ratio: 0 }); continue; }
          const c = P.over(P.rgba(mm[1]), behind);
          out.push({ theme: t, kind: (+mm[2] || +mm[3]) ? 'offset' : 'ring', blur: +mm[4], spread: +(mm[5] || 0), ring: P.hex(c), behind: P.hex(behind), ratio: Math.round(P.ratio(c, behind) * 100) / 100, shadow: cs.boxShadow }); }
        st.remove(); h.setAttribute('data-theme', was); return out; })(${JSON.stringify(R8_THEMES)}, ${JSON.stringify(sel)})`);
      for (const x of rr) { acc.push(Object.assign({ w }, x));
        if (!x.ring || x.blur > 0 || x.ratio < 3) failures.push(`${pin} ${name} ${x.theme}@${w} ring ${x.ratio}:1 ${x.ring || '-'} behind ${x.behind || '-'} (${x.shadow})`); }
      console.log(`palette ${pin} ${name} ring @${w}: ` + rr.map((x) => `${x.theme} ${x.ratio} (${x.kind || 'none'} ${x.ring || '-'} on ${x.behind || '-'})`).join(', '));
    }
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); })()`);
  }
  if (r8.length !== 16) failures.push(`R8 measured ${r8.length} avatar-preview rings, want 8 themes x 2 widths`);
  if (r9.length !== 16) failures.push(`R9 measured ${r9.length} selected avatar tile rings, want 8 themes x 2 widths`);
  /* R3 voice phase (#309 r3): .chan-voice is drawn only when the relay
   * reports a speech engine. The real BOARDS drawer is re-rendered with
   * /api/status answering "whisper":true (rewritten in the browser via CDP
   * Fetch, after the field-office passes above), then measured in christmas
   * at both widths. */
  cdp.onPaused = async (p) => {
    const b = await cdp.send('Fetch.getResponseBody', { requestId: p.requestId });
    const txt = (b.base64Encoded ? Buffer.from(b.body, 'base64').toString('utf8') : b.body).replace('"whisper":false', '"whisper":true');
    await cdp.send('Fetch.fulfillRequest', { requestId: p.requestId, responseCode: p.responseStatusCode || 200, responseHeaders: p.responseHeaders, body: Buffer.from(txt).toString('base64') });
  };
  await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*/api/status*', requestStage: 'Response' }] });
  for (const [w, h] of WIDTHS) {
    await view(w, h);
    await cdp.eval(`(() => { if (!document.querySelector('#hive').classList.contains('nav-open')) document.querySelector('#nav-toggle').click(); })()`);
    await cdp.waitFor(`document.querySelector('#hive').classList.contains('nav-open') && !!document.querySelector('.chan-voice')`, 'voice boards drawer', 15000);
    await sleep(400);
    await cdp.eval(KIT);
    await accentTouch('boards-drawer-voice', w);
  }
  await cdp.send('Fetch.disable', {});
  cdp.onPaused = null;
  /* .robot-call lives only in #robot-list, which the markup keeps at
   * display:none, so no real view can hover it. Static pin instead: one CSS
   * rule gives .chan-voice:hover and .robot-call:hover the same background,
   * and a christmas .robot-call computes the same colour and border colour as
   * the .chan-voice that R3 hovered. */
  {
    const st = await cdp.eval(`(() => { const h = document.documentElement; const was = h.getAttribute('data-theme'); h.setAttribute('data-theme', 'christmas');
      let shared = false; for (const sh of document.styleSheets) { let rs; try { rs = sh.cssRules; } catch (e) { continue; }
        for (const r of rs) if (r.selectorText && /\.chan-voice:hover/.test(r.selectorText) && /\.robot-call:hover/.test(r.selectorText) && r.style.background) shared = true; }
      const rc = document.querySelector('.robot-call'); const cv = document.querySelector('.chan-voice');
      const g = (e) => e ? [getComputedStyle(e).color, getComputedStyle(e).borderTopColor] : null; const out = { shared, rc: g(rc), cv: g(cv) };
      h.setAttribute('data-theme', was); return out; })()`);
    console.log('palette R3s christmas .robot-call static: ' + JSON.stringify(st));
    if (!st.shared || !st.rc || !st.cv || st.rc[0] !== st.cv[0] || st.rc[1] !== st.cv[1]) failures.push('R3s christmas .robot-call does not share the .chan-voice hover rule / colours: ' + JSON.stringify(st));
  }
  for (const w of [1440, 390]) for (const on of [false, true]) if (!r6.some((x) => x.w === w && x.on === on)) failures.push(`R6 christmas measured no ${on ? 'on' : 'off'} switch at ${w}`);
  if (process.env.PALETTE_ART) {
    fs.mkdirSync(process.env.PALETTE_ART, { recursive: true });
    fs.writeFileSync(path.join(process.env.PALETTE_ART, 'palette-report.json'), JSON.stringify(report, null, 1));
  }
  cleanup();
  void chromeProc;
  const texts = report.reduce((n, r) => n + (r.text || []).length, 0);
  const ctls = report.reduce((n, r) => n + (r.ctl || []).length, 0);
  const r3stamps = r3.filter((x) => x.stamp);
  if (!r3.some((x) => x.el.includes('chan-voice'))) failures.push('R3 christmas hovered no .chan-voice (want >= 1)');
  const r5floor = R5_CLAIMS.map(([name, , st, sd]) => [name, r5.filter((x) => x.state === st && (x.m || []).includes(name) && (!sd || x.sd === sd)).length]);
  for (const [name, n] of r5floor) if (n < 1) failures.push(`R5 floor christmas measured no ${name} border (want >= 1)`);
  console.log('palette R5 floor christmas: ' + r5floor.map(([a, n]) => a + '=' + n).join(', '));
  for (const w of [1440, 390]) if (!r7.some((x) => x.w === w)) failures.push(`R7 christmas measured no primary button at ${w} (want >= 1)`);
  if (process.env.PALETTE_ART) { fs.mkdirSync(process.env.PALETTE_ART, { recursive: true }); fs.writeFileSync(path.join(process.env.PALETTE_ART, 'r3r5r6.json'), JSON.stringify({ r3, r5, r6, r7 }, null, 1)); }
  for (const w of [1440, 390]) if (!r3stamps.some((x) => x.w === w)) failures.push(`R3/R4 christmas hovered no .thread-btn at ${w} (want >= 1)`);
  if (failures.length) {
    const uniq = [...new Set(failures)];
    for (const f of uniq.slice(0, 60)) console.error('palette check failed: ' + f);
    if (uniq.length > 60) console.error(`palette check failed: ... ${uniq.length - 60} more`);
    process.exit(1);
  }
  const views = report.filter((r) => r.text).length;
  const rings = report.filter((r) => r.ring).length;
  const themes = new Set(report.filter((r) => r.r1).map((r) => r.r1.theme)).size;
  const r1views = report.filter((r) => r.r1).length;
  const r2text = report.reduce((n, r) => n + (r.r2 ? r.r2.text.length : 0), 0);
  const r2ctl = report.reduce((n, r) => n + (r.r2 ? r.r2.ctl.length : 0), 0);
  const r2views = report.filter((r) => r.r2).length;
  console.log(`palette check passed: ${views} surface views, ${texts} text runs >= 4.5:1, ${ctls} controls >= 3:1, ` +
    `${rings} focus rings >= 2px and >= 3:1, no near-white, theme radio accent matches in ${themes} themes, ` +
    `checked radio fill >= 3:1 in ${r1views} theme x width views, ` +
    `christmas accent-painted ${r2text} text runs >= 4.5:1 and ${r2ctl} controls >= 3:1 over ${r2views} surface views, ` +
    `${r3.length} hovered accent buttons >= 4.5:1 (${r3stamps.length} stamps, outline >= 3:1; ${r3.filter((x) => x.el.includes('chan-voice')).length} chan-voice, ${r3.filter((x) => x.el.includes('robot-call')).length} robot-call), ` +
    `${r5.length} christmas accent / accent-dim border sides and rings >= 3:1 (floor ${R5_CLAIMS.length} claimed rules), ${r6.length} christmas switch track states >= 3:1 with knob >= 3:1, ${r7.length} christmas primary buttons on --accent-fill, ` +
    `${r8.length} avatar-preview rings >= 3:1 (${new Set(r8.map((x) => x.theme)).size} themes x 2 widths), ` +
    `${r9.length} selected avatar tile rings >= 3:1 (${new Set(r9.map((x) => x.theme)).size} themes x 2 widths), ` +
    'ink-stamp actions 20-24px with 44px touch hit at 390');
}

/* A CDP call that never answers (e.g. after the browser is stopped) drains
 * the event loop; that must fail, never exit 0 without the pass line. */
let finished = false;
process.on('beforeExit', () => { if (!finished) { console.error('palette check failed: event loop drained before the check finished'); process.exit(1); } });
main().then(() => { finished = true; }, (e) => die(e && e.stack ? e.stack : String(e)));
