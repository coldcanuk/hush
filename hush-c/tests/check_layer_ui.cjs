// check_layer_ui.cjs: #283 layer pins D1-D10 for the Developer Log drawer,
// the New channel dialog and Manage Channel. Drives headless system Chrome
// over CDP with the Node standard library only (no npm packages). Every
// pin uses real CDP input (mouse clicks, Tab / Shift-Tab / Escape / Space
// keys) and reads rendered state: which layers show, where focus is, and
// what the Kit menu does. Each pin runs at 1440 light, 1440 field-office
// and 390 dark. All pins run before the verdict, so a failing build lists
// every failing pin, then exits 1.
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// HUSH_TEST_WAIT_S (Chrome DevTools ready deadline, default 30),
// L283_ONLY (comma list of pin names, e.g. D6,D9, for local runs).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

const RELAY = process.env.HUSH_RELAY_BIN || path.join(__dirname, '..', 'hush-relay');
const ONLY = (process.env.L283_ONLY || '').split(',').filter(Boolean);
const CONFIGS = [
  { name: '1440-light', w: 1440, h: 900, theme: 'light' },
  { name: '1440-field-office', w: 1440, h: 900, theme: 'field-office' },
  { name: '390-dark', w: 390, h: 844, theme: 'dark' },
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const liveProcs = [];
function fail(msg) {
  for (const p of liveProcs.splice(0)) {
    try { p.kill('SIGTERM'); } catch (e) { /* gone */ }
  }
  console.error('layer UI check failed: ' + msg);
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
    const userDir = fs.mkdtempSync(path.join(os.tmpdir(), 'l283-chrome-'));
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
    const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true });
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

// Page-side reader: which layers show, where focus is, Kit menu state.
const STATE = `(() => {
  const a = document.activeElement;
  const shown = (id) => { const e = document.getElementById(id); return !!(e && e.classList.contains('show')); };
  const within = (id) => { const e = document.getElementById(id); return !!(e && a && e.contains(a)); };
  return {
    ae: a ? (a.id || (a.getAttribute('aria-label') || a.tagName)) : null,
    devlog: shown('dev-log-drawer'), settings: shown('settings'),
    newchan: shown('new-chan-drawer'), manage: shown('manage-chan'),
    kitOpen: !document.getElementById('kit-menu').hidden,
    navOpen: document.getElementById('hive').classList.contains('nav-open'),
    devlogOn: document.getElementById('dev-log').checked,
    modal: ['dev-log-drawer', 'new-chan-drawer', 'manage-chan'].filter((id) => document.getElementById(id).getAttribute('aria-modal') === 'true'),
    inDevlog: within('dev-log-drawer'), inSettings: within('settings'),
    inNewchan: within('new-chan-drawer'), inManage: within('manage-chan')
  };
})()`;

async function main() {
  const chrome = resolveChrome();
  const cdp = new Cdp(await launchChrome(chrome));
  await cdp.connect();
  await cdp.send('Page.enable', {});

  const home = fs.mkdtempSync(path.join(os.tmpdir(), 'l283-home-'));
  const cfg = fs.mkdtempSync(path.join(os.tmpdir(), 'l283-cfg-'));
  const port = await freePort();
  const relay = spawn(RELAY, ['--no-open', String(port)], {
    env: Object.assign({}, process.env, {
      HUSH_HOME: home, HUSH_CONFIG_DIR: cfg, HUSH_PASS_HELPER: '/nonexistent-hush-pass-helper',
    }),
    stdio: 'ignore',
  });
  liveProcs.push(relay);
  let up = false;
  for (let i = 0; i < 200 && !up; i++) {
    try { up = (await fetch(`http://127.0.0.1:${port}/`)).ok; } catch (e) { /* not up */ }
    if (!up) await sleep(100);
  }
  if (!up)
    fail(`relay did not come up on ${port}`);

  const view = async (c) => {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: c.w, height: c.h, deviceScaleFactor: 1, mobile: false });
  };
  const jsClick = async (sel) => {
    await cdp.waitFor(`!!document.querySelector('${sel}')`, sel);
    await cdp.eval(`document.querySelector('${sel}').click()`);
  };

  // Setup through the real gate once: create a key, name the hive, meet Major.
  await view(CONFIGS[0]);
  await cdp.send('Page.navigate', { url: `http://127.0.0.1:${port}/` });
  await cdp.waitFor(`!!document.querySelector('#gate.show')`, 'gate');
  await jsClick('#begin');
  await jsClick('#create-id');
  await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup step');
  await jsClick('#ack-key');
  await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe step');
  await cdp.eval(`document.querySelector('#vibe-name').value = 'LAYERHIVE'`);
  await jsClick('#do-vibe');
  await jsClick('#meet-payne');
  await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive');

  // Real CDP input.
  const point = (sel) => cdp.eval(`(() => { const e = document.querySelector(${JSON.stringify(sel)});
    if (!e) return null; e.scrollIntoView({ block: 'center', inline: 'center' }); const r = e.getBoundingClientRect();
    if (!r.width || !r.height) return null;
    return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
  const mouseAt = async (pt) => {
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: pt.x, y: pt.y });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: pt.x, y: pt.y, button: 'left', clickCount: 1, buttons: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: pt.x, y: pt.y, button: 'left', clickCount: 1, buttons: 0 });
    await sleep(250);
  };
  // Returns false when the target has no box (cannot be clicked).
  const realClick = async (sel) => {
    const pt = await point(sel);
    if (!pt) return false;
    await mouseAt(pt);
    return true;
  };
  const KEYS = {
    Escape: { key: 'Escape', code: 'Escape', vk: 27 },
    Tab: { key: 'Tab', code: 'Tab', vk: 9 },
    Enter: { key: 'Enter', code: 'Enter', vk: 13, text: '\r' },
    Space: { key: ' ', code: 'Space', vk: 32, text: ' ' },
  };
  const press = async (name, shift) => {
    const k = KEYS[name];
    const mod = shift ? 8 : 0;
    const down = { type: k.text ? 'keyDown' : 'rawKeyDown', key: k.key, code: k.code,
      windowsVirtualKeyCode: k.vk, nativeVirtualKeyCode: k.vk, modifiers: mod };
    if (k.text) down.text = k.text;
    await cdp.send('Input.dispatchKeyEvent', down);
    await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: k.key, code: k.code,
      windowsVirtualKeyCode: k.vk, nativeVirtualKeyCode: k.vk, modifiers: mod });
    await sleep(name === 'Tab' ? 60 : 250);
  };
  const state = () => cdp.eval(STATE);
  // A point on the layer's dimmed backdrop: inside the drawer box but
  // outside its .panel, where elementFromPoint is the drawer itself.
  const backdropPoint = (id) => cdp.eval(`(() => {
    const d = document.getElementById('${id}');
    if (!d || !d.classList.contains('show')) return null;
    const W = window.innerWidth, H = window.innerHeight;
    const cands = [[8, 24], [W - 8, 24], [W / 2, 20], [8, H - 8], [W - 8, H - 8], [W / 2, H - 6], [8, H / 2], [W - 8, H / 2]];
    for (const c of cands) {
      const t = document.elementFromPoint(c[0], c[1]);
      if (t === d) return { x: c[0], y: c[1] };
    }
    return null;
  })()`);
  const backdropClick = async (id) => {
    const pt = await backdropPoint(id);
    if (!pt) return false;
    await mouseAt(pt);
    return true;
  };
  // Focus stops reached by n real Tab (or Shift-Tab) presses.
  const tabWalk = async (n, shift, id) => {
    const stops = [];
    for (let i = 0; i < n; i++) {
      await press('Tab', shift);
      stops.push(await cdp.eval(`(() => { const a = document.activeElement; const d = document.getElementById('${id}');
        return { ae: a ? (a.id || a.getAttribute('aria-label') || a.tagName) : null, inside: !!(a && d.contains(a)) }; })()`));
    }
    return stops;
  };
  const focusableCount = (id) => cdp.eval(`(() => { const d = document.getElementById('${id}');
    return Array.prototype.filter.call(d.querySelectorAll('button, [href], input, select, textarea, summary, [tabindex]:not([tabindex="-1"])'),
      (el) => !el.disabled && el.getAttribute('aria-hidden') !== 'true' && el.getClientRects().length > 0
        && getComputedStyle(el).visibility !== 'hidden').length; })()`);

  // Fresh page for every pin run: reload, pin the theme through the app's
  // stored-choice boot path, Developer Logging off.
  const reset = async (c) => {
    await view(c);
    await cdp.eval(`localStorage.setItem('hush-theme', ${JSON.stringify(c.theme)})`);
    await cdp.send('Page.reload', {});
    await sleep(200);
    await cdp.waitFor(`!!document.querySelector('#hive.show') && document.documentElement.getAttribute('data-theme') === ${JSON.stringify(c.theme)}`, 'hive at ' + c.name);
    await cdp.eval(`(() => { const s = document.getElementById('dev-log');
      if (s.checked) { s.checked = false; s.dispatchEvent(new Event('change')); } })()`);
    await sleep(150);
    await cdp.eval(`document.activeElement && document.activeElement.blur && document.activeElement.blur()`);
  };
  // Settings through the real Kit path (Kit stamp, then Settings).
  const openSettingsReal = async () => {
    const a = await realClick('#rail-toggle');
    const b = await realClick('#settings-btn');
    return a && b && (await cdp.tryWait(`document.getElementById('settings').classList.contains('show')`, 1500));
  };
  // Developer Logging on: a real click on the switch, or focus + Space.
  const devlogOn = async (how) => {
    if (how === 'mouse') {
      if (!(await realClick('#dev-log + .slider'))) return false;
    } else {
      await cdp.eval(`document.getElementById('dev-log').focus()`);
      await press('Space');
    }
    return await cdp.tryWait(`document.getElementById('dev-log').checked && document.getElementById('dev-log-drawer').classList.contains('show')`, 2000);
  };
  // Opens New channel through one of its three real openers.
  const openNewChan = async (opener) => {
    if (opener === '#add-chan') {
      if (!(await realClick('#rail-toggle'))) return false;
      if (!(await realClick('#add-chan'))) return false;
    } else if (opener === '#nav-new-channel') {
      if (!(await realClick('#nav-toggle'))) return false;
      await cdp.tryWait(`document.getElementById('hive').classList.contains('nav-open')`, 1000);
      await sleep(300);
      if (!(await realClick('#nav-new-channel'))) return false;
    } else if (!(await realClick('#qb-3'))) {
      return false;
    }
    return await cdp.tryWait(`document.getElementById('new-chan-drawer').classList.contains('show')`, 1500);
  };
  const OPTS = '.chan-options[aria-label="Options for #general"]';
  // Opens Manage Channel the real way: BOARDS, then ⋯ on #general, then Manage.
  const openManage = async () => {
    if (!(await realClick('#nav-toggle'))) return false;
    await cdp.tryWait(`document.getElementById('hive').classList.contains('nav-open')`, 1000);
    await sleep(300);
    if (!(await realClick(OPTS))) return false;
    await cdp.tryWait(`document.getElementById('chan-menu').classList.contains('show')`, 1000);
    if (!(await realClick('#chan-menu button[data-act="manage"]'))) return false;
    return await cdp.tryWait(`document.getElementById('manage-chan').classList.contains('show')`, 1500);
  };
  const isOpts = (ae) => ae === 'Options for #general';

  const results = [];
  const record = (pin, c, variant, ok, detail) => {
    results.push({ pin, cfg: c.name, variant, ok });
    console.log(`L283 ${ok ? 'PASS' : 'FAIL'} ${pin} ${c.name} ${variant}: ${JSON.stringify(detail)}`);
  };

  const PINS = {
    // D1: Settings -> Developer Logging on -> Esc closes the log, a second
    // Esc closes Settings, then one real click on KIT opens the Kit menu.
    D1: async (c) => {
      for (const how of ['mouse', 'keyboard']) {
        await reset(c);
        const opened = await openSettingsReal();
        const on = await devlogOn(how);
        await press('Escape');
        const esc1 = await state();
        await press('Escape');
        const esc2 = await state();
        const hit = await cdp.eval(`(() => { const k = document.getElementById('rail-toggle'); const r = k.getBoundingClientRect();
          const t = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return t ? (t.id || (t.closest('[id]') || {}).id || t.tagName) : null; })()`);
        await realClick('#rail-toggle');
        const kit = await state();
        record('D1', c, how, opened && on && !esc2.devlog && !esc2.settings && kit.kitOpen,
          { opened, on, esc1, esc2: { devlog: esc2.devlog, settings: esc2.settings }, kitHit: hit, kitOpen: kit.kitOpen });
      }
    },
    // D2: dev-log open -> Esc closes it; Settings stays open underneath.
    D2: async (c) => {
      await reset(c);
      const opened = await openSettingsReal();
      const on = await devlogOn('mouse');
      await press('Escape');
      const s = await state();
      record('D2', c, 'esc', opened && on && !s.devlog && s.settings, { opened, on, after: s });
    },
    // D3: dev-log open -> a real click on its backdrop closes it.
    D3: async (c) => {
      await reset(c);
      const opened = await openSettingsReal();
      const on = await devlogOn('mouse');
      const inPanel = await realClick('#dev-log-body');
      const kept = await state();
      const clicked = await backdropClick('dev-log-drawer');
      const s = await state();
      record('D3', c, 'backdrop', opened && on && inPanel && kept.devlog && clicked && !s.devlog && s.settings,
        { opened, on, panelClickKeepsOpen: inPanel && kept.devlog, clicked, after: s });
    },
    // D4: focus moves into the dev-log drawer on open and returns to the
    // #dev-log switch on Esc, Close and backdrop.
    D4: async (c) => {
      for (const how of ['esc', 'close', 'backdrop']) {
        await reset(c);
        const opened = await openSettingsReal();
        const on = await devlogOn('mouse');
        const open = await state();
        let did = true;
        if (how === 'esc') await press('Escape');
        else if (how === 'close') did = await realClick('#dev-log-close2');
        else did = await backdropClick('dev-log-drawer');
        const s = await state();
        const modal = open.modal.join() === 'dev-log-drawer' && !s.modal.length;
        record('D4', c, how, opened && on && open.inDevlog && modal && did && !s.devlog && s.ae === 'dev-log',
          { opened, on, openFocus: open.ae, inDevlog: open.inDevlog, modal: [open.modal, s.modal], did, after: { devlog: s.devlog, ae: s.ae } });
      }
    },
    // D5: on -> Close -> off. Mouse: real clicks on the switch and Close
    // (regression guard for the off click). Keyboard: Space on, Enter on
    // the focused Close, Space again turns it off (needs focus return).
    D5: async (c) => {
      await reset(c);
      let opened = await openSettingsReal();
      let on = await devlogOn('mouse');
      let closed = await realClick('#dev-log-close2');
      let off = await realClick('#dev-log + .slider');
      await sleep(300);
      let s = await state();
      record('D5', c, 'mouse', opened && on && closed && off && !s.devlogOn && !s.devlog && s.settings,
        { opened, on, closed, off, after: { devlogOn: s.devlogOn, devlog: s.devlog, settings: s.settings } });
      await reset(c);
      opened = await openSettingsReal();
      on = await devlogOn('keyboard');
      await cdp.eval(`document.getElementById('dev-log-close2').focus()`);
      await press('Enter');
      const afterClose = await state();
      await press('Space');
      await sleep(300);
      s = await state();
      record('D5', c, 'keyboard', opened && on && !afterClose.devlog && !s.devlogOn && !s.devlog && s.settings,
        { opened, on, afterClose: { devlog: afterClose.devlog, ae: afterClose.ae }, after: { devlogOn: s.devlogOn, devlog: s.devlog, settings: s.settings } });
    },
    // D6: New channel traps Tab: Tab from Cancel lands on #new-chan,
    // Shift-Tab from #new-chan lands on Cancel, and a full Tab and
    // Shift-Tab cycle never stops outside the dialog.
    D6: async (c) => {
      await reset(c);
      const opened = await openNewChan('#qb-3');
      const n = await focusableCount('new-chan-drawer');
      await cdp.eval(`document.getElementById('new-chan-close').focus()`);
      await press('Tab');
      const fromCancel = await state();
      await cdp.eval(`document.getElementById('new-chan').focus()`);
      await press('Tab', true);
      const fromName = await state();
      await cdp.eval(`document.getElementById('new-chan').focus()`);
      const fwd = await tabWalk(n + 1, false, 'new-chan-drawer');
      await cdp.eval(`document.getElementById('new-chan').focus()`);
      const back = await tabWalk(n + 1, true, 'new-chan-drawer');
      const outside = fwd.concat(back).filter((x) => !x.inside);
      record('D6', c, 'tab', opened && n >= 4 && fromCancel.ae === 'new-chan' && fromName.ae === 'new-chan-close' && !outside.length,
        { opened, focusables: n, fromCancel: fromCancel.ae, fromName: fromName.ae, outside: outside.length,
          fwd: fwd.map((x) => x.ae), back: back.map((x) => x.ae) });
    },
    // D7: New channel closes on Esc and on a backdrop click, and focus goes
    // back to the visible control that opened it: #qb-3 itself, the Kit
    // stamp for #add-chan (the Kit menu hides on click, as for Settings),
    // and the BOARDS stamp for #nav-new-channel (opening closes the boards
    // drawer).
    D7: async (c) => {
      const BACK = { '#qb-3': 'qb-3', '#add-chan': 'rail-toggle', '#nav-new-channel': 'nav-toggle' };
      const RUNS = [['#qb-3', 'esc'], ['#qb-3', 'backdrop'], ['#qb-3', 'cancel'], ['#qb-3', 'create'],
        ['#add-chan', 'esc'], ['#add-chan', 'backdrop'], ['#nav-new-channel', 'esc'], ['#nav-new-channel', 'backdrop']];
      for (const [opener, how] of RUNS) {
        {
          await reset(c);
          const opened = await openNewChan(opener);
          const open = await state();
          let did = true;
          if (how === 'esc') await press('Escape');
          else if (how === 'backdrop') did = await backdropClick('new-chan-drawer');
          else if (how === 'cancel') did = await realClick('#new-chan-close');
          else {
            const name = 'layer-' + c.name.replace(/[^a-z0-9]/g, '');
            await cdp.eval(`document.getElementById('new-chan').value = ${JSON.stringify(name)}`);
            did = (await realClick('#new-chan-save')) &&
              (await cdp.tryWait(`(session.channels || []).some((x) => x.slug === ${JSON.stringify(name)})`, 4000));
            await sleep(300);
          }
          const s = await state();
          const modal = open.modal.join() === 'new-chan-drawer' && !s.modal.length;
          record('D7', c, `${opener} ${how}`, opened && open.inNewchan && modal && did && !s.newchan && s.ae === BACK[opener],
            { opened, openFocus: open.ae, modal: [open.modal, s.modal], did, after: { newchan: s.newchan, ae: s.ae }, want: BACK[opener] });
        }
      }
    },
    // D8: Manage Channel closes on Esc, one layer per press (the boards
    // drawer stays open), and focus returns to the ⋯ opener.
    // Close and Save (Save repaints the channel list, so the ⋯ that
    // opened the menu is replaced) return focus the same way.
    D8: async (c) => {
      for (const how of ['esc', 'close', 'save']) {
        await reset(c);
        const opened = await openManage();
        await cdp.eval(`window.__l283Opts = document.querySelector(${JSON.stringify(OPTS)})`);
        let did = true;
        if (how === 'esc') await press('Escape');
        else if (how === 'close') did = await realClick('#manage-close');
        else {
          did = await realClick('#manage-save');
          await cdp.tryWait(`!document.getElementById('manage-chan').classList.contains('show')`, 4000);
          await sleep(300);
        }
        const s = await state();
        const repainted = await cdp.eval(`!window.__l283Opts.isConnected`);
        record('D8', c, how, opened && did && !s.manage && s.navOpen && isOpts(s.ae) && (how !== 'save' || repainted),
          { opened, did, repainted, after: { manage: s.manage, navOpen: s.navOpen, ae: s.ae } });
      }
    },
    // D9: Manage Channel moves focus inside on open, Tab and Shift-Tab stay
    // inside over a full cycle, and a backdrop click closes it.
    D9: async (c) => {
      await reset(c);
      const opened = await openManage();
      const open = await state();
      const n = await focusableCount('manage-chan');
      const fwd = await tabWalk(n + 1, false, 'manage-chan');
      const back = await tabWalk(n + 1, true, 'manage-chan');
      const outside = fwd.concat(back).filter((x) => !x.inside);
      const inPanel = await realClick('#manage-title');
      const kept = await state();
      const did = await backdropClick('manage-chan');
      const s = await state();
      const modal = open.modal.join() === 'manage-chan' && !s.modal.length;
      record('D9', c, 'focus+trap+backdrop', opened && open.inManage && modal && n >= 4 && !outside.length && inPanel && kept.manage && did && !s.manage,
        { opened, openFocus: open.ae, inManage: open.inManage, modal: [open.modal, s.modal], focusables: n, outside: outside.length, panelClickKeepsOpen: inPanel && kept.manage,
          outsideStops: outside.slice(0, 4).map((x) => x.ae), did, after: { manage: s.manage, ae: s.ae } });
    },
    // D10: with dev-log over Settings, the first Esc closes only dev-log
    // and the second closes Settings (focus back on the Kit stamp).
    D10: async (c) => {
      await reset(c);
      const opened = await openSettingsReal();
      const on = await devlogOn('mouse');
      await press('Escape');
      const e1 = await state();
      await press('Escape');
      const e2 = await state();
      record('D10', c, 'esc-esc', opened && on && !e1.devlog && e1.settings && !e2.devlog && !e2.settings && e2.ae === 'rail-toggle',
        { opened, on, esc1: { devlog: e1.devlog, settings: e1.settings, ae: e1.ae }, esc2: { devlog: e2.devlog, settings: e2.settings, ae: e2.ae } });
    },
  };

  for (const c of CONFIGS) {
    for (const pin of Object.keys(PINS)) {
      if (ONLY.length && !ONLY.includes(pin)) continue;
      await PINS[pin](c);
    }
  }
  const bad = results.filter((r) => !r.ok);
  const pins = {};
  for (const r of results) {
    pins[r.pin] = pins[r.pin] || { pass: 0, fail: 0 };
    pins[r.pin][r.ok ? 'pass' : 'fail']++;
  }
  console.log('L283 SUMMARY ' + JSON.stringify(pins));
  for (const p of liveProcs.splice(0)) {
    try { p.kill('SIGTERM'); } catch (e) { /* gone */ }
  }
  if (!results.length)
    fail('no pin ran');
  if (bad.length) {
    console.error('layer UI check failed: ' + bad.length + '/' + results.length + ' pin runs failed: ' +
      bad.map((r) => `${r.pin} ${r.cfg} ${r.variant}`).join('; '));
    process.exit(1);
  }
  console.log(`layer UI check passed: ${results.length}/${results.length} pin runs (${Object.keys(pins).join(",")} x ${CONFIGS.length} configs)`);
}

main().catch((e) => fail(e && e.stack ? e.stack : String(e)));
