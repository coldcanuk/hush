// check_ch0_ui.cjs: Ops CH0_HARD pin for M1 "thread-UI removal"
// (PLAN_ROUTER_WORKER_V2 r1 §5.2, ops-router-ux-accept CH0_HARD). The human
// UI is channels only: no thread UI in any state, theme or viewport.
//
// Drives headless system Chrome over CDP (Node standard library only) on a
// fresh relay, walks the real gate, seeds #welcome with notes, flat replies,
// a loop ask and an approval ask, then for every state below x 8 themes x
// 1440x900 / 390x844 scans the rendered page:
//   idle (empty channel), log (notes + replies + Yes/No stamps), replying
//   (a live thinking job via a wrapped /api/status), decision (a real click on
//   a Yes stamp), error (a refused /api/event send with the draft kept),
//   mention (the @ picker open), boards (Individuals list open), settings,
//   robot (Major's settings and loadout drawer, skills armory).
// A hit is any of, on a visible element (checkVisibility + a non-empty box):
//   #thread-pane, .thread-btn, [id^="thread-"], #milestone-form, a class
//   containing "thread"; or visible text, CSS ::before/::after content,
//   title / aria-label / aria-description / placeholder / alt / button value
//   matching /\bthreads?\b/i, "reply in thread" or "Open 1:1"; or
//   document.title matching. Separately the whole DOM (hidden included) must
//   hold none of those selectors or strings, and the page script must define
//   no openThreadPane / startRobotConversation. Any hit fails (exit 1).
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// HUSH_TEST_WAIT_S (DevTools ready deadline, default 30), CH0_ART (dir:
// writes probe/CH0.json and shots/<state>-<theme>-<vp>.png for field-office
// and dark there). Every Chrome profile and relay scratch dir this run makes
// is recorded and removed on the way out; only processes it spawned are
// signalled.
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

const RELAY = process.env.HUSH_RELAY_BIN || path.join(__dirname, '..', 'hush-relay');
const ART = process.env.CH0_ART || '';
const THEMES = ['field-office', 'dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas'];
const VIEWS = [{ vp: '1440', w: 1440, h: 900 }, { vp: '390', w: 390, h: 844 }];
const SHOT_THEMES = ['field-office', 'dark'];

const STUB_REPLY = 'Stub runtime reply from Ace.';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const liveProcs = [];
const tempDirs = [];
function noteTemp(dir) {
  tempDirs.push(dir);
  return dir;
}
function rmTemps() {
  while (tempDirs.length) {
    const dir = tempDirs.pop();
    try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* best effort */ }
  }
}
/* Stops one child this run spawned (by its own handle, never by name):
 * SIGTERM, then SIGKILL after 5 s; resolves once it has exited. */
function stopProc(p) {
  return new Promise((resolve) => {
    if (p.exitCode !== null || p.signalCode !== null) { resolve(); return; }
    const t = setTimeout(() => { try { p.kill('SIGKILL'); } catch (e) { /* gone */ } }, 5000);
    p.once('exit', () => { clearTimeout(t); resolve(); });
    try { p.kill('SIGTERM'); } catch (e) { clearTimeout(t); resolve(); }
  });
}
/* Children first (Chrome must stop writing its profile), then every
 * recorded temp dir; a dir that survives the first pass is removed again. */
async function stopAll() {
  await Promise.all(liveProcs.splice(0).map(stopProc));
  const dirs = tempDirs.slice();
  rmTemps();
  for (const d of dirs) if (fs.existsSync(d)) { await sleep(300); fs.rmSync(d, { recursive: true, force: true }); }
  const left = dirs.filter((d) => fs.existsSync(d));
  if (left.length) console.error('CH0 UI check: temp dirs left behind: ' + left.join(' '));
  return left.length === 0;
}
class StopRun extends Error {}
let failing = false;
/* Reports, tears down (async) and exits 1; throws so the caller stops. */
function fail(msg) {
  console.error('CH0 UI check failed: ' + msg);
  if (!failing) {
    failing = true;
    stopAll().finally(() => process.exit(1));
  }
  throw new StopRun(msg);
}
const onSignal = (name) => () => { try { fail(name); } catch (e) { /* teardown running */ } };
process.on('SIGINT', onSignal('interrupted'));
process.on('SIGTERM', onSignal('terminated'));

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
  const cands = [];
  if (process.env.HUSH_CHROME_BIN)
    cands.push(process.env.HUSH_CHROME_BIN);
  cands.push('google-chrome', 'chromium', 'chromium-browser');
  for (const c of cands) {
    if (c.includes('/') && fs.existsSync(c))
      return c;
    for (const d of (process.env.PATH || '').split(':')) {
      const p = path.join(d, c);
      try {
        fs.accessSync(p, fs.constants.X_OK);
        return p;
      } catch (e) { /* next */ }
    }
  }
  fail('no Chrome on PATH (need google-chrome or chromium) and HUSH_CHROME_BIN unset');
}

function waitMs() {
  const raw = process.env.HUSH_TEST_WAIT_S;
  const sec = raw === undefined || raw === '' ? 30 : Number(raw);
  if (!Number.isFinite(sec) || sec <= 0)
    fail('HUSH_TEST_WAIT_S must be a positive number of seconds');
  return Math.floor(sec * 1000);
}

/* Headless Chrome on a free port; waits for /json/list. One relaunch on a
 * startup race (deadline hit without an early exit). */
async function launchChrome(chrome) {
  const deadline = waitMs();
  for (let attempt = 1; attempt <= 2; attempt++) {
    const port = await freePort();
    const userDir = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'ch0-chrome-')));
    const env = Object.assign({}, process.env);
    delete env.DBUS_SESSION_BUS_ADDRESS;
    const proc = spawn(chrome,
      ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
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
        if (res.ok) {
          const targets = await res.json();
          const page = targets.find((t) => t.type === 'page');
          if (page)
            return page.webSocketDebuggerUrl;
        }
      } catch (e) { /* not listening yet */ }
      await sleep(100);
    }
    try { proc.kill('SIGTERM'); } catch (e) { /* gone */ }
    if (exited)
      break;
  }
  fail('Chrome DevTools never answered');
}

class Cdp {
  constructor(url) {
    this.url = url;
    this.id = 0;
    this.waiters = new Map();
  }
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
        } else if (msg.method === 'Page.javascriptDialogOpening') {
          this.send('Page.handleJavaScriptDialog', { accept: true }).catch(() => {});
        } else if (msg.method && this.onEvent) {
          this.onEvent(msg);
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
  async eval(expr) {
    return this.evaluate(expr, false);
  }
  async evalAsync(expr) {
    return this.evaluate(expr, true);
  }
  async evaluate(expr, awaitPromise) {
    const r = await this.send('Runtime.evaluate', {
      expression: expr, returnByValue: true, awaitPromise: !!awaitPromise
    });
    if (r.exceptionDetails)
      throw new Error('page eval threw: ' + expr.slice(0, 120));
    return r.result ? r.result.value : null;
  }
  async waitFor(expr, label, timeout) {
    const t0 = Date.now();
    for (;;) {
      let v = null;
      try { v = await this.eval(expr); } catch (e) { /* page busy */ }
      if (v)
        return v;
      if (Date.now() - t0 > (timeout || 25000))
        fail('timeout waiting for ' + (label || expr));
      await sleep(150);
    }
  }
  async tryWait(expr, timeout) {
    const t0 = Date.now();
    for (;;) {
      try {
        if (await this.eval(expr))
          return true;
      } catch (e) { /* page busy */ }
      if (Date.now() - t0 > (timeout || 2000))
        return false;
      await sleep(100);
    }
  }
}


/* Page-side scanner. Returns the hit list for the current paint. */
const SCAN = `((all) => {
  const RX = /\\bthreads?\\b|reply in thread|open 1:1/i;
  const SEL = '#thread-pane, .thread-btn, [id^="thread-"], #milestone-form, [class*="thread"]';
  const ATTRS = ['title', 'aria-label', 'aria-description', 'aria-roledescription', 'placeholder', 'alt'];
  const shown = (el) => {
    if (!el || !el.isConnected) return false;
    if (el.checkVisibility && !el.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true })) return false;
    const r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  };
  const ok = (el) => all || shown(el);
  const where = (el) => {
    const bits = [];
    for (let e = el; e && e.nodeType === 1 && bits.length < 4; e = e.parentElement)
      bits.unshift(e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') +
        (typeof e.className === 'string' && e.className ? '.' + e.className.trim().split(/\\s+/).join('.') : ''));
    return bits.join(' > ');
  };
  const hits = [];
  for (const el of document.querySelectorAll(SEL))
    if (ok(el)) hits.push({ kind: 'selector', at: where(el) });
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let n = walker.nextNode(); n; n = walker.nextNode()) {
    const p = n.parentElement;
    if (!p || p.closest('script, style')) continue;
    if (RX.test(n.textContent) && ok(p)) hits.push({ kind: 'text', at: where(p), text: n.textContent.trim().slice(0, 120) });
  }
  for (const el of document.body.querySelectorAll('*')) {
    if (el.closest('script, style') || !ok(el)) continue;
    for (const a of ATTRS) {
      const v = el.getAttribute(a);
      if (v && RX.test(v)) hits.push({ kind: 'attr', at: where(el), attr: a, text: v.slice(0, 120) });
    }
    if ((el.tagName === 'INPUT' && /^(button|submit|reset)$/i.test(el.type)) && RX.test(el.value || ''))
      hits.push({ kind: 'attr', at: where(el), attr: 'value', text: el.value.slice(0, 120) });
    if (!all) for (const pe of ['::before', '::after']) {
      const c = getComputedStyle(el, pe).content;
      if (c && c !== 'none' && c !== 'normal' && RX.test(c)) hits.push({ kind: 'css-content', at: where(el) + pe, text: c.slice(0, 120) });
    }
  }
  if (RX.test(document.title)) hits.push({ kind: 'title', text: document.title });
  return hits;
})`;

async function main() {
  const chrome = resolveChrome();
  const cdp = new Cdp(await launchChrome(chrome));
  await cdp.connect();
  await cdp.send('Page.enable', {});
  await cdp.send('Runtime.enable', {});
  const pageErrors = [];
  cdp.onEvent = (msg) => {
    if (msg.method === 'Runtime.exceptionThrown')
      pageErrors.push(String((msg.params.exceptionDetails.exception || {}).description || msg.params.exceptionDetails.text).slice(0, 300));
  };

  const home = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'ch0-home-')));
  const cfg = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'ch0-cfg-')));
  // G20 pin: a `codex` stub first on the relay's PATH (pattern from
  // check_approve_ui.cjs U6) so a codex robot runs one turn and replies.
  // HOME is the throwaway dir; the stub bin lives outside it (agent jail).
  const hhome = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'ch0-hhome-')));
  const stubBin = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'ch0-bin-')));
  fs.writeFileSync(path.join(stubBin, 'codex'), '#!/bin/sh\nprintf "%s\\n" "' + STUB_REPLY + '"\n', { mode: 0o755 });
  fs.mkdirSync(path.join(hhome, '.codex'), { recursive: true });
  fs.writeFileSync(path.join(hhome, '.codex', 'auth.json'), '{"ok":true}\n');
  const port = await freePort();
  const relay = spawn(RELAY, ['--no-open', String(port)], {
    env: Object.assign({}, process.env, {
      HOME: hhome, HUSH_AUTO_UPDATE: '0',
      PATH: stubBin + path.delimiter + (process.env.PATH || ''),
      HUSH_HOME: home, HUSH_CONFIG_DIR: cfg, HUSH_PASS_HELPER: '/nonexistent-hush-pass-helper',
    }),
    stdio: ['ignore', 'pipe', 'ignore'],
  });
  liveProcs.push(relay);
  let relayOut = '';
  relay.stdout.on('data', (d) => { relayOut += d; });
  let up = false;
  for (let i = 0; i < 200 && !up; i++) {
    try { up = (await fetch(`http://127.0.0.1:${port}/`)).ok; } catch (e) { /* not up */ }
    if (!up) await sleep(100);
  }
  for (let i = 0; i < 100 && !/\?pair=[0-9a-f]{32}/.test(relayOut); i++) await sleep(50);
  const pairCode = (relayOut.match(/\?pair=([0-9a-f]{32})/) || [])[1];
  if (!up || !pairCode)
    fail(`relay did not come up on ${port} with a pairing link`);

  const view = async (v) => {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: v.w, height: v.h, deviceScaleFactor: 1, mobile: false });
    await sleep(120);
  };
  const jsClick = async (sel) => {
    await cdp.waitFor(`!!document.querySelector('${sel}')`, sel);
    await cdp.eval(`document.querySelector('${sel}').click()`);
  };

  // Real gate once: create a key, name the hive, meet Major.
  await view(VIEWS[0]);
  // #336: the one-time pairing link sets hush_session and the hush_nonce
  // that owner-stamped notes send as X-Hush-Nonce.
  await cdp.send('Page.navigate', { url: `http://127.0.0.1:${port}/?pair=${pairCode}` });
  await cdp.waitFor(`!!document.querySelector('#gate.show')`, 'gate');
  await jsClick('#begin');
  await jsClick('#create-id');
  await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup step');
  await jsClick('#ack-key');
  await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe step');
  await cdp.eval(`document.querySelector('#vibe-name').value = 'CH0HIVE'`);
  await jsClick('#do-vibe');
  await jsClick('#meet-payne');
  await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive');

  const defined = await cdp.eval(`[typeof openThreadPane, typeof startRobotConversation, typeof closeThreadPane]`);
  const scriptHits = defined.every((t) => t === 'undefined') ? [] : [{ kind: 'script', text: 'thread pane functions defined: ' + defined.join(',') }];

  // Seed #welcome through the owner-stamped /api/event (paired nonce).
  await cdp.eval(`[...document.querySelectorAll('.chan')].find((b) => b.textContent.includes('#welcome')).click()`);
  await cdp.waitFor(`document.querySelectorAll('#stream .note').length >= 1`, 'welcome note');
  const post = (body, route = '/api/event') => cdp.evalAsync(`(async () => {
    const m = document.cookie.match(/(?:^|; )hush_nonce=([0-9a-f]{32})(?:;|$)/);
    const headers = { 'Content-Type': 'application/json' };
    if (m) headers['X-Hush-Nonce'] = m[1];
    const r = await fetch(${JSON.stringify(route)}, { method: 'POST', headers, body: JSON.stringify(${JSON.stringify(body)}) });
    return { status: r.status, json: r.ok ? await r.json() : null }; })()`);
  const p1 = await post({ content: 'Morning, Major. Status on the supply manifest?', kind: 1, channel: 'welcome' });
  const p2 = await post({ content: 'Manifest attached.\n```txt\nrations 40\nfuel 12\n```', kind: 1, channel: 'welcome' });
  if (p1.status !== 200 || p2.status !== 200 || !p1.json || !p2.json || !p1.json.id || !p2.json.id)
    fail('seed roots: ' + JSON.stringify([p1, p2]));
  const r1 = p1.json.id;
  const r2 = p2.json.id;
  for (const [root, text] of [[r1, 'Copy that. Reading in now.'], [r1, 'Continue this loop? Reply Yes or No.'],
    [r2, 'Approval needed: Major wants to take a turn. Reply Yes or No.']]) {
    const st = await post({ content: text, kind: 1, channel: 'welcome', reply_to: root });
    if (st.status !== 200) fail('seed reply ' + JSON.stringify(st));
  }
  await cdp.waitFor(`document.querySelector('#stream').textContent.includes('Approval needed: Major') &&
    document.querySelectorAll('#stream .loop-stamp.loop-yes').length >= 2`, 'flat replies with Yes/No stamps', 15000);
  // G20 / M1 acceptance: a real robot reply to a channel note is visible in
  // the channel with no click. Raise Ace on the codex stub and mention it.
  // If the hive asks first ("Approval needed: Ace ..."), the owner answers
  // with the channel's own Yes stamp (the thread composer is gone). Then
  // nothing is clicked while the reply is awaited.
  const raised = await post({ name: 'Ace', system_prompt: 'Answer in one line.', provider: 'codex', save_pass: false }, '/api/agent');
  const ace = raised.json && (raised.json.agents || []).find((a) => a.name === 'Ace');
  if (!ace || !ace.npub) fail('G20 setup: Ace was not raised ' + JSON.stringify(raised).slice(0, 300));
  // #336 approve-every-action: the turn must wait for the owner's Yes, which
  // proves the Yes/No ask is answerable from the channel log.
  const mode = await post({ approval_mode: 'approve_every_action' }, '/api/profile');
  if (mode.status !== 200) fail('G20 setup: approval_mode ' + JSON.stringify(mode).slice(0, 200));
  const ask = await post({ content: 'nostr:' + ace.npub + ' status check for G20', kind: 1, channel: 'welcome', mention_0: ace.npub });
  if (ask.status !== 200 || !ask.json || !ask.json.id) fail('G20 ask ' + JSON.stringify(ask));
  const ASKID = JSON.stringify(ask.json.id);
  let gate = '';
  for (let k = 0; k < 60 && !gate; k++) {
    gate = await cdp.evalAsync(`(async () => { const j = await (await fetch('/api/events')).json();
      const evs = (j.events || j || []).filter((e) => e.reply_to === ${ASKID});
      if (evs.some((e) => String(e.content || '').includes(${JSON.stringify(STUB_REPLY)}))) return 'ran';
      const a = evs.find((e) => String(e.content || '').startsWith('Approval needed: Ace'));
      if (!a) return '';
      const card = [...document.querySelectorAll('#stream .note')].find((n) => n.textContent.includes('Approval needed: Ace'));
      const yes = card && card.querySelector('.loop-stamp.loop-yes');
      if (!yes || yes.disabled) return '';
      yes.click(); return 'asked'; })()`);
    if (!gate) await sleep(500);
  }
  if (gate !== 'asked') fail('G20 Ace must wait for the owner (approve_every_action); got ' + JSON.stringify(gate));
  // Wait for a robot-authored reply event (reply_to set, not the owner, not a
  // developer-log receipt) and require it to be visible in #stream as its OWN
  // card with no click. The roots-only log at 657408d9 hid every such reply
  // behind the thread pane (G20); the FLR-F9 line on the asking card does not
  // count here because it was already shown at 657408d9.
  const INLINE = `(async () => { const r = await fetch('/api/events'); const j = await r.json();
    const evs = (j.events || j || []).filter((e) => e.reply_to === ${ASKID} && !isMineNote(e) && !isDevLogNote(e)
      && String(e.content || '').includes(${JSON.stringify(STUB_REPLY)}));
    const shown = (el) => el.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true }) && el.getBoundingClientRect().height > 0;
    const cards = [...document.querySelectorAll('#stream .note')].filter((n) => shown(n) && !n.querySelector('.log-failure'));
    for (const e of evs) { const c = String(e.content || '').trim().slice(0, 48); if (!c) continue;
      const by = who(e.pubkey);
      const el = cards.find((n) => { const m = n.querySelector('.meta'); return m && m.textContent.startsWith(by + ' ') && n.textContent.includes(c); });
      if (el) return { by, reply: c, reply_to_ask: e.reply_to === ${ASKID}, robotReplies: evs.length }; }
    return null; })()`;
  let inline = null;
  for (let k = 0; k < 60 && !inline; k++) { inline = await cdp.evalAsync(INLINE); if (!inline) await sleep(500); }
  if (!inline) {
    const dbg = await cdp.evalAsync(`(async () => { const j = await (await fetch('/api/events')).json();
      return { evs: (j.events || j || []).filter((e) => e.channel === 'welcome' || !e.channel).slice(-12).map((e) => ({ by: who(e.pubkey), mine: isMineNote(e), rt: e.reply_to ? e.reply_to.slice(0, 8) : '', id: (e.id || '').slice(0, 8), c: String(e.content || '').slice(0, 40) })),
        cards: [...document.querySelectorAll('#stream .note .meta')].map((m) => m.textContent.slice(0, 30)) }; })()`);
    fail('G20 no robot reply became visible inline in #welcome as its own card with no click (30s) ' + JSON.stringify(dbg));
  }
  console.log('G20 PASS robot reply visible inline with no click: ' + JSON.stringify(Object.assign({ approval: gate === 'asked' ? 'answered by the channel Yes stamp' : 'none asked' }, inline)));

  // An empty channel for the idle state.
  await cdp.evalAsync(`(async () => { applySession(await api('/api/channel', { name: 'quiet-room', system_prompt: '' })); return true; })()`);
  await cdp.waitFor(`[...document.querySelectorAll('.chan')].some((b) => b.textContent.includes('#quiet-room'))`, 'quiet-room channel');

  // Page helpers: channel switch, status wrapper, send failure, drawers.
  await cdp.eval(`(() => {
    window.__ch0 = { thinking: null, failSend: false };
    const real = window.fetch.bind(window);
    window.fetch = async (u, o) => {
      const url = String(u);
      if (window.__ch0.failSend && url.endsWith('/api/event') && o && o.method === 'POST')
        return new Response('relay error', { status: 500 });
      const r = await real(u, o);
      if (window.__ch0.thinking && url.indexOf('/api/status') >= 0) {
        const j = await r.clone().json();
        j.thinking = window.__ch0.thinking;
        return new Response(JSON.stringify(j), { status: 200, headers: { 'Content-Type': 'application/json' } });
      }
      return r;
    };
    window.__ch0go = (name) => { const b = [...document.querySelectorAll('.chan')].find((x) => x.textContent.includes('#' + name)); if (b) b.click(); };
    return true; })()`);
  const closeLayers = async () => {
    await cdp.eval(`(() => { for (const id of ['settings', 'agent-drawer', 'profile', 'relay-drawer']) { const e = document.getElementById(id); if (e) e.classList.remove('show'); }
      if (typeof closeNavigation === 'function') closeNavigation(); const mb = document.getElementById('mention-box'); if (mb) { mb.hidden = true; mb.classList.remove('show'); }
      window.__ch0.thinking = null; window.__ch0.failSend = false; return true; })()`);
  };
  const toWelcome = async () => {
    await cdp.eval(`window.__ch0go('welcome')`);
    await cdp.waitFor(`document.querySelector('#stream').textContent.includes('Approval needed: Major')`, 'welcome log');
  };
  const STATES = [
    ['idle', async () => {
      await cdp.eval(`window.__ch0go('quiet-room')`);
      await cdp.waitFor(`!!document.querySelector('#stream .empty')`, 'empty channel');
    }],
    ['log', toWelcome],
    ['replying', async () => {
      await toWelcome();
      await cdp.eval(`window.__ch0.thinking = [{ name: 'Major', parent: ${JSON.stringify(r1)} }]`);
      await cdp.waitFor(`!!document.querySelector('#stream .think-stop')`, 'thinking chip', 8000);
    }],
    ['decision', async () => {
      await toWelcome();
      await cdp.eval(`(() => { const y = [...document.querySelectorAll('#stream .loop-stamp.loop-yes')].pop(); y.scrollIntoView({ block: 'center' }); y.click(); return true; })()`);
      await sleep(150);
    }],
    ['error', async () => {
      await toWelcome();
      await cdp.eval(`(() => { window.__ch0.failSend = true; document.getElementById('msg').value = 'This send is refused.';
        document.getElementById('form').requestSubmit(); return true; })()`);
      await cdp.waitFor(`document.getElementById('send-error').textContent.length > 0`, 'send error');
    }],
    ['mention', async () => {
      await toWelcome();
      await cdp.eval(`(() => { const m = document.getElementById('msg'); m.focus(); m.value = '@'; m.setSelectionRange(1, 1);
        m.dispatchEvent(new Event('input', { bubbles: true })); return true; })()`);
      await sleep(150);
    }],
    ['boards', async () => {
      await toWelcome();
      await cdp.eval(`(() => { const t = document.getElementById('nav-toggle'); const r = t && t.getBoundingClientRect();
        if (t && r.width > 0 && t.getAttribute('aria-expanded') !== 'true') t.click(); return true; })()`);
      await sleep(250);
    }],
    ['settings', async () => {
      await toWelcome();
      await cdp.eval(`document.getElementById('settings-btn').click()`);
      await cdp.waitFor(`document.getElementById('settings').classList.contains('show')`, 'settings');
    }],
    ['robot', async () => {
      await toWelcome();
      await cdp.eval(`(() => { const b = robotModels().find((x) => x.slug === PAYNE_SLUG) || robotModels()[0]; openAgentDrawer(b); return true; })()`);
      await cdp.waitFor(`document.getElementById('agent-drawer').classList.contains('show')`, 'robot drawer');
      await sleep(300);
    }],
  ];

  const probe = { head: process.env.CH0_HEAD || '', states: STATES.map((s) => s[0]), themes: THEMES, views: VIEWS.map((v) => v.vp), runs: 0, hits: [] };
  for (const v of VIEWS) {
    await view(v);
    for (const theme of THEMES) {
      await cdp.eval(`applyTheme(${JSON.stringify(theme)})`);
      for (const [name, setup] of STATES) {
        await closeLayers();
        await setup();
        await cdp.eval(`applyTheme(${JSON.stringify(theme)})`);
        const hits = await cdp.eval(SCAN + '(false)');
        probe.runs++;
        for (const h of hits) probe.hits.push(Object.assign({ state: name, theme, vp: v.vp }, h));
        if (ART && SHOT_THEMES.includes(theme)) {
          const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
          fs.mkdirSync(path.join(ART, 'shots'), { recursive: true });
          fs.writeFileSync(path.join(ART, 'shots', `${name}-${theme}-${v.vp}.png`), Buffer.from(shot.data, 'base64'));
        }
      }
    }
  }
  await closeLayers();
  const domHits = (await cdp.eval(SCAN + '(true)')).map((h) => Object.assign({ state: 'whole-dom' }, h));
  probe.hits.push(...domHits, ...scriptHits);
  probe.pageErrors = pageErrors;
  if (ART) {
    fs.mkdirSync(path.join(ART, 'probe'), { recursive: true });
    fs.writeFileSync(path.join(ART, 'probe', 'CH0.json'), JSON.stringify(probe.hits, null, 1) + '\n');
    fs.writeFileSync(path.join(ART, 'probe', 'CH0-run.json'), JSON.stringify(probe, null, 1) + '\n');
  }
  console.log(`CH0 scanned ${probe.runs} state x theme x viewport paints (${STATES.length} states x ${THEMES.length} themes x ${VIEWS.length} viewports) + whole DOM`);
  if (pageErrors.length)
    console.log('CH0 page exceptions: ' + JSON.stringify(pageErrors.slice(0, 5)));
  if (probe.hits.length) {
    for (const h of probe.hits.slice(0, 20)) console.error('CH0 hit: ' + JSON.stringify(h));
    fail(`${probe.hits.length} thread UI hits (CH0_HARD wants 0)`);
  }
  if (pageErrors.length)
    fail(`${pageErrors.length} uncaught page exceptions`);
  if (!(await stopAll())) {
    console.error('CH0 UI check failed: teardown left temp dirs');
    process.exit(1);
  }
  console.log('CH0 UI check passed: 0 thread UI hits in every state, theme and viewport; no thread pane code; temp dirs removed');
  process.exit(0);
}

main().catch((e) => {
  if (e instanceof StopRun) return;
  try { fail(e && e.stack ? e.stack : String(e)); } catch (e2) { /* teardown running */ }
});
