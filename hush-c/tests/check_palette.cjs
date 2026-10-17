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
//      own --accent in all 8 themes (main: auto, native blue); field-office
//      checked fill >= 3:1 against the panel.
//   K1-K6 (Ops back-LOOK 900a486d):
//   K1 field-office Developer Log with log lines: its text box background and
//      text colour are field-office colour tokens (main: #000 / #0f0), and the
//      lines pass C1.
//   K2 field-office robot drawer with two providers (real paintAgentProviderOrder):
//      the provider pills pass C1 (main: "primary" 3.17:1 on --accent-dim).
//   K3 the thread-pane milestone fields and the main milestone form show their
//      whole placeholder (text width <= content box) in 8 themes at 1440 and 390.
//   K4 field-office 390 header status light, both states: no gradient; fill,
//      border and glow colours are field-office tokens (no white glint); the
//      two states differ; >= 3:1 vs the header.
//   K5 robot picture cells (real paintAgentPicPicker) have a theme-token
//      background, not the native button grey, in 8 themes.
//   K6 the 1943 masthead edition, HQ header stamp and dispatch-log HQ stamp
//      show in field-office and are hidden in the other 7 themes at 1440.
//   K7 mentions (real renderPreservingMentions in a #stream note): a known
//      robot and an npub not in the roster both render as a pill (no leading
//      blank); the unknown one reads "@unknown" (UI_SPEC: an unresolved
//      @npub1... token is never painted; the short npub is only in its title),
//      no pill text contains "npub", and it is renamed when the roster gains it
//      (real setSession); pill text >= 4.5:1 in 8 themes at 1440 and 390.
//      K7t: through the real 1 s tick(), a pending pill is not re-rendered
//      while the roster is unchanged, and is renamed when /api/session gains
//      the npub.
//   K8 robot inventory items (real renderAllInventories): in field-office the
//      rim is a field-office token >= 3:1 against the grid; in the other 7
//      themes it is the old cyan / gold, unchanged. K8b: the drag-not-allowed
//      ink INV_INK_BAD on a grid cell is a field-office token >= 3:1 against
//      the grid, and the old #f66 in the other 7 themes.
//   L1 (G291-2 follow-up) a fixture per shared rule that painted a literal
//      colour in every theme (code spines, Canvas syntax, call tiles, inventory
//      text, pressed buttons, skill gems / slots / doll / lifetime chips, the
//      bad badge, the dragged inventory item): in field-office every painted colour is a field-office token,
//      text >= 4.5:1 and rims >= 3:1; in the other 7 themes the computed value
//      equals khaki head d58185ab's, at 1440 and 390.
//   W1 (no white in field-office) in every measured view, the L1 fixture, the
//      call stage and the skills board: no element with a box paints pure white
//      (#fff / rgb(255,255,255), alpha > 0) in text colour, background colour or
//      gradient, a drawn border side or outline, box- or text-shadow, SVG fill
//      or stroke.
//   K9 the real call stage (openStage + addTile, Mute and Muted) passes
//      C1/C2/C4/W1 in field-office (Ops #325: label 1.23:1, #fff Mute).
//   K10 the real skills board (paintSkillBoard) and skill sheet
//      (paintSkillSheet) pass C1/C2/C4/W1 in field-office (Ops #325: #ffd75e
//      lifetime ink, #d4a017 chip rim, #0ff portrait rim).
//   K11 the channel voice button (whisper:true via a wrapped /api/status, real
//      tick + paintChannels), BOARDS list at 1440 and 390, rest and :hover, in
//      field-office: glyph >= 4.5:1, rim or opaque fill >= 3:1, tokens only.
//   B1 Thread, Download and Stop are 20-24px tall at both widths; at 390 a
//      point 20px above and below the centre still hits the control (44px).
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// HUSH_TEST_WAIT_S (DevTools ready deadline, default 30), HUSH_CDP_TIMEOUT_MS
// (per CDP call, default 60000; a closed socket fails at once), PALETTE_ART (dir:
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
        if (r.ok)
          return { proc, port, userDir };
      } catch (e) { /* not yet */ }
      await sleep(100);
    }
    try { proc.kill('SIGTERM'); } catch (e) { /* gone */ }
    if (exited)
      die(`Chrome exited early code=${exited.code} signal=${exited.signal}`);
  }
  die('Chrome DevTools did not answer before the deadline');
}

/* L1 fixture: one element per shared rule that painted a literal colour in
 * every theme (the 30 of the G291-2 follow-up), in the real hive page. Runs in
 * the page as (${l1Fixture})(); window.__l1undo() removes it again. */
function l1Fixture() {
  const old = document.getElementById('l1-fix'); if (old) old.remove();
  const box = document.createElement('div'); box.id = 'l1-fix';
  box.style.cssText = 'position:fixed;left:0;top:0;width:600px;z-index:9999;background:var(--surface-2)';
  document.body.appendChild(box);
  const mk = (tag, cls, attrs, parent, text) => { const e = document.createElement(tag); if (cls) e.className = cls;
    for (const [k, v] of Object.entries(attrs || {})) e.setAttribute(k, v); if (text) e.textContent = text; (parent || box).appendChild(e); return e; };
  const E = {}; const S = [];
  const add = (key, el, sel, props, kind) => { E[key] = el; for (const p of props) S.push({ key, sel, prop: p, kind }); };
  const badge = document.getElementById('badge');
  if (badge) { badge.dataset.l1Was = badge.className; badge.classList.add('bad'); add('badge-bad', badge, '#badge.bad', ['border-top-color'], 'rim'); }
  for (const l of ['python', 'go', 'javascript', 'js', 'c']) add('code-' + l, mk('div', 'code-block', { 'data-lang': l }, box, 'x = 1'), `.code-block[data-lang="${l}"]`, ['border-left-color'], 'rim');
  const hi = document.getElementById('code-canvas-hi') || mk('pre', '', { id: 'code-canvas-hi' });
  for (const t of ['kw', 'str', 'num']) add('tok-' + t, mk('span', 'tok-' + t, { 'data-l1': '1' }, hi, t + '_sample'), '#code-canvas-hi .tok-' + t, ['color'], 'text');
  add('btn-danger', mk('button', 'btn danger', { type: 'button' }, box, 'Leave'), '.btn.danger', ['color'], 'text');
  const tile = mk('div', 'tile', {}, box); add('tile', tile, '.tile', ['background-color'], 'fill');
  add('tile-video', mk('video', '', {}, tile), '.tile video', ['background-color'], 'fill');
  add('tile-span', mk('span', '', {}, tile, 'Major'), '.tile span', ['background-color', 'color'], 'text');
  add('tile-mute', mk('button', 'tile-mute', { type: 'button' }, tile, 'Mute'), '.tile-mute', ['background-color', 'color'], 'text');
  /* The inventory fixtures live in their own #robot-inventory-like wrapper
     inside #l1-fix (same --surface fill behind the rim), not in the live
     #robot-inventory: the app's inventory re-render detached them before the
     read and L1@390 went flaky (Gauge #325 r6 finding 2). */
  const inv = mk('div', 'l1-inv', {}, box);
  inv.style.cssText = 'display:block;position:relative;height:40px;padding:4px;box-sizing:border-box;background:var(--surface);border:1px solid var(--line);border-radius:8px';
  add('inv-item', mk('div', 'inv-item', { 'data-l1': '1' }, inv, 'Rx'), '.inv-item', ['color'], 'text');
  for (const [k, id] of [['inv-btn', 'inv-seed'], ['qb-gear', 'qb-config'], ['send', 'thread-send']]) {
    const el = document.getElementById(id); if (el) add(k + '-active', el, '#' + id + ':active', ['box-shadow'], 'shadow'); }
  const pool = mk('div', 'skill-pool', {}, box);
  for (const sc of ['system', 'user', 'robot']) add('gem-' + sc, mk('div', 'skill-gem', { 'data-scope': sc }, pool, 'G'), `.skill-gem[data-scope="${sc}"]`, ['box-shadow', 'border-top-color'], 'shadow');
  add('slot-ok', mk('div', 'skill-slot drop-ok', {}, box), '.skill-slot.drop-ok', ['border-top-color', 'box-shadow'], 'rim');
  add('slot-open', mk('div', 'skill-slot open', {}, box), '.skill-slot.open', ['box-shadow'], 'shadow');
  add('doll-drop', mk('div', 'skill-doll-pic drop-ok', {}, box), '.skill-doll-pic.drop-ok', ['box-shadow'], 'shadow');
  add('inv-drag', mk('div', 'inv-item dragging', { 'data-l1': '1' }, inv, 'Rx'), '.inv-item.dragging', ['border-top-color', 'box-shadow'], 'rim');
  add('slot-no', mk('div', 'skill-slot drop-no', {}, box), '.skill-slot.drop-no', ['border-top-color'], 'rim');
  add('doll-pic', mk('div', 'skill-doll-pic', {}, box), '.skill-doll-pic', ['border-top-color'], 'rim');
  for (const lt of ['always', 'oncall']) { const g = mk('div', 'skill-gem', { 'data-lifetime': lt }, pool); mk('span', 'sig', {}, g, 'S');
    add('life-' + lt, mk('span', 'life', {}, g, lt), `.skill-gem[data-lifetime="${lt}"] .life`, ['color'], 'text'); }
  for (const lt of ['always', 'oncall']) add('chip-' + lt, mk('button', 'skill-life', { 'data-lifetime': lt, type: 'button' }, box, lt), `.skill-life[data-lifetime="${lt}"]`, ['border-top-color', 'color'], 'text+rim');
  window.__l1 = E; window.__l1spec = S;
  window.__l1undo = () => { box.remove(); for (const s of document.querySelectorAll('[data-l1]')) s.remove(); if (badge) badge.className = badge.dataset.l1Was; };
  return S.length;
}
/* Computed values on khaki head d58185ab in the 7 other themes (they read the
 * literal through var(--fo-*, literal) and must not move). '@390' = 390 only. */
const L1_EXPECT = {
  "badge-bad|border-top-color": {"*": "rgb(127, 29, 29)", "@390": "rgb(42, 36, 30)"},
  "code-python|border-left-color": "rgb(55, 118, 171)",
  "code-go|border-left-color": "rgb(0, 173, 216)",
  "code-javascript|border-left-color": "rgb(247, 223, 30)",
  "code-js|border-left-color": "rgb(247, 223, 30)",
  "code-c|border-left-color": "rgb(168, 185, 204)",
  "tok-kw|color": "rgb(125, 211, 252)",
  "tok-str|color": "rgb(134, 239, 172)",
  "tok-num|color": "rgb(251, 191, 36)",
  "btn-danger|color": "rgb(255, 255, 255)",
  "tile|background-color": "rgb(0, 0, 0)",
  "tile-video|background-color": "rgb(0, 0, 0)",
  "tile-span|background-color": "rgba(0, 0, 0, 0.55)",
  "tile-mute|background-color": "rgba(0, 0, 0, 0.55)",
  "tile-mute|color": "rgb(255, 255, 255)",
  "inv-item|color": "rgb(255, 255, 255)",
  "inv-btn-active|box-shadow": "rgba(0, 0, 0, 0.45) 0px 2px 5px 0px inset",
  "qb-gear-active|box-shadow": "rgba(0, 0, 0, 0.45) 0px 2px 5px 0px inset",
  "send-active|box-shadow": {"*": "rgba(0, 0, 0, 0.4) 0px 2px 5px 0px inset", "dark": "rgb(0, 0, 0) 2px 2px 0px 0px"},
  "gem-system|box-shadow": "rgba(212, 160, 23, 0.333) 0px 0px 6px 0px",
  "gem-system|border-top-color": "rgb(212, 160, 23)",
  "gem-user|box-shadow": "rgba(0, 255, 255, 0.267) 0px 0px 6px 0px",
  "gem-user|border-top-color": "rgb(0, 255, 255)",
  "gem-robot|box-shadow": "rgba(204, 102, 255, 0.267) 0px 0px 6px 0px",
  "gem-robot|border-top-color": "rgb(204, 102, 255)",
  "slot-ok|border-top-color": "rgb(0, 255, 255)",
  "slot-ok|box-shadow": "rgb(0, 255, 255) 0px 0px 8px 0px",
  "slot-open|box-shadow": "rgba(212, 160, 23, 0.4) 0px 0px 8px 0px inset",
  "doll-drop|box-shadow": "rgb(0, 255, 255) 0px 0px 8px 0px",
  "inv-drag|border-top-color": "rgb(255, 255, 255)",
  "inv-drag|box-shadow": "rgb(255, 255, 255) 0px 0px 8px 0px",
  "slot-no|border-top-color": "rgb(255, 102, 102)",
  "doll-pic|border-top-color": "rgb(0, 255, 255)",
  "life-always|color": "rgb(255, 215, 94)",
  "life-oncall|color": "rgb(143, 211, 255)",
  "chip-always|border-top-color": "rgb(212, 160, 23)",
  "chip-always|color": "rgb(255, 215, 94)",
  "chip-oncall|border-top-color": "rgb(0, 255, 255)",
  "chip-oncall|color": "rgb(143, 211, 255)",
};

/* A CDP call with no answer (Chrome crashed, socket closed) fails the run
 * instead of hanging it: HUSH_CDP_TIMEOUT_MS per call, default 60000. */
const CDP_TIMEOUT_MS = parseInt(process.env.HUSH_CDP_TIMEOUT_MS || '', 10) > 0 ? parseInt(process.env.HUSH_CDP_TIMEOUT_MS, 10) : 60000;
class Cdp {
  constructor(url) { this.url = url; this.id = 0; this.waiters = new Map(); this.closed = false; }
  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);
      this.ws.onopen = () => resolve();
      this.ws.onerror = (e) => reject(new Error('CDP connect failed: ' + e.message));
      this.ws.onclose = () => { this.closed = true;
        if (this.waiters.size) die(`CDP socket closed with ${this.waiters.size} call(s) unanswered (Chrome exited?)`); };
      this.ws.onmessage = (ev) => {
        const msg = JSON.parse(ev.data.toString());
        if (msg.id && this.waiters.has(msg.id)) {
          const w = this.waiters.get(msg.id);
          this.waiters.delete(msg.id);
          w(msg.result || {});
        } else if (msg.method === 'Page.javascriptDialogOpening') {
          this.send('Page.handleJavaScriptDialog', { accept: true }).catch(() => {});
        }
      };
    });
  }
  send(method, params) {
    if (this.closed) die(`CDP ${method} after the socket closed (Chrome exited?)`);
    const id = ++this.id;
    return new Promise((resolve) => {
      const t = setTimeout(() => die(`CDP ${method} got no answer in ${CDP_TIMEOUT_MS} ms`), CDP_TIMEOUT_MS);
      this.waiters.set(id, (r) => { clearTimeout(t); resolve(r); });
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
  /* Colour tokens of the current theme: every --* custom property that parses as a colour, as hex. */
  const tokenNames = () => { const names = new Set(); for (const sh of document.styleSheets) { let rules; try { rules = sh.cssRules; } catch (e) { continue; }
    const walk = (rs) => { for (const r of rs) { if (r.style) for (const p of r.style) if (p.startsWith('--')) names.add(p); if (r.cssRules) walk(r.cssRules); } }; walk(rules); } return [...names]; };
  const tokens = () => { const cs = getComputedStyle(document.documentElement); const out = new Map();
    for (const n of tokenNames()) { const v = cs.getPropertyValue(n).trim(); if (!v || !CSS.supports('color', v)) continue; const c = rgba(v); if (c[3] >= 1) out.set(hex(c), n); }
    return out; };
  window.__pal = { rgba, over, lum, ratio, bgOf, opacityOf, disabled, hex, path, onTop, textRect, tokens };
  return true;
})()`;

/* W1: pure white anywhere in the rendered page. Every element with a box
 * (any size, on or off screen) and visibility visible: text colour where it
 * has text or is a control, background colour and gradient stops, each border
 * side and the outline where they are drawn, box-shadow, text-shadow, and SVG
 * fill / stroke. A colour is white when r = g = b = 255 with alpha > 0.
 * Returns { n, hits }. */
const WHITE = `(() => {
  const isW = (v) => [...String(v).matchAll(/rgba?\\(([^)]*)\\)/g)].some((m) => { const p = m[1].split(/[ ,\\/]+/).filter(Boolean).map(Number);
    return p[0] === 255 && p[1] === 255 && p[2] === 255 && (p.length < 4 || p[3] > 0); }) || /(^|[^-\\w])(white|#fff(fff)?)([^-\\w]|$)/i.test(String(v));
  const hits = []; let n = 0;
  for (const el of document.querySelectorAll('body, body *')) {
    if (!el.getClientRects().length) continue; const cs = getComputedStyle(el); if (cs.visibility !== 'visible') continue; n++;
    const where = (el.id ? '#' + el.id : el.tagName.toLowerCase()) + (typeof el.className === 'string' && el.className ? '.' + el.className.trim().split(/\\s+/).join('.') : '');
    const hasText = [...el.childNodes].some((c) => c.nodeType === 3 && c.nodeValue.trim()) || /^(INPUT|TEXTAREA|SELECT|BUTTON)$/.test(el.tagName);
    const chk = (prop, v) => { if (isW(v)) hits.push({ el: where, prop, v }); };
    if (hasText) chk('color', cs.color);
    chk('background-color', cs.backgroundColor); if (cs.backgroundImage !== 'none') chk('background-image', cs.backgroundImage);
    for (const side of ['top', 'right', 'bottom', 'left'])
      if (cs.getPropertyValue('border-' + side + '-style') !== 'none' && parseFloat(cs.getPropertyValue('border-' + side + '-width')) > 0) chk('border-' + side + '-color', cs.getPropertyValue('border-' + side + '-color'));
    if (cs.outlineStyle !== 'none' && parseFloat(cs.outlineWidth) > 0) chk('outline-color', cs.outlineColor);
    if (cs.boxShadow !== 'none') chk('box-shadow', cs.boxShadow);
    if (cs.textShadow !== 'none') chk('text-shadow', cs.textShadow);
    if (el instanceof SVGElement) { chk('fill', cs.fill); chk('stroke', cs.stroke); }
  }
  return { n, hits };
})()`;
const whiteViews = [];
/* Every colour painted by the matched real elements (text, background, drawn
 * borders, box-shadow) must be a field-office token value; drawn rims >= 3:1
 * against the parent's background. Runs in field-office. */
const TOKSCAN = (sel) => `(() => { const P = window.__pal; const T = P.tokens(); const out = [];
  const cols = (v) => [...String(v).matchAll(/rgba?\\([^)]*\\)/g)].map((m) => P.rgba(m[0])).filter((c) => c[3] > 0);
  for (const el of document.querySelectorAll(${JSON.stringify(sel)})) { if (!el.getClientRects().length) continue; const cs = getComputedStyle(el);
    const where = el.tagName.toLowerCase() + '.' + String(el.className).trim().split(/\\s+/).join('.');
    const props = ['color', 'background-color', 'box-shadow'];
    for (const sd of ['top', 'right', 'bottom', 'left']) if (cs.getPropertyValue('border-' + sd + '-style') !== 'none' && parseFloat(cs.getPropertyValue('border-' + sd + '-width')) > 0) props.push('border-' + sd + '-color');
    for (const pr of props) { const v = cs.getPropertyValue(pr); const bad = cols(v).filter((c) => !T.has(P.hex(c))).map((c) => P.hex(c) + (c[3] < 1 ? '@' + c[3] : ''));
      const r = { el: where, prop: pr, v, bad };
      if (/border/.test(pr)) { const behind = P.bgOf(el.parentElement); r.ratio = Math.round(P.ratio(P.over(P.rgba(v), behind), behind) * 100) / 100;
        /* As C2: an opaque fill >= 3:1 against what is behind it carries the edge, and the rim then is not the boundary. */
        const fill = P.rgba(cs.backgroundColor); if (fill[3] === 1) { r.fillRatio = Math.round(P.ratio(fill, behind) * 100) / 100; if (r.fillRatio >= 3) r.edge = 'fill'; } }
      out.push(r); } }
  return out; })()`;
const tokCheck = async (cdp, tag, w, sel) => {
  const r = await cdp.eval(TOKSCAN(sel));
  for (const x of r) { if (x.bad.length) failures.push(`${tag}@${w} ${x.el} ${x.prop} paints ${x.bad.join(',')}, not a field-office token`);
    if (x.ratio !== undefined && x.ratio < 3 && x.edge !== 'fill') failures.push(`${tag}@${w} ${x.el} ${x.prop} rim ${x.ratio}:1, want >= 3:1`); }
  return r;
};
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
  const THEMES = ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office'];
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
      await whiteCheck(surface, w);
      for (const t of m.text) if (t.ratio < 4.5) failures.push(`C1 ${surface}@${w} text ${t.ratio}:1 ${t.fg} on ${t.bg} ${t.el} "${t.t}"`);
      for (const c of m.ctl) if (c.ratio < 3) failures.push(`C2 ${surface}@${w} control ${c.ratio}:1 border ${c.border} fill ${c.fill} behind ${c.behind} ${c.el}`);
      for (const x of m.white) failures.push(`C4 ${surface}@${w} near-white ${x.kind} ${x.c} ${x.el}`);
      if (m.n.text < 3) failures.push(`${surface}@${w} measured only ${m.n.text} text runs (surface not on screen?)`);
      console.log(`palette ${surface}@${w}: ${m.n.text} text (min ${Math.min(...m.text.map((t) => t.ratio)).toFixed(2)}), ` +
        `${m.n.ctl} controls (min ${m.ctl.length ? Math.min(...m.ctl.map((c) => c.ratio)).toFixed(2) : '-'}), ${m.white.length} near-white, ${m.n.exempt} disabled exempt, ${m.n.hidden} covered`);
    }
  };

  /* W1 in the current view; the theme must already be field-office. */
  const whiteCheck = async (surface, w) => {
    const wt = await cdp.eval(WHITE); whiteViews.push({ surface, w, n: wt.n, hits: wt.hits.length });
    for (const x of wt.hits.slice(0, 5)) failures.push(`W1 ${surface}@${w} white ${x.prop} ${x.v} on ${x.el}`);
    if (wt.hits.length > 5) failures.push(`W1 ${surface}@${w} ... ${wt.hits.length - 5} more white values`);
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

  /* K7: mention pills. */
  {
    const FAKE = 'npub1zq' + 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'.repeat(2).slice(0, 56);
    for (const [w, h] of WIDTHS) {
      await view(w, h); await cdp.eval(KIT);
      const k7 = await cdp.eval(`(() => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
        const major = robotModels()[0]; const note = document.querySelector('#stream .note');
        const tmp = document.createElement('div'); tmp.className = 'body'; note.appendChild(tmp);
        renderPreservingMentions('nostr:' + major.npub + ' please write a one-line welcome. nostr:${FAKE} too, and hi @${FAKE} end', tmp);
        tmp.scrollIntoView({ block: 'center' });
        const pills = [...tmp.querySelectorAll('.mention-pill')];
        const shape = { pills: pills.map((p) => p.textContent), titles: pills.map((p) => p.title), arias: pills.map((p) => p.getAttribute('aria-label') || ''), body: tmp.textContent, first: tmp.firstChild ? (tmp.firstChild.nodeType === 1 ? 'pill' : JSON.stringify(tmp.firstChild.nodeValue.slice(0, 3))) : null };
        for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t);
          for (const p of pills) { const bg = P.bgOf(p); let fg = P.rgba(getComputedStyle(p).color); fg = P.over([fg[0], fg[1], fg[2], fg[3] * P.opacityOf(p)], bg);
            out.push({ theme: t, pill: p.textContent, ratio: Math.round(P.ratio(fg, bg) * 100) / 100, fg: P.hex(fg), bg: P.hex(bg) }); } }
        h.setAttribute('data-theme', was); tmp.remove(); return { shape, out }; })()`);
      console.log(`palette K7 shape@${w}: ${JSON.stringify(k7.shape)}`);
      report.push({ surface: 'K7', w, k7 });
      if (k7.shape.first !== 'pill') failures.push(`K7@${w} mention line starts with ${k7.shape.first}, want the @Major pill`);
      if (/npub/i.test(k7.shape.body)) failures.push(`K7@${w} the note body text paints an npub: ${JSON.stringify(k7.shape.body)} (UI_SPEC: unresolved @npub1... tokens are not painted)`);
      if (!/npub1zq/.test(k7.shape.arias[1] || '')) failures.push(`K7@${w} the @unknown pill aria-label ${JSON.stringify(k7.shape.arias[1])} lacks the short npub`);
      if (k7.shape.pills.some((t) => /npub/i.test(t))) failures.push(`K7@${w} a pill paints an npub ${JSON.stringify(k7.shape.pills)} (UI_SPEC: unresolved @npub1... tokens are not painted)`);
      if (k7.shape.pills.length !== 3 || k7.shape.pills[1] !== '@unknown' || k7.shape.pills[2] !== '@unknown' || !/npub1zq/.test(k7.shape.titles[1] || '')) failures.push(`K7@${w} pills ${JSON.stringify(k7.shape.pills)} titles ${JSON.stringify(k7.shape.titles)}, want @Major and two @unknown pills with the short npub in their title`);
      for (const r of k7.out) if (r.ratio < 4.5) failures.push(`K7 ${r.theme}@${w} mention pill "${r.pill}" text ${r.ratio}:1 ${r.fg} on ${r.bg}`);
      console.log(`palette K7@${w}: ${k7.out.length} pill text measures (min ${k7.out.length ? Math.min(...k7.out.map((r) => r.ratio)) : '-'})`);
    }
    const rep7 = await cdp.eval(`(() => { const note = document.querySelector('#stream .note'); const tmp = document.createElement('div'); tmp.className = 'body'; note.appendChild(tmp);
      renderPreservingMentions('nostr:${FAKE} please check in', tmp); const before = { pending: tmp.classList.contains('mention-pending'), pill: tmp.querySelector('.mention-pill') ? tmp.querySelector('.mention-pill').textContent : null };
      const keep = session; setSession(Object.assign({}, session, { members: (session.members || []).concat([{ name: 'Kestrel', npub: '${FAKE}' }]) }));
      const after = { pending: tmp.classList.contains('mention-pending'), pill: tmp.querySelector('.mention-pill') ? tmp.querySelector('.mention-pill').textContent : null };
      setSession(keep); tmp.remove(); return { before, after }; })()`);
    console.log(`palette K7 repaint: ${JSON.stringify(rep7)}`); report.push({ surface: 'K7r', w: 390, k7r: rep7 });
    if (!rep7.before.pending || rep7.after.pending || rep7.after.pill !== '@Kestrel') failures.push(`K7 roster load does not rename the @unknown pill: ${JSON.stringify(rep7)}`);
    /* K7t: the live roster arrives through the real 1 s tick() (it assigns
       session directly, not via setSession). With the roster unchanged a
       pending pill is left alone (same DOM node after two ticks: no
       per-second re-render), also after an unrelated member joins; when that
       member leaves as the npub joins (roster size unchanged), one tick
       renames it. fetch is wrapped for /api/session only and restored. */
    const OTHER = 'npub1hawk' + 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'.repeat(2).slice(0, 54);
    const tick7 = await cdp.eval(`(async () => { await tick(); const note = document.querySelector('#stream .note'); const tmp = document.createElement('div'); tmp.className = 'body'; note.appendChild(tmp);
      const pill = () => tmp.querySelector('.mention-pill'); const state = () => ({ pending: tmp.classList.contains('mention-pending'), pill: pill() ? pill().textContent : null });
      renderPreservingMentions('nostr:${FAKE} please check in', tmp); let node = pill();
      await tick(); await tick(); const kept = pill() === node;
      const realFetch = window.fetch; let extra = [];
      window.fetch = (u, o) => realFetch(u, o).then((r) => String(u).indexOf('/api/session') < 0 ? r : r.json().then((j) => new Response(JSON.stringify(Object.assign({}, j, { members: (j.members || []).concat(extra) })), { headers: { 'content-type': 'application/json' } })));
      let keptAfterChange = null, after = null;
      try {
        extra = [{ name: 'Hawk', npub: '${OTHER}' }]; await tick(); node = pill(); await tick(); await tick(); keptAfterChange = pill() === node && state().pill === '@unknown';
        /* Same-size swap: Hawk leaves as Kestrel (the mentioned npub) joins in one tick. */
        extra = [{ name: 'Kestrel', npub: '${FAKE}' }]; await tick(); after = state();
      } finally { window.fetch = realFetch; await tick(); tmp.remove(); }
      return { kept, keptAfterChange, after }; })()`, true);
    console.log(`palette K7 tick: ${JSON.stringify(tick7)}`); report.push({ surface: 'K7t', w: 390, k7t: tick7 });
    if (!tick7.kept || !tick7.keptAfterChange) failures.push(`K7 tick re-renders a pending @unknown pill while the roster is unchanged: ${JSON.stringify(tick7)}`);
    if (!tick7.after || tick7.after.pending || tick7.after.pill !== '@Kestrel') failures.push(`K7 tick roster does not rename the @unknown pill: ${JSON.stringify(tick7)}`);
    /* K7p (Gauge #325 r5 finding 1, r6 finding 1): the live robot partial
       preview. A partial is robot output before the relay rewrites @npub1...
       tokens, so both real paths (paintThink builds the slot, refreshPartials
       fills it from /api/reply) must not paint "npub". The slot is NOT blanked
       between paths: refreshPartials polls twice into the slot paintThink drew
       and each read must be exactly K7P_WANT with 2 pills, so a paint that
       appends instead of replacing (paintPartial's clear reverted) fails.
       fetch is wrapped for /api/reply only and restored. */
    const part7 = await cdp.eval(`(async () => { const job = { parent: 'k7p-root', name: 'Major' }; const key = partialKey(job.parent, job.name);
      const text = 'draft for @${FAKE} and nostr:${FAKE} done';
      partials[key] = text; const host = document.createElement('div'); document.querySelector('#stream').appendChild(host);
      paintThink(host, [job]); const slot = host.querySelector('.partial');
      const think = { text: slot ? slot.textContent : null, pills: slot ? slot.querySelectorAll('.mention-pill').length : 0 };
      const realFetch = window.fetch; let refresh = null, refresh2 = null;
      const read = () => ({ text: slot ? slot.textContent : null, pills: slot ? slot.querySelectorAll('.mention-pill').length : 0 });
      window.fetch = (u, o) => String(u).indexOf('/api/reply') < 0 ? realFetch(u, o) : Promise.resolve(new Response(JSON.stringify({ running: true, text }), { headers: { 'content-type': 'application/json' } }));
      try { await refreshPartials({ thinking: [job] }); refresh = read(); await refreshPartials({ thinking: [job] }); refresh2 = read(); }
      finally { window.fetch = realFetch; delete partials[key]; host.remove(); }
      return { think, refresh, refresh2 }; })()`, true);
    console.log(`palette K7 partial: ${JSON.stringify(part7)}`); report.push({ surface: 'K7p', w: 390, k7p: part7 });
    const K7P_WANT = 'draft for @unknown and @unknown done';
    for (const [path, r] of [['paintThink', part7.think], ['refreshPartials', part7.refresh], ['refreshPartials poll 2', part7.refresh2]]) {
      if (!r || r.text === null) { failures.push(`K7p ${path} drew no .partial`); continue; }
      if (/npub/i.test(r.text)) failures.push(`K7p ${path} .partial paints an npub: ${JSON.stringify(r.text)} (UI_SPEC: unresolved @npub1... tokens are not painted)`);
      if (r.text !== K7P_WANT) failures.push(`K7p ${path} .partial reads ${JSON.stringify(r.text)}, want exactly ${JSON.stringify(K7P_WANT)} (a poll replaces the preview, it does not append)`);
      if (r.pills !== 2) failures.push(`K7p ${path} .partial has ${r.pills} mention pills, want 2 (@unknown x2)`);
    }
    /* K7k (Gauge #325 r5 finding 2): the roster key is the sorted npub=name
       pairs. Through the real tick(), with a pending @unknown pill:
       reordering the same members does not repaint (sort; T8); changing only
       a member's npub repaints (npub half; T9); changing only a member's
       name repaints (name half; T10). Repaint = the pill node is replaced. */
    const OWL = 'npub1owl' + 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'.repeat(2).slice(0, 55);
    const OWL2 = 'npub1owx' + 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'.repeat(2).slice(0, 55);
    const key7 = await cdp.eval(`(async () => { await tick(); const note = document.querySelector('#stream .note'); const tmp = document.createElement('div'); tmp.className = 'body'; note.appendChild(tmp);
      const pill = () => tmp.querySelector('.mention-pill');
      renderPreservingMentions('nostr:${FAKE} please check in', tmp);
      const realFetch = window.fetch; let extra = [];
      window.fetch = (u, o) => realFetch(u, o).then((r) => String(u).indexOf('/api/session') < 0 ? r : r.json().then((j) => new Response(JSON.stringify(Object.assign({}, j, { members: (j.members || []).concat(extra) })), { headers: { 'content-type': 'application/json' } })));
      const out = {};
      try {
        extra = [{ name: 'Hawk', npub: '${OTHER}' }, { name: 'Owl', npub: '${OWL}' }]; await tick(); await tick();
        let node = pill(); extra = [{ name: 'Owl', npub: '${OWL}' }, { name: 'Hawk', npub: '${OTHER}' }]; await tick(); out.reorderKept = pill() === node;
        node = pill(); extra = [{ name: 'Owl', npub: '${OWL2}' }, { name: 'Hawk', npub: '${OTHER}' }]; await tick(); out.npubRepaint = pill() !== node;
        node = pill(); extra = [{ name: 'Falcon', npub: '${OWL2}' }, { name: 'Hawk', npub: '${OTHER}' }]; await tick(); out.nameRepaint = pill() !== node;
        out.still = pill() ? pill().textContent : null;
      } finally { window.fetch = realFetch; await tick(); tmp.remove(); }
      return out; })()`, true);
    console.log(`palette K7 key: ${JSON.stringify(key7)}`); report.push({ surface: 'K7k', w: 390, k7k: key7 });
    if (!key7.reorderKept) failures.push(`K7k roster key is order-sensitive: a reordered roster repaints the pending pill ${JSON.stringify(key7)}`);
    if (!key7.npubRepaint) failures.push(`K7k roster key ignores the npub half: an npub-only change does not repaint ${JSON.stringify(key7)}`);
    if (!key7.nameRepaint) failures.push(`K7k roster key ignores the name half: a name-only change does not repaint ${JSON.stringify(key7)}`);
  }
  /* K8: inventory item rims. */
  {
    await view(1440, 900); await cdp.eval(KIT);
    const k8 = await cdp.eval(`(() => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
      /* All roster robots may be locked: add one unlocked item (same ink the app uses) so the live rim is measured too. */
      let spot = null; for (let y = 0; y < INV_ROWS && !spot; y++) for (let x = 0; x < INV_COLS && !spot; x++) if (isSpaceFree(x, y, 1, 1, null)) spot = [x, y];
      const live = (typeof INV_INK_LIVE === 'string') ? INV_INK_LIVE : '#0ff';
      if (spot) invItems.push({ id: 'k8-live', name: 'K8 live', w: 1, h: 1, gx: spot[0], gy: spot[1], color: live });
      renderAllInventories();
      for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t); const T = P.tokens();
        const items = [...document.querySelectorAll('#robot-inventory .inv-item')];
        const rims = items.map((it) => { const c = P.rgba(getComputedStyle(it).borderTopColor); const behind = P.bgOf(it.parentElement);
          return { rim: P.hex(c), token: T.get(P.hex(c)) || null, ratio: Math.round(P.ratio(P.over(c, behind), behind) * 100) / 100 }; });
        out.push({ theme: t, items: items.length, rims: [...new Set(rims.map((r) => r.rim))], tokens: [...new Set(rims.map((r) => r.token))], min: rims.length ? Math.min(...rims.map((r) => r.ratio)) : 0 }); }
      /* K8b: the drag-not-allowed ink (INV_INK_BAD) as a rim on a grid cell, in every theme. */
      const grid = document.querySelector('#robot-inventory .inv-item').parentElement; const bad = document.createElement('div'); bad.className = 'inv-item';
      bad.style.borderColor = INV_INK_BAD; bad.style.borderStyle = 'solid'; bad.style.borderWidth = '2px'; grid.appendChild(bad);
      const badOut = [];
      for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t); const T = P.tokens(); const c = P.rgba(getComputedStyle(bad).borderTopColor); const behind = P.bgOf(grid);
        badOut.push({ theme: t, rim: P.hex(c), token: T.get(P.hex(c)) || null, ratio: Math.round(P.ratio(P.over(c, behind), behind) * 100) / 100 }); }
      bad.remove();
      h.setAttribute('data-theme', was); invItems = invItems.filter((it) => it.id !== 'k8-live'); renderAllInventories(); return { out, badOut }; })()`);
    for (const r of k8.badOut) {
      console.log(`palette K8b ${r.theme}: ${JSON.stringify(r)}`); report.push({ surface: 'K8b', w: 1440, k8b: r });
      if (r.theme === 'field-office') { if (!r.token || r.ratio < 3) failures.push(`K8b field-office drag-not-allowed rim ${r.rim} token ${r.token} ${r.ratio}:1`); }
      else if (r.rim !== '#ff6666') failures.push(`K8b ${r.theme} drag-not-allowed rim changed to ${r.rim} (want the old #ff6666)`);
    }
    for (const r of k8.out) {
      console.log(`palette K8 ${r.theme}: ${JSON.stringify(r)}`); report.push({ surface: 'K8', w: 1440, k8: r });
      if (r.items < 2) { failures.push(`K8 ${r.theme} no inventory items rendered`); continue; }
      if (r.theme === 'field-office') { if (r.tokens.includes(null) || r.min < 3) failures.push(`K8 field-office inventory rim ${r.rims.join(',')} tokens ${r.tokens.join(',')} min ${r.min}:1`); }
      else if (r.rims.some((c) => c !== '#00ffff' && c !== '#d4a017')) failures.push(`K8 ${r.theme} inventory rim changed to ${r.rims.join(',')} (want the old #00ffff / #d4a017)`);
    }
  }
  /* L1: the shared literal-colour rules (G291-2 follow-up). */
  for (const [w, h] of WIDTHS) {
    await view(w, h); await cdp.eval(KIT);
    const nfix = await cdp.eval(`(${l1Fixture.toString()})()`);
    await cdp.send('DOM.enable', {}); await cdp.send('CSS.enable', {});
    const root = (await cdp.send('DOM.getDocument', { depth: 0 })).root.nodeId; const forced = [];
    for (const sel of ['#inv-seed', '#qb-config', '#thread-send']) { const q = await cdp.send('DOM.querySelector', { nodeId: root, selector: sel });
      if (q.nodeId) { await cdp.send('CSS.forcePseudoState', { nodeId: q.nodeId, forcedPseudoClasses: ['active'] }); forced.push(q.nodeId); } }
    await pinTheme(); await whiteCheck('L1-fixture', w);
    const l1 = await cdp.eval(`(() => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
      const cols = (v) => [...v.matchAll(/rgba?\\([^)]*\\)/g)].map((m) => P.rgba(m[0])).filter((c) => c[3] > 0);
      for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t); const T = P.tokens();
        for (const s of window.__l1spec) { const el = window.__l1[s.key]; const v = getComputedStyle(el).getPropertyValue(s.prop); const r = { theme: t, key: s.key, prop: s.prop, v };
          if (t === 'field-office') {
            r.untok = cols(v).filter((c) => c[3] < 1 || !T.has(P.hex(c))).map((c) => c[3] < 1 ? 'rgba(' + c.join(',') + ')' : P.hex(c));
            if (s.prop === 'color') { const bg = P.bgOf(el); let fg = P.rgba(v); fg = P.over([fg[0], fg[1], fg[2], fg[3] * P.opacityOf(el)], bg); r.ratio = Math.round(P.ratio(fg, bg) * 100) / 100; r.min = 4.5; }
            else if (/border/.test(s.prop)) { const behind = P.bgOf(el.parentElement); const c = P.rgba(v); r.ratio = Math.round(P.ratio(P.over(c, behind), behind) * 100) / 100; r.min = 3; }
          }
          out.push(r); } }
      h.setAttribute('data-theme', was); window.__l1undo(); return out; })()`);
    for (const id of forced) await cdp.send('CSS.forcePseudoState', { nodeId: id, forcedPseudoClasses: [] });
    report.push({ surface: 'L1', w, l1 });
    let n = 0;
    for (const r of l1) {
      const k = r.key + '|' + r.prop;
      if (r.theme === 'field-office') {
        n++;
        if (r.untok.length) failures.push(`L1 field-office@${w} ${k} paints ${r.untok.join(',')}, not a field-office token (${r.v})`);
        if (r.ratio !== undefined && r.ratio < r.min) failures.push(`L1 field-office@${w} ${k} ${r.ratio}:1, want >= ${r.min}:1`);
      } else if (L1_EXPECT[k] !== undefined) {
        const e = L1_EXPECT[k]; const want = typeof e === 'string' ? e : (e[r.theme] || (w < 641 && e['@390']) || e['*']);
        if (r.v !== want) failures.push(`L1 ${r.theme}@${w} ${k} is ${r.v}, want the unchanged ${want}`);
      }
    }
    const fo = l1.filter((r) => r.theme === 'field-office' && r.ratio !== undefined);
    console.log(`palette L1@${w}: ${nfix} fixture declarations, ${n} field-office measures, ${l1.length - n} other-theme values, ` +
      `min field-office ratio ${fo.length ? Math.min(...fo.map((r) => r.ratio)) : '-'}`);
    if (nfix < 40) failures.push(`L1@${w} only ${nfix} fixture declarations (want 40)`);
  }
  await ringCheck('main-thread', ['#stream .thread-btn', '#stream .note-files button', '#stream .think-stop', '#nav-toggle', '#rail-toggle']);

  await measure('thread-pane', async () => {
    await cdp.eval(`(() => { const p = document.querySelector('#thread-pane'); if (!p.classList.contains('show')) document.querySelector('#stream .thread-btn').click(); })()`);
    await cdp.waitFor(`document.querySelector('#thread-pane').classList.contains('show') && document.querySelectorAll('#thread-stream .note').length >= 2`, 'thread pane');
  });
  await ringCheck('thread-pane', ['#thread-msg', '#thread-close']);
  /* K3: whole placeholder visible in the milestone fields, 8 themes x 2 widths. */
  for (const [w, h] of WIDTHS) {
    await view(w, h);
    await cdp.eval(KIT);
    const k3 = await cdp.eval(`(() => { const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
      const ctx = document.createElement('canvas').getContext('2d');
      for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t);
        for (const el of document.querySelectorAll('#thread-desk input, #milestone-form input')) {
          const r = el.getBoundingClientRect(); if (r.width < 2) { out.push({ theme: t, id: el.id || el.name, hidden: true }); continue; }
          const ph = getComputedStyle(el, '::placeholder'); const cs = getComputedStyle(el);
          ctx.font = ph.fontStyle + ' ' + ph.fontWeight + ' ' + ph.fontSize + ' ' + ph.fontFamily; ctx.letterSpacing = ph.letterSpacing === 'normal' ? '0px' : ph.letterSpacing;
          let txt = el.placeholder; if (ph.textTransform === 'uppercase') txt = txt.toUpperCase();
          const need = Math.ceil(ctx.measureText(txt).width); const room = Math.floor(el.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight));
          out.push({ theme: t, id: el.id || el.placeholder, need, room, fits: need <= room }); } }
      h.setAttribute('data-theme', was); return out; })()`);
    for (const r of k3) {
      report.push({ surface: 'K3', w, k3: r });
      if (r.hidden) { if (r.id.startsWith('thread-desk')) failures.push(`K3 ${r.theme}@${w} ${r.id} not on screen`); continue; }
      if (!r.fits) failures.push(`K3 ${r.theme}@${w} ${r.id} placeholder needs ${r.need}px, field has ${r.room}px (clipped)`);
    }
    const shown = k3.filter((r) => !r.hidden);
    console.log(`palette K3@${w}: ${shown.filter((r) => r.fits).length}/${shown.length} milestone fields show the whole placeholder (min room-need ${Math.min(...shown.map((r) => r.room - r.need))}px)`);
  }
  await cdp.eval(`(() => { const x = document.querySelector('#thread-close'); if (x) x.click(); })()`);
  await sleep(300);
  await measure('settings', async () => {
    await cdp.eval(`document.querySelector('#settings').classList.remove('show')`);
    await cdp.eval(`document.querySelector('#settings-btn').click()`);
    await cdp.waitFor(`document.querySelector('#settings').classList.contains('show')`, 'settings');
  });
  await ringCheck('settings', ['#turn-on', '#vibe-public', '#settings input[name="theme"]:checked', '#settings-close']);
  /* R1: theme radios take the theme's own --accent (no native blue), in every
   * theme. Both sides go through computed colour so oklch / hex compare equal. */
  {
    const themes = ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office'];
    const r1 = await cdp.eval(`(() => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
      const probe = document.createElement('i'); document.body.appendChild(probe);
      for (const t of ${JSON.stringify(themes)}) {
        h.setAttribute('data-theme', t);
        const radio = document.querySelector('#settings input[name="theme"]:checked') || document.querySelector('#settings input[name="theme"]');
        probe.style.color = 'var(--accent)'; const want = getComputedStyle(probe).color;
        const raw = getComputedStyle(radio).accentColor; probe.style.color = raw === 'auto' ? '' : raw;
        const got = raw === 'auto' ? 'auto' : getComputedStyle(probe).color;
        const behind = P.bgOf(radio.parentElement);
        out.push({ theme: t, accentColor: raw, got, want, match: got === want,
          ratio: got === 'auto' ? 0 : Math.round(P.ratio(P.rgba(got), behind) * 100) / 100 });
      }
      probe.remove(); h.setAttribute('data-theme', was); return out; })()`);
    for (const r of r1) {
      console.log(`palette R1 ${r.theme}: ${JSON.stringify(r)}`);
      report.push({ surface: 'R1:' + r.theme, w: 1440, r1: r });
      if (!r.match) failures.push(`R1 theme radio accent-color in ${r.theme} is ${r.accentColor}, want the theme's --accent ${r.want}`);
      if (r.theme === 'field-office' && r.ratio < 3) failures.push(`R1 field-office checked radio fill ${r.ratio}:1 vs panel, want >= 3`);
    }
    if (r1.length !== 8) failures.push('R1 measured ' + r1.length + ' themes, want 8');
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
  /* K1: Developer Log with real log lines (appendDevLog), field-office. */
  await measure('dialog-dev-log-lines', async () => {
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
      if (!document.getElementById('dev-log-body').textContent) { appendDevLog('a0d89f7a: Mention received.'); appendDevLog('1f6a6361: Mention received.'); }
      document.getElementById('dev-log-drawer').classList.add('show'); })()`);
    await sleep(150);
  });
  {
    await pinTheme(); await cdp.eval(KIT);
    const k1 = await cdp.eval(`(() => { const P = window.__pal; const T = P.tokens(); const el = document.getElementById('dev-log-body'); const cs = getComputedStyle(el);
      const bg = P.hex(P.rgba(cs.backgroundColor)), fg = P.hex(P.rgba(cs.color));
      return { lines: el.textContent.split('\\n').filter(Boolean).length, bg, bgToken: T.get(bg) || null, fg, fgToken: T.get(fg) || null }; })()`);
    console.log(`palette K1: ${JSON.stringify(k1)}`); report.push({ surface: 'K1', w: 390, k1 });
    if (k1.lines < 2) failures.push(`K1 Developer Log fixture shows ${k1.lines} lines, want 2`);
    if (!k1.bgToken || !k1.fgToken) failures.push(`K1 Developer Log text box paints ${k1.bg} / ${k1.fg}, want field-office tokens (got ${k1.bgToken} / ${k1.fgToken})`);
  }
  /* K2 + K5: robot drawer with two providers and the picture picker, painted by the app's own functions. */
  const AGENT_FIXTURE = `(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
    draftProviders.splice(0, draftProviders.length, 'grok', 'claude'); paintAgentProviderOrder(); paintAgentPicPicker();
    const d = document.getElementById('agent-drawer'); d.classList.add('show'); const p = d.querySelector('.panel'); if (p) p.scrollTop = 0;
    const pills = document.getElementById('agent-provider-pills'); if (pills) pills.scrollIntoView({ block: 'center' }); })()`;
  await measure('dialog-agent-pills', async () => { await cdp.eval(AGENT_FIXTURE); await sleep(150); });
  /* K9 (Ops #325 P1): the real call stage (openStage + addTile: own tile, a robot
     tile with Mute, a second robot tile muted) passes C1/C2/C4/W1 in field-office. */
  await measure('call-stage', async () => {
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
      document.getElementById('tiles').innerHTML = ''; openStage('Call · Payne', 'One-to-one with Payne. Mute any voice you do not want.');
      addTile(selfId, null, 'You · human'); addTile('k9-payne', null, 'Payne · agent'); addTile('k9-scout', null, 'Scout · robot');
      const m = document.querySelector('#tile-k9-scout .tile-mute'); if (m && !m.classList.contains('on')) m.click(); })()`);
    await sleep(150);
  });
  {
    const k9 = report.filter((r) => r.surface === 'call-stage').map((r) => ({ w: r.w, tile: r.text.filter((t) => /tile/.test(t.el)) }));
    for (const r of k9) {
      console.log(`palette K9@${r.w}: ${r.tile.length} call-tile text runs (min ${r.tile.length ? Math.min(...r.tile.map((t) => t.ratio)) : '-'}) ${JSON.stringify(r.tile.map((t) => [t.t, t.ratio, t.fg, t.bg]))}`);
      report.push({ surface: 'K9', w: r.w, k9: r.tile });
      if (r.tile.length < 5) failures.push(`K9@${r.w} only ${r.tile.length} call-tile text runs measured (want 3 labels + Mute + Muted)`);
    }
    for (const [w, h] of WIDTHS) { await view(w, h); await pinTheme(); await cdp.eval(KIT);
      const t = await tokCheck(cdp, 'K9 tokens', w, '#stage .tile, #stage .tile video, #stage .tile span, #stage .tile-mute');
      console.log(`palette K9 tokens@${w}: ${t.length} values, ${t.filter((x) => x.bad.length).length} off-token, min rim ${Math.min(...t.filter((x) => x.ratio !== undefined).map((x) => x.ratio))}, edge by fill ${t.filter((x) => x.edge === 'fill').length} (min fill ${Math.min(...t.filter((x) => x.edge === 'fill').map((x) => x.fillRatio))})`); }
    await cdp.eval(`(() => { document.getElementById('stage').classList.remove('show'); document.getElementById('tiles').innerHTML = ''; })()`);
  }
  /* K10 (Ops #325 P2): the real skills board in the robot drawer (paintSkillBoard:
     lifetime shelves, gems, 3x3 doll and portrait) and the skill sheet chips
     (paintSkillSheet), field-office. */
  await measure('agent-skills', async () => {
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
      /* The test relay ships no skills: a fixture catalog (one per scope; robot scope = on-call) if empty, kept for the sheet below. */
      if (!skillCatalog.length) { window.__k10cat = true; skillCatalog = [
        { id: 'k10-sys', name: 'Field notes', scope: 'system', summary: 'Keep short notes.', chars: 120, complexity: 1 },
        { id: 'k10-user', name: 'Brief me', scope: 'user', summary: 'One-line brief.', chars: 80, complexity: 1 },
        { id: 'k10-bot', name: 'Radio log', scope: 'robot', summary: 'Log radio calls.', chars: 90, complexity: 1 },
        { id: 'k10-bot2', name: 'Night watch', scope: 'robot', summary: 'Watch at night.', chars: 90, complexity: 1 }]; }
      window.__k10eq = equippedSkills.slice(); if (!equippedSkills.length) equippedSkills.push('k10-bot2');
      const d = document.getElementById('agent-drawer'); d.classList.add('show'); paintSkillBoard(editSlug);
      const l = document.getElementById('skill-loadout') || document.getElementById('skill-armory'); if (l) l.scrollIntoView({ block: 'center' }); })()`);
    await sleep(150);
  });
  {
    const k10 = await cdp.eval(`(() => ({ gems: document.querySelectorAll('#agent-drawer .skill-gem').length, life: document.querySelectorAll('#agent-drawer .skill-gem .life').length, doll: document.querySelectorAll('#agent-drawer .skill-doll-pic').length }))()`);
    console.log(`palette K10 board: ${JSON.stringify(k10)}`); report.push({ surface: 'K10', k10 });
    if (!k10.doll || !k10.gems || k10.life < 2) failures.push(`K10 skills board not painted: ${JSON.stringify(k10)}`);
    for (const [w, h] of WIDTHS) { await view(w, h); await pinTheme(); await cdp.eval(KIT);
      const t = await tokCheck(cdp, 'K10 tokens', w, '#agent-drawer .skill-gem, #agent-drawer .skill-gem .life, #agent-drawer .skill-life, #agent-drawer .skill-doll-pic, #agent-drawer .skill-slot');
      console.log(`palette K10 tokens@${w}: ${t.length} values, ${t.filter((x) => x.bad.length).length} off-token, min rim ${Math.min(...t.filter((x) => x.ratio !== undefined).map((x) => x.ratio))}, edge by fill ${t.filter((x) => x.edge === 'fill').length} (min fill ${Math.min(...t.filter((x) => x.edge === 'fill').map((x) => x.fillRatio))})`); }
  }
  await measure('skill-sheet', async () => {
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
      skillSheetView = 'robot'; if (equippedSkills.indexOf('k10-sys') < 0 && window.__k10cat) equippedSkills.push('k10-sys');
      openSkillSheet(); })()`);
    await sleep(150);
  });
  {
    const k10s = await cdp.eval(`document.querySelectorAll('#skill-drawer .skill-life').length`);
    console.log(`palette K10 sheet: ${k10s} lifetime chips`); report.push({ surface: 'K10s', chips: k10s });
    if (!k10s) failures.push('K10 skill sheet shows no lifetime chip');
    await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
      equippedSkills.splice(0, equippedSkills.length, ...(window.__k10eq || [])); if (window.__k10cat) { skillCatalog = []; delete window.__k10cat; } })()`);
  }
  {
    const k2 = report.filter((r) => r.surface === 'dialog-agent-pills').map((r) => r.text.filter((t) => t.el.includes('pill')));
    for (let i = 0; i < k2.length; i++) {
      console.log(`palette K2@${WIDTHS[i][0]}: ${k2[i].length} provider pill text runs (min ${k2[i].length ? Math.min(...k2[i].map((t) => t.ratio)) : '-'})`);
      report.push({ surface: 'K2', w: WIDTHS[i][0], k2: k2[i] });
      if (k2[i].length < 2) failures.push(`K2@${WIDTHS[i][0]} measured ${k2[i].length} provider pill text runs, want >= 2 (fixture not painted?)`);
    }
    await view(1440, 900); await cdp.eval(AGENT_FIXTURE); await cdp.eval(KIT);
    const k5 = await cdp.eval(`(() => { const P = window.__pal; const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
      for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t); const T = P.tokens(); const cells = [...document.querySelectorAll('#agent-drawer .agent-pic-cell')];
        const bgs = [...new Set(cells.map((c) => P.hex(P.rgba(getComputedStyle(c).backgroundColor))))];
        out.push({ theme: t, cells: cells.length, bgs, off: bgs.filter((b) => !T.has(b)) }); }
      h.setAttribute('data-theme', was); return out; })()`);
    for (const r of k5) {
      console.log(`palette K5 ${r.theme}: ${JSON.stringify(r)}`); report.push({ surface: 'K5', w: 1440, k5: r });
      if (r.cells < 1) failures.push(`K5 ${r.theme} no robot picture cells painted`);
      else if (r.off.length) failures.push(`K5 ${r.theme} picture cell background ${r.off.join(', ')} is not a theme token`);
    }
  }
  await cdp.eval(`(() => { for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show'); })()`);
  /* K4: field-office header status light at 390, both states. */
  {
    await view(390, 844); await pinTheme(); await cdp.eval(KIT);
    const k4 = await cdp.eval(`(() => { const P = window.__pal; const T = P.tokens(); const b = document.getElementById('badge'); const had = b.classList.contains('ok'); const out = [];
      for (const ok of [false, true]) { b.classList.toggle('ok', ok); const cs = getComputedStyle(b); const behind = P.bgOf(b.parentElement);
        const fc = P.rgba(cs.backgroundColor); const fill = fc[3] > 0 ? P.hex(fc) : 'transparent'; const border = P.hex(P.rgba(cs.borderTopColor));
        out.push({ state: ok ? 'ok' : 'not-ok', w: b.offsetWidth, image: cs.backgroundImage, shadow: cs.boxShadow, fill, fillToken: fill === 'transparent' ? 'transparent' : (T.get(fill) || null), border, borderToken: T.get(border) || null,
          shadowOff: (cs.boxShadow.match(/rgba?\\([^)]*\\)/g) || []).map((c) => P.rgba(c)).filter((c) => c[3] > 0 && (!T.has(P.hex(c)) || P.lum(c) >= 0.8)).map((c) => P.hex(c)),
          ratio: Math.round(Math.max(fc[3] > 0 ? P.ratio(P.over(fc, behind), behind) : 0, parseFloat(cs.borderTopWidth) >= 1 ? P.ratio(P.over(P.rgba(cs.borderTopColor), behind), behind) : 0) * 100) / 100, behind: P.hex(behind) }); }
      b.classList.toggle('ok', had); return out; })()`);
    for (const r of k4) {
      console.log(`palette K4 ${r.state}@390: ${JSON.stringify(r)}`); report.push({ surface: 'K4', w: 390, k4: r });
      if (r.image !== 'none' || r.shadowOff.length || !r.fillToken || !r.borderToken || r.ratio < 3)
        failures.push(`K4 field-office 390 status light (${r.state}) image ${r.image.slice(0, 40)} off-palette glow ${r.shadowOff.join(',')} fill ${r.fill} (${r.fillToken}) border ${r.border} (${r.borderToken}) ${r.ratio}:1`);
    }
    if (k4[0].fill === k4[1].fill && k4[0].border === k4[1].border) failures.push('K4 status light looks the same in both states');
  }
  /* K6: field-office masthead chrome only in field-office (1440). */
  {
    await view(1440, 900); await cdp.eval(KIT);
    const k6 = await cdp.eval(`(() => { const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
      const sels = ['.fo-edition', '.fo-hq > span', '.fo-log-stamp'];
      for (const t of ${JSON.stringify(THEMES)}) { h.setAttribute('data-theme', t);
        for (const sel of sels) { const el = document.querySelector(sel); const r = el ? el.getBoundingClientRect() : null;
          out.push({ theme: t, sel, shown: !!r && r.width > 0 && r.height > 0 && getComputedStyle(el).visibility === 'visible' }); } }
      h.setAttribute('data-theme', was); return out; })()`);
    for (const r of k6) {
      report.push({ surface: 'K6', w: 1440, k6: r });
      if (r.theme === 'field-office' ? !r.shown : r.shown) failures.push(`K6 ${r.sel} ${r.shown ? 'shows' : 'hidden'} in ${r.theme}`);
    }
    console.log(`palette K6@1440: ${k6.filter((r) => r.theme === 'field-office' && r.shown).length} field-office chrome items shown in field-office, ${k6.filter((r) => r.theme !== 'field-office' && r.shown).length} shown in the other 7 themes`);
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

  /* K11 (CoS 10:54): the channel voice button (.chan-voice) exists only when
     /api/status says whisper:true. fetch is wrapped so /api/status reports it,
     the real tick() + paintChannels() draw it, and the BOARDS list is measured
     at 1440 and 390, at rest and under a CDP-forced :hover, in field-office:
     glyph >= 4.5:1, and the rim or the opaque fill >= 3:1 against the row. */
  {
    await cdp.eval(`(async () => { if (!window.__k11fetch) { const rf = window.fetch; window.__k11fetch = rf;
      window.fetch = (u, o) => rf(u, o).then((r) => String(u).indexOf('/api/status') < 0 ? r : r.json().then((j) => new Response(JSON.stringify(Object.assign({}, j, { whisper: true })), { headers: { 'content-type': 'application/json' } }))); }
      await tick(); paintChannels();
      /* Hold the channel list still while :hover is forced (the 1 s tick repaints it, replacing the forced nodes). */
      window.__k11paint = paintChannels; paintChannels = () => {}; })()`, true);
    await cdp.send('DOM.enable', {}); await cdp.send('CSS.enable', {});
    for (const [w, h] of WIDTHS) {
      await view(w, h);
      if (w < 641) { await cdp.eval(`(() => { if (!document.querySelector('#hive').classList.contains('nav-open')) document.querySelector('#nav-toggle').click(); })()`);
        await cdp.waitFor(`document.querySelector('#hive').classList.contains('nav-open')`, 'boards drawer'); await sleep(400); }
      await pinTheme(); await cdp.eval(KIT);
      const root = (await cdp.send('DOM.getDocument', { depth: 0 })).root.nodeId;
      const ids = (await cdp.send('DOM.querySelectorAll', { nodeId: root, selector: '.chan-voice' })).nodeIds;
      for (const state of ['rest', 'hover']) {
        for (const id of ids) await cdp.send('CSS.forcePseudoState', { nodeId: id, forcedPseudoClasses: state === 'hover' ? ['hover'] : [] });
        await sleep(100);
        const k11 = await cdp.eval(`(() => { const P = window.__pal; const T = P.tokens(); return [...document.querySelectorAll('.chan-voice')].filter((el) => el.getClientRects().length).map((el) => {
          const cs = getComputedStyle(el); const behind = P.bgOf(el.parentElement); const fillC = P.rgba(cs.backgroundColor); const bg = P.bgOf(el);
          const fg = P.rgba(cs.color); const rim = P.rgba(cs.borderTopColor);
          return { text: Math.round(P.ratio(P.over(fg, bg), bg) * 100) / 100, rim: Math.round(P.ratio(P.over(rim, behind), behind) * 100) / 100,
            fill: fillC[3] === 1 ? Math.round(P.ratio(fillC, behind) * 100) / 100 : null, fg: P.hex(fg), bg: P.hex(bg), border: P.hex(rim), behind: P.hex(behind),
            tokens: [P.hex(fg), P.hex(rim)].concat(fillC[3] > 0 ? [P.hex(fillC)] : []).every((c) => T.has(c)) }; }); })()`);
        console.log(`palette K11@${w} ${state}: ${k11.length} voice buttons ${JSON.stringify(k11[0] || null)} (min text ${Math.min(...k11.map((r) => r.text))}, min edge ${Math.min(...k11.map((r) => Math.max(r.rim, r.fill || 0)))})`);
        report.push({ surface: 'K11', w, state, k11 });
        if (!k11.length) failures.push(`K11@${w} no .chan-voice drawn with whisper:true`);
        for (const r of k11) {
          if (r.text < 4.5) failures.push(`K11@${w} ${state} chan-voice glyph ${r.text}:1 ${r.fg} on ${r.bg}`);
          if (Math.max(r.rim, r.fill || 0) < 3) failures.push(`K11@${w} ${state} chan-voice edge ${r.rim}:1 rim ${r.border} / fill ${r.fill} on ${r.behind}, want >= 3:1`);
          if (!r.tokens) failures.push(`K11@${w} ${state} chan-voice paints off-token ${r.fg}/${r.border}`);
        }
      }
      for (const id of ids) await cdp.send('CSS.forcePseudoState', { nodeId: id, forcedPseudoClasses: [] });
      await whiteCheck('K11-boards', w);
    }
    /* K11o (Gauge #325 r5 finding 4): in the other 7 themes the voice button
       keeps its pre-PR paint through the var(--fo-voice-*, <old>) fallbacks
       (index.html:118-119): rim --accent-dim, glyph --accent; under :hover
       fill --accent-dim, glyph --accent. */
    {
      const root = (await cdp.send('DOM.getDocument', { depth: 0 })).root.nodeId;
      const ids = (await cdp.send('DOM.querySelectorAll', { nodeId: root, selector: '.chan-voice' })).nodeIds;
      const k11o = [];
      for (const state of ['rest', 'hover']) {
        for (const id of ids) await cdp.send('CSS.forcePseudoState', { nodeId: id, forcedPseudoClasses: state === 'hover' ? ['hover'] : [] });
        k11o.push(...await cdp.eval(`(() => { const h = document.documentElement; const was = h.getAttribute('data-theme'); const out = [];
          const res = (v) => { const d = document.createElement('i'); d.style.color = v; document.body.appendChild(d); const c = getComputedStyle(d).color; d.remove(); return c; };
          for (const t of ${JSON.stringify(THEMES.filter((t) => t !== 'field-office'))}) { h.setAttribute('data-theme', t);
            const el = document.querySelector('.chan-voice'); if (!el) { out.push({ theme: t, state: '${state}', missing: true }); continue; }
            const cs = getComputedStyle(el); const dim = res('var(--accent-dim)'), acc = res('var(--accent)');
            out.push({ theme: t, state: '${state}', ink: cs.color === acc, rim: cs.borderTopColor === dim, fill: '${state}' === 'hover' ? cs.backgroundColor === dim : true }); }
          h.setAttribute('data-theme', was); return out; })()`));
      }
      for (const id of ids) await cdp.send('CSS.forcePseudoState', { nodeId: id, forcedPseudoClasses: [] });
      console.log(`palette K11o: ${k11o.length} theme x state reads, ${k11o.filter((r) => !r.missing && r.ink && r.rim && r.fill).length} on the pre-PR paint`);
      report.push({ surface: 'K11o', k11o });
      for (const r of k11o) {
        if (r.missing) { failures.push(`K11o ${r.theme} ${r.state} no .chan-voice`); continue; }
        if (!r.ink || !r.rim || !r.fill) failures.push(`K11o ${r.theme} ${r.state} chan-voice off its pre-PR paint: ink(--accent)=${r.ink} rim(--accent-dim)=${r.rim} hover fill(--accent-dim)=${r.fill}`);
      }
    }
    await cdp.eval(`(async () => { if (window.__k11paint) { paintChannels = window.__k11paint; delete window.__k11paint; } if (window.__k11fetch) { window.fetch = window.__k11fetch; delete window.__k11fetch; } await tick(); paintChannels(); })()`, true);
  }

  console.log(`palette W1: ${whiteViews.length} field-office views, ${whiteViews.reduce((n, v) => n + v.n, 0)} element scans, ${whiteViews.reduce((n, v) => n + v.hits, 0)} white values`);
  report.push({ surface: 'W1', whiteViews });
  if (process.env.PALETTE_ART) {
    fs.mkdirSync(process.env.PALETTE_ART, { recursive: true });
    fs.writeFileSync(path.join(process.env.PALETTE_ART, 'palette-report.json'), JSON.stringify(report, null, 1));
  }
  cleanup();
  void chromeProc;
  const texts = report.reduce((n, r) => n + (r.text || []).length, 0);
  const ctls = report.reduce((n, r) => n + (r.ctl || []).length, 0);
  if (failures.length) {
    const uniq = [...new Set(failures)];
    for (const f of uniq.slice(0, 60)) console.error('palette check failed: ' + f);
    if (uniq.length > 60) console.error(`palette check failed: ... ${uniq.length - 60} more`);
    process.exit(1);
  }
  const views = report.filter((r) => r.text).length;
  const rings = report.filter((r) => r.ring).length;
  const themes = report.filter((r) => r.r1).length;
  console.log(`palette check passed: ${views} surface views, ${texts} text runs >= 4.5:1, ${ctls} controls >= 3:1, ` +
    `${rings} focus rings >= 2px and >= 3:1, no near-white, theme radio accent matches in ${themes} themes, ` +
    'ink-stamp actions 20-24px with 44px touch hit at 390, ' +
    `K1 dev log on tokens, K2 ${report.filter((r) => r.k2).reduce((n, r) => n + r.k2.length, 0)} provider pill text runs, ` +
    `K3 ${report.filter((r) => r.k3 && !r.k3.hidden).length} milestone fields fit their placeholder, K4 status light 2 states on tokens, ` +
    `K5 picture cells on tokens in ${report.filter((r) => r.k5).length} themes, K6 masthead chrome field-office only, ` +
    `K7 ${report.filter((r) => r.k7).reduce((n, r) => n + r.k7.out.length, 0)} mention pill text measures >= 4.5:1 + @unknown fallback (no npub painted) renamed on roster load, ` +
    `K8 inventory rims: field-office tokens, other ${report.filter((r) => r.k8 && r.k8.theme !== 'field-office').length} themes unchanged, ` +
    `L1 ${report.filter((r) => r.l1).reduce((n, r) => n + r.l1.filter((x) => x.theme === 'field-office').length, 0)} shared-rule values on field-office tokens, ` +
    `${report.filter((r) => r.l1).reduce((n, r) => n + r.l1.filter((x) => x.theme !== 'field-office' && L1_EXPECT[x.key + '|' + x.prop] !== undefined).length, 0)} other-theme values unchanged`);
}

main().catch((e) => die(e && e.stack ? e.stack : String(e)));
