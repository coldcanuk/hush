// check_restart_ui.cjs: behaviour proof for the ID-1 restart/backup UI.
// Drives headless system Chrome over CDP with the Node standard library
// only (no npm packages, nothing installed). Every assertion below reads
// rendered behaviour (checkbox state, visible copy, header text, painted
// contrast), never page source. Fails loudly when node, Chrome, the relay,
// or any behaviour is missing.
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// ID1_TAG (shot filename tag, default "after"), ID1_NO_ASSERT=1 (navigate
// and shoot without asserting, for before/after pairs), ID1_VIEW_W
// (viewport width, default 1440), ID1_ART (shot/JSON dir).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const RELAY = process.env.HUSH_RELAY_BIN || path.join(__dirname, '..', 'hush-relay');
const TAG = process.env.ID1_TAG || 'after';
const NO_ASSERT = process.env.ID1_NO_ASSERT === '1';
const VIEW_W = parseInt(process.env.ID1_VIEW_W || '1440', 10);
const VIEW_H = VIEW_W < 800 ? 800 : 900;
const ART = process.env.ID1_ART || path.join(os.tmpdir(), 'id1-shots');
fs.mkdirSync(ART, { recursive: true });

// Shown on the backup step on both paths (with and without pass).
const NEVER_SHARE = 'Never share your secret key. Anyone with it can impersonate you.';
// The one no-pass reason on the backup step (CoS copy, pre-walk polish).
// It must appear exactly once, and nothing else on that screen may restate it.
const NOPASS_REASON = "Hush can't save this key on this computer, so keep your copy somewhere safe.";
const REASON_ECHO = /can.t save|cannot save|not installed|unavailable|somewhere safe/i;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const liveProcs = [];
function fail(msg) {
  for (const p of liveProcs.splice(0)) {
    try { p.kill('SIGTERM'); } catch (e) { /* gone */ }
  }
  console.error('restart UI check failed: ' + msg);
  process.exit(1);
}
function check(cond, msg) {
  if (!NO_ASSERT && !cond)
    fail(msg);
}

function resolveChrome() {
  const cands = [];
  if (process.env.HUSH_CHROME_BIN)
    cands.push(process.env.HUSH_CHROME_BIN);
  cands.push('google-chrome', 'chromium', 'chromium-browser');
  const pathDirs = (process.env.PATH || '').split(':');
  for (const c of cands) {
    if (!c)
      continue;
    if (c.includes('/') && fs.existsSync(c))
      return c;
    for (const d of pathDirs) {
      const p = path.join(d, c);
      try {
        fs.accessSync(p, fs.constants.X_OK);
        return p;
      } catch (e) { /* next */ }
    }
  }
  fail('no Chrome on PATH (need google-chrome or chromium) and HUSH_CHROME_BIN unset');
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
  async eval(expr, awaitPromise) {
    const r = await this.send('Runtime.evaluate',
      { expression: expr, awaitPromise: !!awaitPromise, returnByValue: true });
    if (r.exceptionDetails)
      throw new Error('page eval threw: ' + expr.slice(0, 120));
    return r.result ? r.result.value : null;
  }
  async waitFor(expr, label, timeout) {
    const t0 = Date.now();
    for (;;) {
      let v = null;
      try {
        v = await this.eval(expr);
      } catch (e) { /* page busy, retry */ }
      if (v)
        return v;
      if (Date.now() - t0 > (timeout || 25000))
        fail('timeout waiting for ' + (label || expr));
      await sleep(250);
    }
  }
  async click(sel) {
    await this.waitFor(`!!document.querySelector('${sel}')`, sel);
    await this.eval(`document.querySelector('${sel}').click()`);
  }
  async shot(name) {
    const r = await this.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(path.join(ART, `${TAG}-${name}-${VIEW_W}.png`),
      Buffer.from(r.data, 'base64'));
    console.log(`shot ${TAG}-${name}-${VIEW_W}.png`);
  }
}

// Painted contrast of a rendered element (opaque ancestor walk).
// Understands the rgb()/rgba() and oklch() serializations this UI emits.
const CONTRAST_EXPR = (sel) => `(() => {
  const el = document.querySelector('${sel}');
  if (!el) return null;
  const lin = (rgb) => {
    const f = (c) => {
      c /= 255;
      return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
    };
    return [f(rgb[0]), f(rgb[1]), f(rgb[2])];
  };
  const parse = (s) => {
    s = s.trim();
    if (s.startsWith('oklch(')) {
      const p = s.slice(6, -1).split('/')[0].trim().split(/\\s+/).map(Number);
      const L = p[0], C = p[1] || 0, H = (p[2] || 0) * Math.PI / 180;
      const a = C * Math.cos(H), b = C * Math.sin(H);
      const l = Math.pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
      const m = Math.pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
      const q = Math.pow(L - 0.0894841775 * a - 1.2914855480 * b, 3);
      const cl = (v) => Math.min(1, Math.max(0, v));
      return { lin: [cl(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * q),
                     cl(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * q),
                     cl(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * q)],
               alpha: s.includes('/') ? parseFloat(s.split('/')[1]) : 1 };
    }
    const nums = s.match(/[\\d.]+/g).map(Number);
    return { lin: lin(nums.slice(0, 3)), alpha: nums.length > 3 ? nums[3] : 1 };
  };
  const lum = (v) => 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
  const cs = getComputedStyle(el);
  const fg = parse(cs.color);
  let bg = null, node = el;
  while (node && node !== document.documentElement) {
    const c = parse(getComputedStyle(node).backgroundColor);
    if (c.alpha >= 1) { bg = c.lin; break; }
    node = node.parentElement;
  }
  if (!bg) bg = parse(getComputedStyle(document.body).backgroundColor).lin;
  const x = lum(fg.lin), y = lum(bg);
  return { ratio: (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05), fg: cs.color, bg: cs.backgroundColor };
})()`;
  const RATIO_OF = (m) => (m && typeof m.ratio === 'number' ? m.ratio : null);

async function main() {
  const chrome = resolveChrome();
  const track = (proc) => { liveProcs.push(proc); return proc; };
  const userDir = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-chrome-'));
  const chromeProc = track(spawn(chrome,
    ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
      '--remote-debugging-port=0', `--user-data-dir=${userDir}`, 'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'] }));
  let devtools = null;
  const t0 = Date.now();
  let stderr = '';
  chromeProc.stderr.on('data', (d) => { stderr += d.toString(); });
  while (Date.now() - t0 < 20000) {
    const m = stderr.match(/DevTools listening on (ws:\/\/\S+)/);
    if (m) { devtools = m[1]; break; }
    await sleep(200);
  }
  if (!devtools)
    fail('Chrome printed no DevTools URL: ' + stderr.slice(0, 300));
  const httpBase = devtools.replace(/^ws:\/\//, 'http://').replace(/\/devtools.*$/, '');
  const targets = await (await fetch(httpBase + '/json/list')).json();
  const pageTarget = targets.find((t) => t.type === 'page');
  if (!pageTarget)
    fail('no page target in ' + JSON.stringify(targets).slice(0, 200));
  const cdp = new Cdp(pageTarget.webSocketDebuggerUrl);
  await cdp.connect();
  await cdp.send('Page.enable', {});
  await cdp.send('Emulation.setDeviceMetricsOverride',
    { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });

  const contrasts = {};
  const relays = [];
  const startRelay = async (home, cfg, passHelper, fakeDir, port) => {
    const env = Object.assign({}, process.env, {
      HUSH_HOME: home, HUSH_CONFIG_DIR: cfg, HUSH_PASS_HELPER: passHelper,
    });
    if (fakeDir)
      env.HUSH_FAKE_PASS_DIR = fakeDir;
    const tokenFile = path.join(home, 'session.token');
    const headers = () => {
      try {
        return { 'X-Hush-Token': fs.readFileSync(tokenFile, 'utf8').trim() };
      } catch (e) {
        return {};
      }
    };
    const proc = track(spawn(RELAY, ['--no-open', String(port)], { env, stdio: 'ignore' }));
    for (let i = 0; i < 200; i++) {
      try {
        const r = await fetch(`http://127.0.0.1:${port}/api/session`, { headers: headers() });
        if (r.ok)
          return { proc, headers };
      } catch (e) { /* not up */ }
      await sleep(100);
    }
    fail(`relay did not come up on ${port}`);
  };
  const stopRelay = async (proc) => {
    proc.kill('SIGTERM');
    for (let i = 0; i < 100 && proc.exitCode === null; i++)
      await sleep(100);
  };
  const sess = async (port, headers) =>
    await (await fetch(`http://127.0.0.1:${port}/api/session`, { headers: headers() })).json();
  const goto = async (port) => {
    await cdp.send('Page.navigate', { url: `http://127.0.0.1:${port}/` });
    await cdp.waitFor(`!!document.querySelector('#gate.show')`, 'gate');
  };
  // Pre-walk r4 (Gauge B1): open the robot editor through the real UI and
  // report whether the pass box and its how-to are actually rendered.
  const ROBOT_PASS_LABEL = 'Checked to save its key in your password manager.';
  const robotPassBox = async (label, openExpr) => {
    await cdp.eval(`(() => { const c = document.querySelector('#agent-close');
      if (document.querySelector('#agent-drawer.show') && c) c.click(); })()`);
    await cdp.eval(openExpr);
    await cdp.waitFor(`document.querySelector('#agent-drawer.show') && document.querySelector('#agent-title').textContent === ${JSON.stringify(label)}`, 'robot editor: ' + label);
    return await cdp.eval(`(() => {
      const l = document.querySelector('#agent-pass').closest('label');
      const h = document.querySelector('#agent-pass-howto');
      const shown = (e) => e.getClientRects().length > 0;
      return { title: document.querySelector('#agent-title').textContent, label: shown(l), howto: shown(h),
        open: h.open, text: l.textContent.trim(), cmd: h.textContent.includes('pass show hush/agents/<slug>/nsec') };
    })()`);
  };
  const editRobot = (slug) => `document.querySelector('#robot-list .robot-card[data-slug="${slug}"] .robot-actions button').click()`;
  const checkRobotPassHidden = (st, why) =>
    check(!st.label && !st.howto, `robot pass box and how-to hidden on ${st.title} (${why}): ${JSON.stringify(st)}`);
  // Pre-walk r4 (Ops FAIL-B): at every width 641-1440, in field-office
  // and dark, the header badge shows whole (no ellipsis) inside the
  // viewport and the header has no sideways overflow. CI runs at 1440, so
  // the viewport is narrowed 1px at a time here, then restored.
  const badgeFit = async (phase, want) => {
    const bad = [];
    let n = 0;
    for (let w = 641; w <= 1440; w++) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
      const r = await cdp.eval(`(() => { const html = document.documentElement; const keep = html.getAttribute('data-theme');
        const h = document.querySelector('header'); const b = document.querySelector('header #badge'); const out = [];
        for (const th of ['field-office', 'dark']) {
          html.setAttribute('data-theme', th);
          const q = b.getBoundingClientRect(); const vw = html.clientWidth;
          if (q.left < -0.5 || q.right > vw + 0.5 || b.scrollWidth > b.clientWidth || h.scrollWidth > h.clientWidth + 1 || !b.textContent.includes(${JSON.stringify(want)}))
            out.push({ th, vw, l: Math.round(q.left), r: Math.round(q.right), sw: b.scrollWidth, cw: b.clientWidth, hsw: h.scrollWidth, hcw: h.clientWidth, t: b.textContent });
        }
        html.setAttribute('data-theme', keep); return out; })()`);
      n++;
      if (r.length && bad.length < 6) bad.push(Object.assign({ w }, r[0]));
      else if (r.length) bad.push(w);
    }
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
    console.log(`header badge fit ${phase}: ${n} widths x 2 themes, ${bad.length} bad ${JSON.stringify(bad.slice(0, 8))}`);
    check(n === 800 && !bad.length, `header badge shows whole inside the viewport at 641-1440 ${phase}: ${JSON.stringify(bad.slice(0, 8))}`);
  };
  const sample = async (sels) => {
    const out = {};
    for (const sel of sels) {
      try {
        out[sel] = await cdp.eval(CONTRAST_EXPR(sel));
      } catch (e) {
        out[sel] = null;
      }
    }
    return out;
  };
  async function tryWait(expr, timeout) {
    const t0 = Date.now();
    for (;;) {
      try {
        if (await cdp.eval(expr))
          return true;
      } catch (e) { /* page busy, retry */ }
      if (Date.now() - t0 > (timeout || 8000))
        return false;
      await sleep(250);
    }
  }
  const gateText = () => cdp.eval(`document.querySelector('#gate').textContent`);
  const gateVisible = () => cdp.eval(`document.querySelector('#gate').innerText`);

  try {
    // Phase A: pass missing the whole way.
    const home = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-home-'));
    const cfg = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-cfg-'));
    const port = 18772;
    let R = await startRelay(home, cfg, '/nonexistent-hush-pass-helper', null, port);
    let proc = R.proc;
    let s = await sess(port, R.headers);
    check(s.pass_available === false, 'virgin session reports pass unavailable');
    check(s.restart_lost_login === false, 'virgin session has no restart-loss flag');

    await goto(port);
    let t = await gateText();
    check(!t.includes('did not survive the restart'), 'virgin splash shows no restart note');
    check(!t.includes('Detecting identity'), 'splash hides the Detecting line');
    Object.assign(contrasts, { splash: await sample(['#begin', '#profile-btn', '#nav-toggle', '#rail-toggle']) });
    await cdp.waitFor(`/listening/.test(document.querySelector('header #badge').textContent)`, 'badge listening before login');
    if (VIEW_W === 1440) await badgeFit('before login', 'listening');
    const beginRatio = RATIO_OF(contrasts.splash['#begin']);
    check(beginRatio !== null && beginRatio >= 4.5, `BEGIN contrast ${JSON.stringify(contrasts.splash['#begin'])} < 4.5`);
    await cdp.shot('splash-begin');

    await cdp.click('#begin');
    await cdp.waitFor(`document.querySelector('#gate').textContent.includes('Use an existing key')`, 'landing');
    t = await gateText();
    check(!t.includes('did not survive the restart'), 'virgin landing shows no restart note');

    // O5: the help card uses plain office wording, no Nostr jargon and no
    // bare nsec1… token.
    await cdp.click('#show-help');
    await cdp.waitFor(`!!document.querySelector('#back-help')`, 'help card');
    t = await gateText();
    check(t.includes('signs you in with an identity key instead of a password'), 'help card uses plain wording');
    check(!t.includes('Nostr') && !t.includes('nsec1…'), 'help card has no Nostr jargon or bare nsec1…');
    // Pre-walk F10: keys are named in plain words with the code in brackets.
    check(t.includes('public key (npub)') && t.includes('secret key (nsec)'), 'help card names the keys in plain words');
    await cdp.click('#back-help');
    await cdp.waitFor(`!!document.querySelector('#use-id')`, 'landing after help');
    await cdp.click('#use-id');
    await cdp.waitFor(`!!document.querySelector('#nsec-in')`, 'import card');
    t = await gateText();
    check(await cdp.eval(`document.querySelector('label[for="nsec-in"]').textContent`) === 'Secret key (nsec)', 'import label is plain');
    check(t.includes('secret key (nsec)'), 'import card names the key in plain words');
    // Pre-walk r4 (claim trace): the import card promises only what the
    // flow does. Import loads the key in memory (nothing is saved before
    // the backup step) and no public key is shown before that step.
    check(t.includes('Paste your secret key (nsec). Nothing is saved before the next step.') &&
      !/public key|npub/i.test(t), 'import card makes no public-key promise');
    const preview = await cdp.eval(`(() => { const i = document.querySelector('#nsec-in'); i.value = 'nsec1example';
      i.dispatchEvent(new Event('input')); return document.querySelector('#npub-preview').textContent; })()`);
    check(preview === 'Looks like a secret key (nsec).', `import preview makes no public-key promise: ${preview}`);
    await cdp.click('#back-import');
    await cdp.waitFor(`!!document.querySelector('#create-id')`, 'landing after import');

    await cdp.click('#create-id');
    await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup');
    const checked = await cdp.eval(`document.querySelector('#save-pass').checked`);
    check(checked === false, 'backup checkbox renders unchecked without pass');
    const disabled = await cdp.eval(`document.querySelector('#save-pass').disabled`);
    check(disabled === true, 'backup checkbox is disabled without pass');
    Object.assign(contrasts, { backup: await sample(['#ack-key', '#reveal-key', '#copy-key']) });
    t = await gateText();
    check(!t.includes('Checked to save'), 'backup label drops the Checked-to-save line without pass');
    check(!t.includes('Uncheck the box'), 'backup drops the Uncheck line without pass');
    check(t.split(NOPASS_REASON).length === 2, 'backup shows the no-pass reason exactly once');
    check(!REASON_ECHO.test(t.split(NOPASS_REASON).join(' ')), 'backup states the no-pass reason only once');
    check(!/\bpass\b/i.test(await gateVisible()), 'backup shows no bare pass without pass');
    check(t.includes(NEVER_SHARE), 'backup keeps the never-share warning without pass');
    check((t.match(/Copy it now/g) || []).length === 1, 'backup says Copy it now once without pass');
    // Pre-walk: the disabled label dims with its checkbox.
    const offLabel = await cdp.eval(`(() => { const l = document.querySelector('#save-pass').closest('label');
      return l.classList.contains('is-off') && parseFloat(getComputedStyle(l).opacity) < 0.8; })()`);
    check(offLabel === true, 'backup dims the disabled save label without pass');
    check(await cdp.eval(`!document.querySelector('#gate details')`), 'backup shows no how-to-find line without pass');
    await cdp.shot('backup-nopass');

    await cdp.click('#ack-key');
    await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe step');
    Object.assign(contrasts, { setup: await sample(['#do-vibe']) });
    await cdp.eval(`document.querySelector('#vibe-name').value = 'CDPHIVE'`);
    await cdp.click('#do-vibe');
    await cdp.waitFor(`!!document.querySelector('#meet-payne')`, 'payne step');
    Object.assign(contrasts.setup, await sample(['#meet-payne']));
    await cdp.click('#meet-payne');
    await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive');
    const vibeSub = await cdp.eval(`document.querySelector('#vibe-sub').textContent`);
    check(vibeSub === 'CDPHIVE', `header shows the hive name while logged in, got ${vibeSub}`);
    // Pin the field-office theme through the app's own stored-choice boot
    // path: the O1 contrast rules live under that theme, so measure them
    // there. (The server profile allowlist has no field-office entry.)
    await cdp.eval(`localStorage.setItem('hush-theme','field-office')`);
    await cdp.send('Page.reload', {});
    await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive after theme pin');
    await cdp.waitFor(`document.documentElement.getAttribute('data-theme')==='field-office'`, 'field-office theme');
    Object.assign(contrasts, { hive: await sample(['#send', '#profile-btn', '#nav-toggle', '#rail-toggle', '#vibe-sub']) });

    // Pre-walk r3 (Gauge P2-1): the drawer fade cue must not latch. The
    // cue (::after) and its flex row gap are left out of the overflow test,
    // so a drawer that fits by even 1px drops the class, and dropping it
    // leaves nothing but the bottom padding to scroll. Drive the drawer
    // height across the edge around a fixed probe child: short by 3px (cue
    // on), then 1px spare (cue off), short again, then 6px spare (cue off).
    // r4 (Ops FAIL-A): "free" is measured from the last row's bottom, not
    // scrollHeight, since padding-only overflow no longer sets the cue.
    const fade = await cdp.eval(`(async () => {
      const d = document.querySelector('#fo-drawer');
      const frames = () => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(r, 60))));
      if (!d.clientHeight && document.querySelector('#nav-toggle')) { document.querySelector('#nav-toggle').click(); await frames(); }
      const probe = document.createElement('div');
      probe.style.cssText = 'height:400px;flex:none';
      d.appendChild(probe);
      const oldH = d.style.height, oldMax = d.style.maxHeight, oldMin = d.style.minHeight, oldFlex = d.style.flex;
      d.style.maxHeight = 'none'; d.style.minHeight = '0'; d.style.flex = 'none';
      d.style.height = '200px'; await frames();
      d.classList.remove('is-overflowing');
      const lastRow = () => { const top = d.getBoundingClientRect().top + d.clientTop - d.scrollTop; let m = 0;
        for (const c of d.children) if (c.getClientRects().length) m = Math.max(m, c.getBoundingClientRect().bottom - top); return m; };
      const content = Math.round(lastRow());
      const pb = parseFloat(getComputedStyle(d).paddingBottom) || 0;
      const extra = d.offsetHeight - d.clientHeight; // borders (+ padding when border-box)
      const out = [];
      for (const free of [-3, 1, -3, 6]) {
        d.style.height = (content + free + (getComputedStyle(d).boxSizing === 'border-box' ? extra : 0)) + 'px';
        await frames();
        out.push({ free, got: d.clientHeight - content, on: d.classList.contains('is-overflowing'),
          scroll: d.scrollHeight - d.clientHeight, pb });
      }
      probe.remove();
      d.style.height = oldH; d.style.maxHeight = oldMax; d.style.minHeight = oldMin; d.style.flex = oldFlex;
      await frames();
      return { content, out };
    })()`, true);
    console.log('drawer fade edge: ' + JSON.stringify(fade));
    for (const r of fade.out) {
      check(r.got === r.free, `drawer fade probe sized to ${r.free}px free, got ${r.got}`);
      if (r.free < 0)
        check(r.on, `drawer fade cue shows when content overflows by ${-r.free}px`);
      else {
        check(!r.on, `drawer fade cue clears when the drawer fits with ${r.free}px spare (no latch)`);
        check(r.scroll <= r.pb + 1, `drawer that fits leaves nothing but its ${r.pb}px bottom padding to scroll, got ${r.scroll}px`);
      }
    }

    // Pre-walk r4 (Ops FAIL-A): only bottom padding overflowing must not
    // set the fade. A probe row is sized so the last row ends exactly at
    // the client box bottom at 1440x900 (Ops's default-window case), and
    // at 375x778, then the window grows 1px at a time through the padding
    // band: 1440 at h 900-908 and 375 at h 778-792. There scrollHeight
    // still exceeds clientHeight (padding cut) but every row shows, so the
    // class must stay off; 2px shorter than the start, a row is cut and
    // the class must be on. Viewport restored after.
    const fadeBand = [];
    for (const [bw, h0, h1] of [[1440, 900, 908], [375, 778, 792]]) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: bw, height: h0, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
      const prep = await cdp.eval(`(async () => {
        const d = document.querySelector('#fo-drawer');
        const frames = () => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(r, 60))));
        if (!d.clientHeight && document.querySelector('#nav-toggle')) { document.querySelector('#nav-toggle').click(); await frames(); }
        let probe = document.querySelector('#fade-band-probe');
        if (!probe) { probe = document.createElement('div'); probe.id = 'fade-band-probe'; probe.style.cssText = 'height:0;flex:none'; d.appendChild(probe); }
        probe.style.height = '0px'; await frames();
        const last = () => { const top = d.getBoundingClientRect().top + d.clientTop - d.scrollTop; let m = 0;
          for (const c of d.children) if (c.getClientRects().length) m = Math.max(m, c.getBoundingClientRect().bottom - top); return m; };
        // Rows may sit on an auto margin, so grow the probe until the last
        // row reaches the client bottom.
        for (let i = 0; i < 200 && last() < d.clientHeight - 0.5; i++) {
          probe.style.height = (parseFloat(probe.style.height) + Math.max(1, Math.round(d.clientHeight - last()))) + 'px'; await frames(); }
        return { ch: d.clientHeight, last: last(), probe: probe.style.height, open: d.clientHeight > 0 };
      })()`, true);
      check(prep.open && Math.abs(prep.last - prep.ch) <= 1, `fade band probe puts the last row at the client bottom at ${bw}x${h0}: ${JSON.stringify(prep)}`);
      for (let h = h0 - 2; h <= h1; h++) {
        await cdp.send('Emulation.setDeviceMetricsOverride', { width: bw, height: h, deviceScaleFactor: 1, mobile: false });
        const r = await cdp.eval(`(async () => { const d = document.querySelector('#fo-drawer');
          await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(r, 60))));
          const top = d.getBoundingClientRect().top + d.clientTop - d.scrollTop; let m = 0;
          for (const c of d.children) if (c.getClientRects().length) m = Math.max(m, c.getBoundingClientRect().bottom - top);
          return { sh: d.scrollHeight, ch: d.clientHeight, last: Math.round(m * 10) / 10, on: d.classList.contains('is-overflowing') }; })()`, true);
        fadeBand.push(Object.assign({ w: bw, h }, r));
      }
      await cdp.eval(`(() => { const p = document.querySelector('#fade-band-probe'); if (p) p.remove(); })()`);
    }
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
    if (VIEW_W > 640) await cdp.eval(`(() => { const d = document.querySelector('#fo-drawer'); d.scrollTop = 0; })()`);
    console.log('drawer fade padding band: ' + JSON.stringify(fadeBand.map((r) => `${r.w}x${r.h} sh${r.sh} ch${r.ch} last${r.last} ${r.on ? 'on' : 'off'}`)));
    for (const [bw, h0] of [[1440, 900], [375, 778]]) {
      const rows = fadeBand.filter((r) => r.w === bw);
      const cut = rows.find((r) => r.h === h0 - 2);
      check(cut && cut.last > cut.ch + 1 && cut.on, `drawer fade shows when a row is cut by 2px at ${bw}x${h0 - 2}: ${JSON.stringify(cut)}`);
      const band = rows.filter((r) => r.h >= h0);
      check(band.filter((r) => r.sh > r.ch + 1).length >= 5, `padding band exercised at ${bw} (scrollHeight > clientHeight): ${JSON.stringify(band)}`);
      for (const r of band)
        check(!r.on && r.last <= r.ch + 1, `drawer fade stays off when only padding overflows at ${bw}x${r.h}: ${JSON.stringify(r)}`);
    }

    // Pre-walk r3 (Ops F2): every drawer stat, including the build stamp,
    // shows whole inside the clip wrapper (no glyph cut at its right edge).
    // CI's shallow checkout stamps only the short SHA ("v28f9c24"; r4 prints it once),
    // so the check also swaps in a real tagged-build stamp of the length Ops
    // saw on a release build, plus a longer one, and measures each.
    const statsFit = await cdp.eval(`(() => {
      const clip = document.querySelector('#stats .stats-clip');
      if (!clip) return { err: 'no stats clip' };
      const c = clip.getBoundingClientRect();
      const measure = () => [...clip.querySelectorAll('.stat')].map((s) => {
        const r = document.createRange(); r.selectNodeContents(s.firstChild || s);
        const bad = [...r.getClientRects()].filter((q) => q.width > 0 &&
          (q.left < c.left - 0.5 || q.right > c.right + 0.5 || q.top < c.top - 0.5 || q.bottom > c.bottom + 0.5));
        return { t: s.textContent, bad: bad.map((q) => [Math.round(q.left), Math.round(q.right), Math.round(c.left), Math.round(c.right)]) };
      });
      const live = measure();
      const last = clip.querySelector('.stat:last-child');
      const keep = last.textContent;
      const fixture = [];
      for (const t of ['v0.0.1-697-g4c1f2594', 'v0.0.1-9999-g0123456789ab',
        'v0.0.1-9999-g0123456789abcdef0123456789abcdef01234567']) {
        last.textContent = t;
        const m = measure();
        fixture.push(m[m.length - 1]);
      }
      last.textContent = keep;
      return { live, fixture };
    })()`);
    check(!statsFit.err, statsFit.err);
    const stamp = statsFit.live.length ? statsFit.live[statsFit.live.length - 1].t : '';
    console.log('drawer stamp: ' + JSON.stringify(stamp));
    check(/^v\S+( \S+)?$/.test(stamp), `drawer shows the build stamp as its last stat, got ${JSON.stringify(stamp)}`);
    // Pre-walk r4: the SHA is printed once (CI: "v7fc2acd", not "v7fc2acd 7fc2acd").
    const stampWords = stamp.split(' ');
    check(stampWords.length < 2 || !stampWords[0].includes(stampWords[1]), `drawer stamp prints the build SHA once, got ${JSON.stringify(stamp)}`);
    check(statsFit.fixture.length === 3 && statsFit.fixture[0].t === 'v0.0.1-697-g4c1f2594',
      `real-length stamp fixture measured: ${JSON.stringify(statsFit.fixture)}`);
    for (const s of statsFit.live.concat(statsFit.fixture))
      check(!s.bad.length, `drawer stat ${JSON.stringify(s.t)} is cut by the clip edge ${JSON.stringify(s.bad)}`);

    // Pre-walk r3 (Ops F1): the header badge carries its state in both
    // title and aria-label; at <= 480px it is a dot, elsewhere unclipped.
    const bdg = await cdp.eval(`(() => { const b = document.querySelector('header #badge'); const cs = getComputedStyle(b);
      return { text: b.textContent, title: b.title, aria: b.getAttribute('aria-label'), role: b.getAttribute('role'),
        sw: b.scrollWidth, cw: b.clientWidth, w: b.getBoundingClientRect().width, fs: parseFloat(cs.fontSize) }; })()`);
    console.log('header badge: ' + JSON.stringify(bdg));
    check(bdg.role === 'img' && !!bdg.aria && bdg.aria === bdg.title && bdg.aria.includes(bdg.text) && bdg.text.length > 0,
      `badge state is in title and aria-label: ${JSON.stringify(bdg)}`);
    check(/listening/.test(bdg.aria), `logged-in badge label names the relay state: ${bdg.aria}`);
    if (VIEW_W <= 480)
      check(bdg.fs === 0 && bdg.w <= 16, `badge is a dot at ${VIEW_W}px: ${JSON.stringify(bdg)}`);
    else if (VIEW_W > 640) // 481-640 keeps the 95px cap with an ellipsis
      check(bdg.sw <= bdg.cw, `badge text is not clipped at ${VIEW_W}px: ${JSON.stringify(bdg)}`);
    // Pre-walk r3/r4 (P3-D, Ops FAIL-B): CI runs at 1440, so narrow the
    // viewport to 375 here to check the badge lamp (R07): green while
    // listening, amber when not (R08), 12px with no text, seated at the
    // header's right padding, in field-office and dark; then restore.
    if (VIEW_W > 480) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: 375, height: 800, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
    }
    const dot = await cdp.eval(`(() => { const b = document.querySelector('header #badge'); const h = document.querySelector('header');
      const html = document.documentElement; const theme = html.getAttribute('data-theme');
      const keep = [b.textContent, b.title, b.getAttribute('aria-label'), b.className];
      // Lamp colour: the gradient's body stop (the one before the dark
      // edge), or the flat background colour when there is no gradient.
      const colour = () => { const img = getComputedStyle(b).backgroundImage; const m = img.match(/rgb\\(\\d+, \\d+, \\d+\\)/g) || [];
        return m.length >= 2 ? m[m.length - 2] : getComputedStyle(b).backgroundColor; };
      const m = () => { const cs = getComputedStyle(b); const r = b.getBoundingClientRect(); const hr = h.getBoundingClientRect();
        return { w: r.width, h: r.height, fs: parseFloat(cs.fontSize), fill: colour(), rim: cs.borderTopColor, rimW: parseFloat(cs.borderTopWidth),
          glow: cs.boxShadow, seat: Math.round(hr.right - r.right), mid: Math.round((r.top + r.bottom) / 2 - (hr.top + hr.bottom) / 2) }; };
      const out = {};
      for (const th of ['field-office', 'dark']) {
        html.setAttribute('data-theme', th);
        badge(true, 'listening', 'Relay listening on port 1');
        const ok = m();
        badge(false, 'relay unreachable');
        const bad = Object.assign(m(), { aria: b.getAttribute('aria-label'), title: b.title, role: b.getAttribute('role') });
        out[th] = { ok, bad };
      }
      html.setAttribute('data-theme', theme);
      b.textContent = keep[0]; b.title = keep[1]; b.setAttribute('aria-label', keep[2]); b.className = keep[3];
      return out; })()`);
    console.log('badge lamp at 375: ' + JSON.stringify(dot));
    const rgb = (c) => (String(c).match(/\d+(\.\d+)?/g) || []).slice(0, 3).map(Number);
    const green = (c) => { const [r, g, b] = rgb(c); return g > r + 40 && g > b + 40; };
    const amber = (c) => { const [r, g, b] = rgb(c); return r >= g && g > b + 40 && r > 150; };
    for (const th of ['field-office', 'dark']) {
      const { ok, bad } = dot[th];
      for (const [st, v] of [['ok', ok], ['bad', bad]]) {
        check(v.fs === 0 && v.w >= 10 && v.w <= 16 && v.h >= 10 && v.h <= 16, `${st} badge is a ~12px lamp with no text at 375 (${th}): ${JSON.stringify(v)}`);
        check(v.rimW >= 1 && /rgb/.test(v.glow) && v.glow !== 'none', `${st} badge lamp has a rim and glow (${th}): ${JSON.stringify(v)}`);
        check(v.seat >= 0 && v.seat <= 20 && Math.abs(v.mid) <= 4, `${st} badge lamp sits at the header's right edge (${th}): ${JSON.stringify(v)}`);
      }
      check(green(ok.fill), `listening badge lamp is green at 375 (${th}): ${JSON.stringify(ok)}`);
      check(amber(bad.fill), `not-listening badge lamp is amber at 375 (${th}): ${JSON.stringify(bad)}`);
      check(bad.role === 'img' && bad.aria === 'relay unreachable' && bad.title === 'relay unreachable', `bad badge keeps its state text (${th}): ${JSON.stringify(bad)}`);
    }
    if (VIEW_W > 480) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
    }
    await cdp.shot('hive-fo');
    if (VIEW_W === 1440) await badgeFit('after login', 'npub1');

    // Pre-walk r4 (Gauge B1), no pass installed: the robot editor never
    // claims to save the key, on Raise or any Edit path.
    await cdp.waitFor(`!!document.querySelector('#robot-list .robot-card[data-slug="coach"]')`, 'robot cards');
    check((await sess(port, R.headers)).pass_available === false, 'phase A relay reports no pass');
    checkRobotPassHidden(await robotPassBox('Raise a robot', `document.querySelector('#raise-agent').click()`), 'no pass');
    checkRobotPassHidden(await robotPassBox('Edit locked robot', editRobot('coach')), 'no pass');
    checkRobotPassHidden(await robotPassBox('Edit Major', editRobot('sgt-major-payne')), 'no pass');
    await cdp.eval(`document.querySelector('#agent-close').click()`);
    await cdp.click('#hive-close');
    await cdp.waitFor(`!!document.querySelector('#hive-leave.show')`, 'leave chooser');
    Object.assign(contrasts, { leave: await sample(['#leave-exit', '#leave-close', '#leave-cancel']) });
    await cdp.click('#leave-cancel');

    await cdp.click('#profile-btn');
    await cdp.waitFor(`document.querySelector('#profile.show')`, 'profile drawer');
    // Pre-walk r3/r4 (P3-D, R15): the four profile actions pair into two
    // rows in DOM order Save, Logout / Copy, Close (no lone Close row).
    const profRows = await cdp.eval(`(() => [...document.querySelectorAll('#profile .actions .btn')].map((e) =>
      [e.id, Math.round(e.getBoundingClientRect().top)]))()`);
    const rowTops = [...new Set(profRows.map((r) => r[1]))];
    check(profRows.map((r) => r[0]).join(',') === 'profile-save,profile-logout,profile-copy,profile-close' &&
      rowTops.length === 2 && profRows[0][1] === profRows[1][1] && profRows[2][1] === profRows[3][1],
      `profile actions pair into two rows: ${JSON.stringify(profRows)}`);
    // Pre-walk r3 (Ops 3): the profile picture field is labelled "Picture".
    check(await cdp.eval(`document.querySelector('label[for="prof-avatar"]').textContent`) === 'Picture', 'profile picture label reads Picture');
    // Pre-walk r4 (claim trace): no code reads the picture input and Save
    // profile sends no picture, so the field is disabled and says so.
    const pic = await cdp.eval(`({ disabled: document.querySelector('#prof-avatar').disabled,
      help: document.querySelector('#prof-avatar-status').textContent })`);
    check(pic.disabled && pic.help === 'Not available yet: Hush does not save a profile picture.',
      `profile picture field does not promise a saved picture: ${JSON.stringify(pic)}`);
    await cdp.click('#profile-logout');
    await cdp.waitFor(`document.querySelector('#gate.show') && document.querySelector('#gate').textContent.includes('Use an existing key')`, 'post-logout landing');
    t = await gateText();
    check(!t.includes('did not survive the restart'), 'post-logout landing shows no restart note');
    const subAfterLogout = await cdp.eval(`document.querySelector('#vibe-sub').textContent`);
    check(subAfterLogout === 'local hive mind', `header resets after logout, got ${subAfterLogout}`);
    s = await sess(port, R.headers);
    check(s.restart_lost_login === false, 'logout sets no restart-loss flag');
    Object.assign(contrasts, { landing: await sample(['#create-id', '#use-id']) });
    const createLogout = contrasts.landing['#create-id'];
    check(RATIO_OF(createLogout) !== null, 'create-key button renders');
    await cdp.shot('logout-landing');

    // Fresh loads boot the field-office theme until a POST applies the
    // saved profile theme, so re-enter the landing that way to measure
    // the create-key button under the fixed rules.
    await cdp.eval(`localStorage.setItem('hush-theme','field-office')`);
    await cdp.send('Page.reload', {});
    await cdp.waitFor(`!!document.querySelector('#begin')`, 'splash again');
    await cdp.click('#begin');
    await cdp.waitFor(`document.querySelector('#gate').textContent.includes('Use an existing key')`, 'landing again');
    Object.assign(contrasts, { landingFO: await sample(['#create-id', '#use-id']) });
    const createFO = RATIO_OF(contrasts.landingFO['#create-id']);
    check(createFO !== null && createFO >= 4.5, `create-key contrast ${JSON.stringify(contrasts.landingFO['#create-id'])} < 4.5`);

    await stopRelay(proc);
    R = await startRelay(home, cfg, '/nonexistent-hush-pass-helper', null, port);
    proc = R.proc;
    s = await sess(port, R.headers);
    check(s.restart_lost_login === true, 'restart without pass sets the restart-loss flag');
    check(s.logged_in === false && s.has_vibe === true, 'restart keeps vibe without login');
    await goto(port);
    await tryWait(`document.querySelector('#gate').textContent.includes('did not survive the restart')`, 8000);
    t = await gateText();
    check(t.includes('did not survive the restart'), 'restart note shows after a pass-less restart');
    check(t.includes('your identity key (starts with nsec1)'), 'restart note uses office wording');
    await cdp.shot('restart-landing');
    await cdp.click('#begin');
    await tryWait(`document.querySelector('#gate').textContent.includes('did not survive the restart')`, 8000);
    t = await gateText();
    check(t.includes('did not survive the restart'), 'landing repeats the restart note');
    await stopRelay(proc);

    // Phase B: pass works, checkbox defaults on.
    const home2 = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-home2-'));
    const cfg2 = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-cfg2-'));
    const fakeDir = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-fake-'));
    const fakeHelper = path.join(__dirname, 'fake-pass.sh');
    R = await startRelay(home2, cfg2, fakeHelper, fakeDir, port);
    proc = R.proc;
    s = await sess(port, R.headers);
    check(s.pass_available === true, 'fake pass helper reports available');
    await goto(port);
    await cdp.click('#begin');
    await cdp.waitFor(`document.querySelector('#gate').textContent.includes('Use an existing key')`, 'landing 2');
    await cdp.click('#create-id');
    await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup 2');
    const checked2 = await cdp.eval(`document.querySelector('#save-pass').checked`);
    check(checked2 === true, 'backup checkbox renders checked with pass');
    t = await gateText();
    check(t.includes('Checked to save it in your password manager.'), 'backup keeps the Checked-to-save line with pass');
    // Pre-walk F10: the retrieve command sits only behind "How to find it later".
    const howto = await cdp.eval(`(() => { const d = document.querySelector('#gate details.howto');
      const l = document.querySelector('#save-pass').closest('label');
      return !!d && !d.open && d.querySelector('summary').textContent === 'How to find it later' &&
        d.textContent.includes('pass show hush/identity/nsec') && !l.textContent.includes('pass show'); })()`);
    check(howto === true, 'backup keeps the retrieve command behind a closed details line with pass');
    check(await cdp.eval(`!document.querySelector('#save-pass').closest('label').classList.contains('is-off')`), 'backup label is not dimmed with pass');
    check(t.includes(NEVER_SHARE), 'backup keeps the never-share warning with pass');
    check(t.includes('Uncheck the box'), 'backup keeps the Uncheck line with pass');
    check(!t.includes(NOPASS_REASON), 'backup shows no no-pass reason with pass');
    const disabled2 = await cdp.eval(`document.querySelector('#save-pass').disabled`);
    check(disabled2 === false, 'backup checkbox stays enabled with pass');
    await cdp.shot('backup-withpass');

    // Pre-walk r4 (Gauge B1), pass available: the label and its closed
    // how-to show on Raise (the create path saves the key) and on no Edit
    // path (update never saves a key).
    await cdp.click('#ack-key');
    await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe step 2');
    await cdp.eval(`document.querySelector('#vibe-name').value = 'PASSHIVE'`);
    await cdp.click('#do-vibe');
    await cdp.waitFor(`!!document.querySelector('#meet-payne')`, 'payne step 2');
    await cdp.click('#meet-payne');
    await cdp.waitFor(`!!document.querySelector('#hive.show')`, 'hive 2');
    await cdp.waitFor(`!!document.querySelector('#robot-list .robot-card[data-slug="coach"]')`, 'robot cards 2');
    const raise2 = await robotPassBox('Raise a robot', `document.querySelector('#raise-agent').click()`);
    console.log('robot pass box (raise, pass): ' + JSON.stringify(raise2));
    check(raise2.label && raise2.howto && !raise2.open && raise2.text === ROBOT_PASS_LABEL && raise2.cmd,
      `robot pass box and closed how-to show on Raise with pass: ${JSON.stringify(raise2)}`);
    const locked2 = await robotPassBox('Edit locked robot', editRobot('coach'));
    checkRobotPassHidden(locked2, 'update saves no key');
    await cdp.click('#agent-clone');
    await cdp.waitFor(`!!document.querySelector('#robot-list .robot-card[data-slug="coach-copy"]')`, 'cloned robot');
    checkRobotPassHidden(await robotPassBox('Edit robot', editRobot('coach-copy')), 'update saves no key');
    checkRobotPassHidden(await robotPassBox('Edit Major', editRobot('sgt-major-payne')), 'update saves no key');
    const raise3 = await robotPassBox('Raise a robot', `document.querySelector('#raise-agent').click()`);
    check(raise3.label && raise3.howto, `robot pass box shows again on Raise after an Edit: ${JSON.stringify(raise3)}`);
    await cdp.eval(`document.querySelector('#agent-close').click()`);
    await stopRelay(proc);

    fs.writeFileSync(path.join(ART, `${TAG}-contrast-${VIEW_W}.json`),
      JSON.stringify(contrasts, null, 1) + '\n');
    console.log(NO_ASSERT ? `restart UI shots done, no asserts (${TAG} ${VIEW_W})`
                          : `restart UI behaviour ok (${TAG} ${VIEW_W})`);
  } finally {
    for (const p of relays.splice(0)) {
      try { p.kill('SIGTERM'); } catch (e) { /* gone */ }
    }
    try { chromeProc.kill('SIGTERM'); } catch (e) { /* gone */ }
  }
}

main().catch((e) => fail(e.message + '\n' + (e.stack || '').split('\n').slice(0, 4).join('\n')));
