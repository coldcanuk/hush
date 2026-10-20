// check_pair_ui.cjs: the unpaired-window card (#336 follow-up, Ops #364 c3).
// A window without the hush_session cookie gets 401 from every /api route
// except /api/status. It must show "Pair this window" (not the first-run
// splash), stop asking for events/presence, back off its polling, try
// POST /api/window once, and leave the card without a reload once the
// cookie is there. A paired first run still shows the splash, and a paired
// hive polls with no 401. Headless system Chrome over CDP, Node standard
// library only. Counts come from CDP Network/Log events, not page code.
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// HUSH_TEST_WAIT_S (Chrome DevTools ready deadline, default 30),
// PAIR_PROBE=1 prints the unpaired counts only (for measuring a build
// without the card, e.g. main).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

const RELAY = process.env.HUSH_RELAY_BIN || path.join(__dirname, '..', 'hush-relay');
const PROBE = process.env.PAIR_PROBE === '1';
/* Unpaired window, first 30 s after load (own probe on this branch, PR body):
 * GET /api/session 401 at 0.2, 3.1, 8.1 and 17.1 s (2 s doubling, run on the
 * 1 s tick; the next is at about 34 s) plus one POST /api/window 401 = 5, and
 * one console error each. main b857d697 under the same probe: 110 and 110. */
const WINDOW_MS = 30000;
const MAX_401 = 5;
const MAX_CONSOLE_ERRORS = 5;
const MAX_WINDOW_POSTS = 1;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const liveProcs = [];
const tempDirs = [];
const failures = [];
function noteTemp(dir) { tempDirs.push(dir); return dir; }
function stopAll() {
  for (const p of liveProcs.splice(0)) { try { p.kill('SIGTERM'); } catch (e) { /* gone */ } }
  while (tempDirs.length) { try { fs.rmSync(tempDirs.pop(), { recursive: true, force: true }); } catch (e) { /* best effort */ } }
}
function die(msg) {
  stopAll();
  console.error('pair UI check failed: ' + msg);
  process.exit(1);
}
process.on('SIGINT', () => die('interrupted'));
process.on('SIGTERM', () => die('terminated'));

function freePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.once('error', reject);
    srv.listen(0, '127.0.0.1', () => { const p = srv.address().port; srv.close((e) => (e ? reject(e) : resolve(p))); });
  });
}
function resolveChrome() {
  const cands = [];
  if (process.env.HUSH_CHROME_BIN) cands.push(process.env.HUSH_CHROME_BIN);
  cands.push('google-chrome', 'chromium', 'chromium-browser');
  for (const c of cands) {
    if (c.includes('/') && fs.existsSync(c)) return c;
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
  if (!Number.isFinite(sec) || sec <= 0) die('HUSH_TEST_WAIT_S must be a positive number of seconds');
  return Math.floor(sec * 1000);
}
async function launchChrome(chrome) {
  const deadline = waitMs();
  for (let attempt = 1; attempt <= 2; attempt++) {
    const port = await freePort();
    const userDir = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'pair-chrome-')));
    const env = Object.assign({}, process.env);
    delete env.DBUS_SESSION_BUS_ADDRESS;
    const proc = spawn(chrome, ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
      '--disable-dev-shm-usage', '--disable-software-rasterizer',
      `--remote-debugging-port=${port}`, `--user-data-dir=${userDir}`, 'about:blank'],
    { stdio: ['ignore', 'ignore', 'ignore'], env });
    liveProcs.push(proc);
    let exited = false;
    proc.once('exit', () => { exited = true; });
    const t0 = Date.now();
    while (!exited && Date.now() - t0 < deadline) {
      try {
        const res = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(400) });
        if (res.ok) { const page = (await res.json()).find((t) => t.type === 'page'); if (page) return page.webSocketDebuggerUrl; }
      } catch (e) { /* not up yet */ }
      await sleep(100);
    }
    try { proc.kill('SIGKILL'); } catch (e) { /* gone */ }
  }
  die('Chrome DevTools did not come up');
}

class Cdp {
  constructor(url) { this.url = url; this.id = 0; this.waiters = new Map(); this.listeners = []; }
  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);
      this.ws.onopen = () => resolve();
      this.ws.onerror = (e) => reject(new Error('CDP connect failed: ' + e.message));
      this.ws.onmessage = (ev) => {
        const msg = JSON.parse(ev.data.toString());
        if (msg.id && this.waiters.has(msg.id)) { const w = this.waiters.get(msg.id); this.waiters.delete(msg.id); w(msg); return; }
        if (msg.method) for (const fn of this.listeners) fn(msg.method, msg.params || {});
      };
    });
  }
  on(fn) { this.listeners.push(fn); }
  send(method, params) {
    const id = ++this.id;
    return new Promise((resolve) => { this.waiters.set(id, (m) => resolve(m.result || {})); this.ws.send(JSON.stringify({ id, method, params: params || {} })); });
  }
  async eval(expr) {
    const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error('page eval threw: ' + expr.slice(0, 120));
    return r.result ? r.result.value : null;
  }
  async waitFor(expr, label, timeout) {
    const t0 = Date.now();
    for (;;) {
      let v = null;
      try { v = await this.eval(expr); } catch (e) { /* page busy */ }
      if (v) return v;
      if (Date.now() - t0 > (timeout || 25000)) die('timeout waiting for ' + (label || expr));
      await sleep(100);
    }
  }
}

/* What the gate shows: the pair card, the splash, the hive. */
const VIEW = `(() => { const shown = (sel) => { const e = document.querySelector(sel); if (!e) return false;
    const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 && getComputedStyle(e).visibility === 'visible'; };
  const h1 = document.querySelector('#gate.show h1');
  return { pair: shown('#gate.show #pair-card'), splash: shown('#gate.show #begin'), hive: shown('#hive.show'),
    h1: h1 ? h1.textContent : '', text: shown('#gate.show #pair-card') ? document.querySelector('#gate .card').innerText : '' }; })()`;

async function main() {
  const chrome = resolveChrome();
  const cdp = new Cdp(await launchChrome(chrome));
  await cdp.connect();
  const net401 = []; const posts = []; const consoleErr = []; const reqs = new Map(); let t0 = Date.now();
  cdp.on((m, p) => {
    if (m === 'Network.requestWillBeSent') reqs.set(p.requestId, { method: p.request.method, url: p.request.url });
    else if (m === 'Network.responseReceived') {
      const rq = reqs.get(p.requestId) || { method: '?', url: p.response.url };
      const u = new URL(p.response.url); const at = Date.now() - t0;
      if (p.response.status === 401) net401.push({ at, method: rq.method, path: u.pathname });
      if (rq.method === 'POST' && u.pathname === '/api/window') posts.push({ at, status: p.response.status });
    } else if (m === 'Log.entryAdded' && p.entry.level === 'error') consoleErr.push({ at: Date.now() - t0, text: p.entry.text });
    else if (m === 'Runtime.consoleAPICalled' && p.type === 'error') consoleErr.push({ at: Date.now() - t0, text: 'console.error' });
    else if (m === 'Runtime.exceptionThrown') consoleErr.push({ at: Date.now() - t0, text: 'exception: ' + (p.exceptionDetails.text || '') });
  });
  for (const d of ['Page', 'Network', 'Log', 'Runtime']) await cdp.send(d + '.enable', {});
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  const reset = () => { net401.length = 0; posts.length = 0; consoleErr.length = 0; t0 = Date.now(); };

  const home = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'pair-home-')));
  const cfg = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'pair-cfg-')));
  const port = await freePort();
  const base = `http://127.0.0.1:${port}`;
  const relay = spawn(RELAY, ['--no-open', String(port)], { stdio: ['ignore', 'pipe', 'ignore'],
    env: Object.assign({}, process.env, { HUSH_HOME: home, HUSH_CONFIG_DIR: cfg, HUSH_PASS_HELPER: '/nonexistent-hush-pass-helper' }) });
  liveProcs.push(relay);
  let out = '';
  relay.stdout.on('data', (d) => { out += d; });
  for (let i = 0; i < 200 && !/\?pair=[0-9a-f]{32}/.test(out); i++) await sleep(50);
  const code = (out.match(/\?pair=([0-9a-f]{32})/) || [])[1];
  if (!code) die('relay printed no pair: link on ' + port);
  let up = false;
  for (let i = 0; i < 200 && !up; i++) { try { up = (await fetch(base + '/api/status')).ok; } catch (e) { /* not up */ } if (!up) await sleep(100); }
  if (!up) die('relay did not come up on ' + port);

  const unpairedRun = async (tag) => {
    await cdp.send('Network.clearBrowserCookies', {});
    await cdp.send('Page.navigate', { url: 'about:blank' });
    await sleep(300);
    reset();
    await cdp.send('Page.navigate', { url: base + '/' });
    await sleep(WINDOW_MS);
    const by = {}; for (const x of net401) { const k = x.method + ' ' + x.path; by[k] = (by[k] || 0) + 1; }
    const sess = net401.filter((x) => x.path === '/api/session').map((x) => (x.at / 1000).toFixed(1));
    console.log(`pair ${tag} unpaired ${WINDOW_MS / 1000}s: ${net401.length} responses 401 ${JSON.stringify(by)}, ` +
      `${consoleErr.length} console errors, ${posts.length} POST /api/window; /api/session 401 at s ${JSON.stringify(sess)}`);
    return { n401: net401.length, by, nerr: consoleErr.length, nposts: posts.length, view: await cdp.eval(VIEW) };
  };

  if (PROBE) {
    const r = await unpairedRun('probe(no hive)');
    console.log('pair probe view: ' + JSON.stringify({ pair: r.view.pair, splash: r.view.splash, hive: r.view.hive, h1: r.view.h1 }));
    stopAll();
    return;
  }

  // U0: a fresh hive (no identity) in a window that is not paired: the pair
  // card, not the splash, and the backoff bound.
  const u0 = await unpairedRun('U0 fresh hive');
  if (!u0.view.pair) failures.push('U0 unpaired window shows no pair card: ' + JSON.stringify(u0.view));
  if (u0.view.splash) failures.push('U0 unpaired window shows the first-run splash (Begin)');
  if (u0.view.h1 !== 'Pair this window') failures.push('U0 gate title is ' + JSON.stringify(u0.view.h1) + ', want "Pair this window"');
  if (!u0.view.text.includes('hush-relay --quit ' + port)) failures.push('U0 pair card does not name this relay port in hush-relay --quit ' + port);
  if (u0.n401 > MAX_401) failures.push(`U0 ${u0.n401} responses 401 in ${WINDOW_MS / 1000}s, want <= ${MAX_401} (backoff)`);
  if (u0.nerr > MAX_CONSOLE_ERRORS) failures.push(`U0 ${u0.nerr} console errors in ${WINDOW_MS / 1000}s, want <= ${MAX_CONSOLE_ERRORS}`);
  if (u0.nposts > MAX_WINDOW_POSTS) failures.push(`U0 ${u0.nposts} POST /api/window, want <= ${MAX_WINDOW_POSTS} (no retry on 401)`);
  for (const k of Object.keys(u0.by)) if (!['GET /api/session', 'POST /api/window'].includes(k)) failures.push('U0 unpaired window still asks ' + k);
  console.log('pair U0 text: ' + JSON.stringify(u0.view.text));
  if (!u0.view.text.includes('quit Hush and start it again from your Hush app or shortcut')) failures.push('U0 pair card has no plain-language step (quit Hush and start it again from your Hush app or shortcut)');

  // L0 (#368 r2): Profile (c) and Settings (Kit) over the pair card say
  // "Pair this window first", show no false state, and their actions are off.
  const lock = async (tag, open, ids, drawer, hint) => {
    const seen = new Set(reqs.keys());
    await cdp.eval(open);
    await sleep(800);
    const r = await cdp.eval(`(() => { const d = document.getElementById(${JSON.stringify(drawer)}); const h = document.getElementById(${JSON.stringify(hint)});
      const on = ${JSON.stringify(ids)}.filter((id) => { const e = document.getElementById(id); return !e || !e.disabled; });
      return { shown: !!d && d.classList.contains('show'), hint: !!h && !h.hidden && h.getBoundingClientRect().height > 0 ? h.textContent : '', on, text: d ? d.innerText : '' }; })()`);
    console.log(`pair L0 ${tag}: ` + JSON.stringify({ shown: r.shown, hint: r.hint, live: r.on }));
    if (!r.shown) failures.push(`L0 ${tag} did not open over the pair card`);
    if (!r.hint.startsWith('Pair this window first')) failures.push(`L0 ${tag} has no visible "Pair this window first" hint`);
    if (r.on.length) failures.push(`L0 ${tag} actions still enabled while unpaired: ` + r.on.join(', '));
    for (const bad of ['no identity yet', 'turn api unreachable']) if (r.text.includes(bad)) failures.push(`L0 ${tag} shows "${bad}" while unpaired`);
    const turnAsks = [...reqs].filter(([id, q]) => !seen.has(id) && new URL(q.url).pathname === '/api/turn').length;
    if (turnAsks) failures.push(`L0 ${tag} asked GET /api/turn ${turnAsks}x while unpaired, want 0 (Settings skips it)`);
    if (tag === 'Profile') { const lo = await cdp.eval(`(document.getElementById('profile-loadout-text') || {}).textContent`);
      if (lo !== 'Pair this window first.') failures.push(`L0 Profile loadout line is ${JSON.stringify(lo)}, want "Pair this window first."`); }
    await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
    await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
    await sleep(300);
    await cdp.eval(`document.getElementById(${JSON.stringify(drawer)}).classList.remove('show')`);
  };
  await lock('Profile', 'openCharacterSheet()', ['profile-save', 'profile-logout', 'profile-copy', 'prof-first', 'prof-last', 'prof-email', 'prof-org'], 'profile', 'profile-pair-hint');
  await lock('Settings', 'openSettings()', ['turn-on', 'turn-daemon', 'turn-host', 'vibe-public', 'vibe-rotate', 'dev-log', 'approval-auto', 'approval-every'], 'settings', 'settings-pair-hint');

  // K0 (#368 Ops follow-up): the Kit menu's hive actions (Invite, Add Channel,
  // Configure Provider, New Robot, New Project) and the Boards drawer's New
  // channel are off while unpaired, the Kit says "Pair this window first", key 3
  // opens no New channel drawer, and a disabled switch, profile field and radio
  // dot are dimmed (opacity <= 0.6) while the pair-locked host field and Kit
  // actions keep their ink with a dashed edge (K2 measures it), in all 8 themes.
  const KIT_LOCKED = ['invite-human', 'add-chan', 'nav-new-channel', 'providers-btn', 'raise-agent', 'add-proj', 'new-proj', 'inv-raise'];
  await cdp.eval("document.getElementById('rail-toggle').click()"); await sleep(400);
  const k0 = await cdp.eval(`(() => { const h = document.getElementById('kit-pair-hint');
    return { live: ${JSON.stringify(KIT_LOCKED)}.filter((id) => { const e = document.getElementById(id); return !e || !e.disabled; }),
      hint: h && !h.hidden && h.getBoundingClientRect().height > 0 ? h.textContent : '' }; })()`);
  console.log('pair K0 Kit: ' + JSON.stringify(k0));
  if (k0.live.length) failures.push('K0 Kit/Boards actions still enabled while unpaired: ' + k0.live.join(', '));
  if (!k0.hint.startsWith('Pair this window first')) failures.push('K0 Kit menu has no visible "Pair this window first" hint');
  await cdp.eval("document.getElementById('rail-toggle').click()"); await sleep(200);
  await cdp.eval(`(() => { const ae = document.activeElement; if (ae && ae.blur) ae.blur(); })()`);
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: '3', code: 'Digit3', text: '3', windowsVirtualKeyCode: 51 });
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: '3', code: 'Digit3', windowsVirtualKeyCode: 51 });
  await sleep(400);
  const nc = await cdp.eval(`document.getElementById('new-chan-drawer').classList.contains('show')`);
  if (nc) { failures.push('K0 key 3 opened New channel while unpaired'); await cdp.eval(`closeLayer('new-chan-drawer')`); }
  const dim = await cdp.eval(`(() => { const out = []; const keep = document.documentElement.getAttribute('data-theme');
    for (const t of ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office']) {
      document.documentElement.setAttribute('data-theme', t);
      const sl = parseFloat(getComputedStyle(document.querySelector('#turn-on + .slider')).opacity);
      const fi = parseFloat(getComputedStyle(document.getElementById('prof-first')).opacity);
      const dot = parseFloat(getComputedStyle(document.getElementById('approval-auto')).opacity);
      const hb = getComputedStyle(document.getElementById('turn-host')).borderTopStyle;
      const kb = getComputedStyle(document.getElementById('invite-human')).borderTopStyle;
      out.push({ t, sl, fi, dot, hb, kb }); }
    document.documentElement.setAttribute('data-theme', keep); return out; })()`);
  console.log('pair K0 disabled dim: ' + JSON.stringify(dim));
  for (const d of dim) if (!(d.sl <= 0.6) || !(d.fi >= 0.6 && d.fi <= 0.6) || !(d.dot <= 0.6)) failures.push(`K0 ${d.t} disabled switch/profile field/radio dot: want slider <= 0.6, prof-first exactly 0.6 (K2b: field ink >= 3:1), radio dot <= 0.6; got slider ${d.sl}, prof-first ${d.fi}, radio dot ${d.dot}`);
  for (const d of dim) if (d.hb !== 'dashed' || d.kb !== 'dashed') failures.push(`K0 ${d.t} pair-locked host field / Kit Invite edge not dashed: ${d.hb}, ${d.kb}`);
  // K0e (#368 Ops pre r4 f2): every one of the 11 pair-locked controls (a radio
  // is its label) has a computed dashed top border of width > 0 in all 8 themes,
  // so the robot-turn labels do not look like the live Theme pills.
  const edge = await cdp.eval(`(() => { const out = []; const keep = document.documentElement.getAttribute('data-theme');
    for (const t of ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office']) {
      document.documentElement.setAttribute('data-theme', t);
      for (const id of ['turn-host', 'approval-auto', 'approval-every', ...${JSON.stringify(KIT_LOCKED)}]) {
        const c = document.getElementById(id); const el = c.type === 'radio' ? c.closest('label') : c; const cs = getComputedStyle(el);
        out.push({ t, id, s: cs.borderTopStyle, w: parseFloat(cs.borderTopWidth), lk: c.classList.contains('pair-locked') && c.disabled }); } }
    document.documentElement.setAttribute('data-theme', keep); return out; })()`);
  console.log('pair K0e locked edges: ' + JSON.stringify(edge));
  for (const e of edge) if (e.s !== 'dashed' || !(e.w > 0)) failures.push(`K0e ${e.t} pair-locked #${e.id} edge not dashed with width > 0: ${e.s} ${e.w}px`);
  // K2 (#368 Ops pre f2): the unpaired-locked text this branch dims (Settings'
  // host field, the robot-turn radio labels, the Kit's locked actions) still
  // reads >= 3:1 against what is behind it, in all 8 themes at 1440 and 390.
  // Opacity is composited up the tree; a field's empty value reads its placeholder.
  const K2_IDS = ['turn-host', 'approval-auto', 'approval-every', ...KIT_LOCKED];
  const k2 = [];
  for (const w of [1440, 390]) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: 900, deviceScaleFactor: 1, mobile: w < 600 });
    await sleep(200);
    k2.push(...await cdp.eval(`(() => { const out = []; const keep = document.documentElement.getAttribute('data-theme');
      const cx = document.createElement('canvas').getContext('2d', { willReadFrequently: true });
      const rgba = (s) => { cx.clearRect(0, 0, 1, 1); cx.fillStyle = 'rgba(0, 0, 0, 0)'; cx.fillStyle = s || 'transparent'; cx.fillRect(0, 0, 1, 1); const d = cx.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255]; };
      const over = (t, u, a) => [0, 1, 2].map((i) => t[i] * a + u[i] * (1 - a));
      const lin = (c) => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
      const lum = (c) => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
      const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
      for (const t of ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office']) {
        document.documentElement.setAttribute('data-theme', t);
        for (const id of ${JSON.stringify(K2_IDS)}) {
          const ctl = document.getElementById(id); const el = ctl.type === 'radio' ? ctl.closest('label') : ctl;
          const chain = []; for (let n = el; n && n.nodeType === 1; n = n.parentElement) chain.unshift(n);
          let bg = [255, 255, 255], op = 1, fg = null;
          for (const n of chain) { const cs = getComputedStyle(n); op *= parseFloat(cs.opacity); const b = rgba(cs.backgroundColor); if (b[3] > 0) bg = over(b, bg, b[3] * op); }
          const cs = getComputedStyle(el); const c = rgba(el.tagName === 'INPUT' && !el.value ? getComputedStyle(el, '::placeholder').color : cs.color);
          fg = over(c, bg, c[3] * op);
          out.push({ w: ${w}, t, id, r: Math.round(ratio(fg, bg) * 100) / 100 }); } }
      document.documentElement.setAttribute('data-theme', keep); return out; })()`));
  }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  const k2min = {}; for (const m of k2) { const k = m.t + '@' + m.w; if (!k2min[k] || m.r < k2min[k].r) k2min[k] = m; }
  console.log('pair K2 unpaired locked text min: ' + JSON.stringify(Object.values(k2min)));
  console.log('pair K2 all: ' + JSON.stringify(k2));
  for (const m of k2) if (m.r < 3) failures.push(`K2 unpaired locked text below 3:1: #${m.id} ${m.t}@${m.w} ${m.r}:1, want >= 3:1`);
  // K2b (#368 Gauge pre r4 P2-2): the dimmed Profile fields (prof-first/last/
  // email/org, opacity 0.6 while unpaired) keep their ink >= 3:1 in all
  // 8 themes at 1440 and 390, measuring the field's text colour (its placeholder
  // when it has one and is empty). The field is composited as one group: ink
  // over its own background, then that group at its opacity over what is behind.
  const k2b = [];
  for (const w of [1440, 390]) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: 900, deviceScaleFactor: 1, mobile: w < 600 });
    await sleep(200);
    k2b.push(...await cdp.eval(`(() => { const out = []; const keep = document.documentElement.getAttribute('data-theme');
      const cx = document.createElement('canvas').getContext('2d', { willReadFrequently: true });
      const rgba = (s) => { cx.clearRect(0, 0, 1, 1); cx.fillStyle = 'rgba(0, 0, 0, 0)'; cx.fillStyle = s || 'transparent'; cx.fillRect(0, 0, 1, 1); const d = cx.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255]; };
      const over = (t, u, a) => [0, 1, 2].map((i) => t[i] * a + u[i] * (1 - a));
      const lin = (c) => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
      const lum = (c) => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
      const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
      for (const t of ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office']) {
        document.documentElement.setAttribute('data-theme', t);
        for (const id of ['prof-first', 'prof-last', 'prof-email', 'prof-org']) {
          const el = document.getElementById(id); const chain = []; for (let n = el.parentElement; n && n.nodeType === 1; n = n.parentElement) chain.unshift(n);
          let back = [255, 255, 255], pop = 1;
          for (const n of chain) { const cs = getComputedStyle(n); pop *= parseFloat(cs.opacity); const b = rgba(cs.backgroundColor); if (b[3] > 0) back = over(b, back, b[3] * pop); }
          const cs = getComputedStyle(el); const op = parseFloat(cs.opacity) * pop; const ob = rgba(cs.backgroundColor);
          const ph = !el.value && el.placeholder; const c = rgba(ph ? getComputedStyle(el, '::placeholder').color : cs.color);
          const gA = ob[3] + c[3] * (1 - ob[3]); const gTxt = gA > 0 ? [0, 1, 2].map((i) => (c[i] * c[3] + ob[i] * ob[3] * (1 - c[3])) / gA) : c;
          const fg = over(gTxt, back, gA * op); const bg = over(ob, back, ob[3] * op);
          out.push({ w: ${w}, t, id, ink: ph ? 'placeholder' : 'text', op: Math.round(op * 100) / 100, r: Math.round(ratio(fg, bg) * 100) / 100 }); } }
      document.documentElement.setAttribute('data-theme', keep); return out; })()`));
  }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  console.log('pair K2b profile fields: ' + JSON.stringify(k2b));
  for (const m of k2b) if (m.r < 3) failures.push(`K2b unpaired Profile field ${m.ink} below 3:1: #${m.id} ${m.t}@${m.w} ${m.r}:1 at opacity ${m.op}, want >= 3:1`);
  // K1 (#368 follow-up): every other route into these drawers stays shut while
  // unpaired: the quick bar's New robot slot, Inventory's Raise, a roster
  // edit (openAgentDrawer), Configure Providers, Invite, New channel, New project.
  const k1 = await cdp.eval(`(async () => { const shown = (id) => { const e = document.getElementById(id); return !!e && e.classList.contains('show'); };
    const keep = quickSlots[3]; quickSlots[3] = 'new-robot'; runQuickSlot(3); quickSlots[3] = keep;
    const r = { quickRobot: shown('agent-drawer') };
    openAgentDrawer({ name: 'x', slug: 'x' }); r.rosterEdit = shown('agent-drawer');
    await openProvidersHub(); r.providers = shown('providers-hub');
    const fire = (id) => { const e = document.getElementById(id); e.disabled = false; e.click(); e.disabled = true; };
    fire('invite-human'); r.invite = shown('member-drawer');
    fire('add-chan'); r.newChan = shown('new-chan-drawer');
    fire('inv-raise'); r.invRaise = shown('agent-drawer');
    return r; })()`);
  { const seenP = new Set(reqs.keys());
    await cdp.eval(`(() => { const i = document.getElementById('new-proj'), b = document.getElementById('add-proj'); i.disabled = false; b.disabled = false; i.value = 'zz'; b.click(); i.value = ''; i.disabled = true; b.disabled = true; })()`);
    await sleep(500);
    k1.newProject = [...reqs].some(([id, q]) => !seenP.has(id) && new URL(q.url).pathname === '/api/project'); }
  console.log('pair K1 other routes: ' + JSON.stringify(k1));
  for (const [k, v] of Object.entries(k1)) if (v) failures.push(`K1 ${k} opened its drawer while unpaired`);
  await cdp.eval(`for (const id of ['agent-drawer', 'providers-hub', 'member-drawer']) document.getElementById(id).classList.remove('show'); if (document.getElementById('new-chan-drawer').classList.contains('show')) closeLayer('new-chan-drawer');`);

  // R0: the cookie arrives on a first run (no identity yet): Check again
  // leaves the card for the first-run splash, with no reload.
  const token = fs.readFileSync(path.join(home, 'session.token'), 'utf8').trim();
  const click = async (sel) => { await cdp.waitFor(`!!document.querySelector('${sel}')`, sel); await cdp.eval(`document.querySelector('${sel}').click()`); };
  await cdp.eval('window.__noReload = true');
  await cdp.send('Network.setCookie', { name: 'hush_session', value: token, url: base + '/', httpOnly: true, sameSite: 'Strict' });
  await click('#pair-check');
  const r0 = await cdp.waitFor(`(() => { const v = ${VIEW}; return (v.splash && !v.pair && window.__noReload === true) ? v : null; })()`, 'first-run splash after pairing without reload', 10000);
  console.log('pair R0 first run recovered to the splash, no reload: ' + JSON.stringify({ splash: r0.splash, pair: r0.pair }));

  // F1: the same fresh hive through the one-time pair link: the first-run splash.
  reset();
  await cdp.send('Page.navigate', { url: base + '/?pair=' + code });
  await cdp.waitFor(`(${VIEW}).splash`, 'first-run splash after the pair link');
  const f1 = await cdp.eval(VIEW);
  if (f1.pair) failures.push('F1 paired first run shows the pair card');
  console.log('pair F1 paired first run: ' + JSON.stringify({ splash: f1.splash, pair: f1.pair }));

  // P1: set the hive up and poll paired: no 401, no console error.
  await click('#begin'); await click('#create-id'); await click('#ack-key');
  await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe step');
  await cdp.eval(`document.querySelector('#vibe-name').value = 'PAIRHIVE'`);
  await click('#do-vibe'); await click('#meet-payne');
  await cdp.waitFor(`(${VIEW}).hive`, 'hive');
  reset();
  await sleep(5000);
  console.log(`pair P1 paired hive 5s: ${net401.length} responses 401, ${consoleErr.length} console errors`);
  if (net401.length) failures.push('P1 paired hive got 401: ' + JSON.stringify(net401.slice(0, 3)));
  if (consoleErr.length) failures.push('P1 paired hive console errors: ' + JSON.stringify(consoleErr.slice(0, 3)));

  // U1: the Ops case: the hive has an identity, this window has no cookie.
  const u1 = await unpairedRun('U1 hive with identity');
  if (!u1.view.pair || u1.view.splash || u1.view.hive) failures.push('U1 unpaired window over a set-up hive: ' + JSON.stringify({ pair: u1.view.pair, splash: u1.view.splash, hive: u1.view.hive }));
  if (u1.n401 > MAX_401) failures.push(`U1 ${u1.n401} responses 401 in ${WINDOW_MS / 1000}s, want <= ${MAX_401}`);
  if (u1.nerr > MAX_CONSOLE_ERRORS) failures.push(`U1 ${u1.nerr} console errors, want <= ${MAX_CONSOLE_ERRORS}`);
  if (u1.nposts > MAX_WINDOW_POSTS) failures.push(`U1 ${u1.nposts} POST /api/window, want <= ${MAX_WINDOW_POSTS}`);

  // U2 (Gauge #368): the 30 s cap and the focus / visibility re-check, through the real 1 s interval.
  for (let i = 0; i < 6; i++) { await cdp.eval('pairNextAt = 0'); await sleep(1300); }
  const cap = await cdp.eval('pairDelayMs');
  if (cap !== 30000) failures.push(`U2 backoff after 6 more retries is ${cap} ms, want the 30000 ms cap`);
  for (const [tag, js] of [['focus', "window.dispatchEvent(new Event('focus'))"], ['visible', "document.dispatchEvent(new Event('visibilitychange'))"]]) {
    await sleep(1500); const n = net401.length; await cdp.eval(js); await sleep(1500);
    if (net401.length <= n) failures.push(`U2 ${tag} did not re-check pairing within 1.5 s`);
  }
  { const n = net401.length; const end = Date.now() + 3000; while (Date.now() < end) { await cdp.eval("window.dispatchEvent(new Event('focus'))"); await sleep(100); }
    await sleep(500); const k = net401.length - n;
    if (k > 5) failures.push(`U2 30 focus events in 3 s made ${k} /api/session requests, want <= 5 (re-checks ride the 1 s tick)`); }
  // U3 (Gauge #368): a non-401 error from /api/session keeps the pair card (no splash, no events).
  await cdp.send('Fetch.enable', { patterns: [{ urlPattern: '*/api/session*', requestStage: 'Request' }] });
  const paused = async (m, p) => { if (m === 'Fetch.requestPaused') await cdp.send('Fetch.fulfillRequest', { requestId: p.requestId, responseCode: 429, responseHeaders: [{ name: 'Content-Type', value: 'application/json' }], body: Buffer.from('{"ok":false}').toString('base64') }); };
  cdp.on(paused);
  await cdp.eval("document.querySelector('#pair-check').click()"); await sleep(2500);
  const u3 = await cdp.eval(VIEW);
  await cdp.send('Fetch.disable', {}); cdp.listeners.splice(cdp.listeners.indexOf(paused), 1);
  if (!u3.pair || u3.splash) die('U3 a 429 from /api/session left the pair card: ' + JSON.stringify({ pair: u3.pair, splash: u3.splash }));
  console.log(`pair U2/U3 cap ${cap} ms, focus/visible re-check, 429 keeps card ${u3.pair && !u3.splash}`);

  // R1: the cookie arrives (as when the pair link opens in another tab of this
  // browser); Check again leaves the card for the hive with no reload.
  await cdp.eval('window.__noReload = true');
  await cdp.send('Network.setCookie', { name: 'hush_session', value: token, url: base + '/', httpOnly: true, sameSite: 'Strict' });
  reset();
  await click('#pair-check');
  const t = Date.now();
  await cdp.waitFor(`(${VIEW}).hive && window.__noReload === true`, 'hive after pairing without reload', 10000);
  const r1ms = Date.now() - t;
  if (r1ms > 2000) failures.push(`R1 Check again took ${r1ms} ms to leave the card, want <= 2000 (it asks at once)`);
  const posted = posts.filter((x) => x.status === 200).length;
  console.log(`pair R1 recovered to the hive in ${Date.now() - t} ms, no reload, POST /api/window 200 x${posted}`);
  if (!posted) failures.push('R1 window was not prepared after pairing (no POST /api/window 200)');
  const r1 = await cdp.eval(VIEW);
  if (r1.pair) failures.push('R1 pair card still shows after pairing');
  const l1 = await cdp.eval(`(() => ({ off: ['profile-save', 'profile-logout', 'profile-copy', 'turn-on', 'vibe-rotate', ...${JSON.stringify(KIT_LOCKED)}].filter((id) => document.getElementById(id).disabled),
    hint: ['profile-pair-hint', 'settings-pair-hint', 'kit-pair-hint'].filter((id) => !document.getElementById(id).hidden),
    dim: [parseFloat(getComputedStyle(document.querySelector('#turn-on + .slider')).opacity), parseFloat(getComputedStyle(document.getElementById('turn-host')).opacity), parseFloat(getComputedStyle(document.getElementById('approval-auto').closest('label')).opacity)],
    ro: ['agent-prompt', 'agent-name'].map((id) => { const p = document.getElementById(id); const d = p.disabled, r = p.readOnly; p.disabled = true; p.readOnly = true; const o = parseFloat(getComputedStyle(p).opacity); p.disabled = d; p.readOnly = r; return o; }) }))()`);
  ['agent-prompt', 'agent-name'].forEach((id, i) => { if (l1.ro[i] < 1) failures.push('L1 #' + id + ' set disabled+readOnly (as for Major standing orders) is dimmed: opacity ' + l1.ro[i] + ', want 1'); });
  if (l1.dim.some((o) => o < 1)) failures.push('L1 paired switch/field still dimmed: ' + JSON.stringify(l1.dim));
  console.log('pair L1 paired: ' + JSON.stringify(l1));
  if (l1.off.length || l1.hint.length) failures.push('L1 paired window still locks Profile/Settings: ' + JSON.stringify(l1));
  // R2 (Gauge #368): pairing resets the backoff; losing the cookie starts again at 2 s.
  await cdp.send('Network.clearBrowserCookies', {}); reset(); await sleep(4500);
  const r2n = net401.filter((x) => x.path === '/api/session').length;
  if (r2n < 2) failures.push(`R2 after pairing then losing the cookie, ${r2n} /api/session 401 in 4.5 s, want >= 2 (backoff reset to 2 s)`);

  stopAll();
  if (failures.length) { for (const f of failures) console.error('pair UI check failed: ' + f); process.exit(1); }
  console.log(`pair UI check passed: unpaired U0 ${u0.n401} / U1 ${u1.n401} responses 401 and ${u0.nerr} / ${u1.nerr} console errors in ${WINDOW_MS / 1000}s ` +
    `(bound ${MAX_401}), POST /api/window ${u0.nposts} / ${u1.nposts}, pair card not splash, first run splash, paired hive 0 401, recovery without reload to the splash (first run) and the hive`);
}
main().catch((e) => die(e.stack || String(e)));
