// check_restart_ui.cjs: behaviour proof for the ID-1 restart/backup UI.
// Drives headless system Chrome over CDP with the Node standard library
// only (no npm packages, nothing installed). Every assertion below reads
// rendered behaviour (checkbox state, visible copy, header text, painted
// contrast), never page source. Fails loudly when node, Chrome, the relay,
// or any behaviour is missing.
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN,
// HUSH_TEST_WAIT_S (Chrome DevTools ready deadline, default 30),
// ID1_TAG (shot filename tag, default "after"), ID1_NO_ASSERT=1 (navigate
// and shoot without asserting, for before/after pairs), ID1_VIEW_W
// (viewport width, default 1440), ID1_ART (shot/JSON dir),
// ID1_CHROME_ONLY=1 (launch Chrome and wait for DevTools, then exit —
// used by the #265 wait pin).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

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
// It must appear exactly once in visible text (not textContent, which still
// sees a hidden node). Nothing else on that screen may restate it, including
// a paraphrase that does not reuse the original phrases.
const NOPASS_REASON = "Hush can't save this key on this computer, so keep your copy somewhere safe.";
const REASON_ECHO = /can.?t save|cannot save|can.?t be saved|cannot be saved|not installed|unavailable|not available|somewhere safe|nothing to save|on this computer|keep your copy|keep a copy|unable to save|will not save|won'?t save|no way to save/i;

// True when visible backup text restates the no-pass reason after the one
// canonical sentence and the known non-reason controls are removed.
function noPassRestated(visible) {
  let rest = String(visible || '').split(NOPASS_REASON).join('\n');
  const allowed = [
    'Save to password manager',
    'I saved it',
    NEVER_SHARE,
    'This key is your account. Copy it now. Hush cannot recover it if you lose it.',
    'Your unique identity key has been created',
    'Your identity key has been imported',
    'Copy value',
    'Reveal',
    'Hide',
    '2 / 4'
  ];
  for (const phrase of allowed)
    rest = rest.split(phrase).join(' ');
  return REASON_ECHO.test(rest);
}

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

function testWaitMs() {
  const raw = process.env.HUSH_TEST_WAIT_S;
  const sec = raw === undefined || raw === '' ? 30 : Number(raw);
  if (!Number.isFinite(sec) || sec <= 0)
    fail('HUSH_TEST_WAIT_S must be a positive number of seconds');
  return Math.floor(sec * 1000);
}

/* Waits until Chrome's remote-debugging HTTP port answers /json/list, or
 * stderr prints the classic DevTools line. Bound by deadlineMs. Returns
 * { ok, httpBase, stderr, detail }. */
async function waitChromeDevtools(chromeProc, port, deadlineMs) {
  let stderr = '';
  let exited = null;
  const onData = (d) => { stderr += d.toString(); };
  const onExit = (code, signal) => { exited = { code, signal }; };
  chromeProc.stderr.on('data', onData);
  chromeProc.once('exit', onExit);
  const t0 = Date.now();
  const httpBase = `http://127.0.0.1:${port}`;
  try {
    while (Date.now() - t0 < deadlineMs) {
      if (exited) {
        return {
          ok: false,
          httpBase: null,
          stderr,
          detail: `Chrome exited early code=${exited.code} signal=${exited.signal}`,
        };
      }
      const m = stderr.match(/DevTools listening on (ws:\/\/\S+)/);
      if (m) {
        const fromLine = m[1].replace(/^ws:\/\//, 'http://').replace(/\/devtools.*$/, '');
        return { ok: true, httpBase: fromLine, stderr, detail: 'stderr' };
      }
      try {
        const res = await fetch(httpBase + '/json/list', {
          signal: AbortSignal.timeout(400),
        });
        if (res.ok) {
          const targets = await res.json();
          if (Array.isArray(targets))
            return { ok: true, httpBase, stderr, detail: 'http' };
        }
      } catch (e) {
        /* not listening yet */
      }
      await sleep(100);
    }
    return {
      ok: false,
      httpBase: null,
      stderr,
      detail: `deadline ${deadlineMs}ms elapsed`,
    };
  } finally {
    chromeProc.stderr.off('data', onData);
  }
}

/* Spawns headless Chrome on a fixed free port. One relaunch if the first
 * attempt hits the DevTools-ready deadline without an early exit (startup
 * race). */
async function launchChromeReady(chrome, track, deadlineMs) {
  let last = null;
  for (let attempt = 1; attempt <= 2; attempt++) {
    const port = await freePort();
    const userDir = fs.mkdtempSync(path.join(os.tmpdir(), 'id1-chrome-'));
    /* CI often sets a junk DBUS_SESSION_BUS_ADDRESS; Chrome then logs
     * dbus parse errors while starting. Drop it for the Chrome child. */
    const chromeEnv = Object.assign({}, process.env);
    delete chromeEnv.DBUS_SESSION_BUS_ADDRESS;
    const chromeProc = track(spawn(chrome,
      ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
        '--disable-dev-shm-usage', '--disable-software-rasterizer',
        `--remote-debugging-port=${port}`, `--user-data-dir=${userDir}`,
        'about:blank'],
      { stdio: ['ignore', 'ignore', 'pipe'], env: chromeEnv }));
    const result = await waitChromeDevtools(chromeProc, port, deadlineMs);
    if (result.ok) {
      return { chromeProc, userDir, port, httpBase: result.httpBase, stderr: result.stderr };
    }
    last = result;
    try { chromeProc.kill('SIGTERM'); } catch (e) { /* gone */ }
    try { fs.rmSync(userDir, { recursive: true, force: true }); } catch (e) { /* best effort */ }
    const idx = liveProcs.indexOf(chromeProc);
    if (idx >= 0)
      liveProcs.splice(idx, 1);
    /* Early exit is a hard failure — retrying will not help a missing binary. */
    if (result.detail.startsWith('Chrome exited early'))
      break;
  }
  fail('Chrome printed no DevTools URL: ' +
       (last ? `${last.detail}; ${last.stderr.slice(0, 400)}` : 'no attempt'));
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


const LOG_PROBE_FN = `function(th, force) {
  document.documentElement.setAttribute('data-theme', th);
  const list = document.querySelector('#fo-roster-list');
  const pane = document.querySelector('#roster-pane');
  const keep = list.innerHTML;
  const paneMin = pane.style.minHeight;
  while (list.querySelectorAll('.fo-person').length < 8) {
    const b = document.createElement('div');
    b.className = 'fo-person static';
    const n = document.createElement('span');
    n.className = 'fo-person-name';
    n.textContent = 'Person ' + list.querySelectorAll('.fo-person').length;
    const k = document.createElement('span');
    k.className = 'fo-person-kind';
    k.textContent = 'robot';
    b.appendChild(n);
    b.appendChild(k);
    list.appendChild(b);
  }
  if (force) pane.style.minHeight = '900px';
  const log = document.querySelector('#fo-log-wrap');
  const lr = log.getBoundingClientRect();
  const scroller = document.scrollingElement || document.documentElement;
  const before = scroller.scrollTop;
  scroller.scrollTop = before + 280;
  const moved = scroller.scrollTop > before + 40;
  scroller.scrollTop = before;
  const out = {
    people: list.querySelectorAll('.fo-person').length,
    logH: Math.round(lr.height),
    logTop: Math.round(lr.top),
    inView: lr.height > 0 && lr.top < window.innerHeight && lr.bottom > 0,
    docH: scroller.scrollHeight, inner: window.innerHeight, moved,
    overflowY: getComputedStyle(document.body).overflowY
  };
  list.innerHTML = keep;
  pane.style.minHeight = paneMin;
  return out;
}`;

async function main() {
  const chrome = resolveChrome();
  const track = (proc) => { liveProcs.push(proc); return proc; };
  const deadlineMs = testWaitMs();
  const launched = await launchChromeReady(chrome, track, deadlineMs);
  const chromeProc = launched.chromeProc;
  const userDir = launched.userDir;
  if (process.env.ID1_CHROME_ONLY === '1') {
    console.log('restart UI chrome ready (' + launched.httpBase +
                ', deadline ' + deadlineMs + 'ms)');
    try { chromeProc.kill('SIGTERM'); } catch (e) { /* gone */ }
    process.exit(0);
  }
  const httpBase = launched.httpBase;
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
        open: h.open, text: l.textContent.trim(), cmd: h.textContent.includes('pass ls hush/agents') && !h.textContent.includes('<robot-id>') };
    })()`);
  };
  const editRobot = (slug) => `document.querySelector('#robot-list .robot-card[data-slug="${slug}"] .robot-actions button').click()`;
  const checkRobotPassHidden = (st, why) =>
    check(!st.label && !st.howto, `robot pass box and how-to hidden on ${st.title} (${why}): ${JSON.stringify(st)}`);
  // Pre-walk r5 (Gauge P2-3): read the save_pass the UI actually sends.
  // The page's fetch is wrapped for one save: the POST /api/agent body is
  // kept as sent and the request is refused, so the fixture's roster does
  // not change. The pass box is forced to `checked` first, so a hidden but
  // checked box would leak true if the shown-and-checked rule broke.
  const savePassSent = async (label, openExpr, checked) => {
    await robotPassBox(label, openExpr);
    if (label === 'Raise a robot') {
      // Raise needs one worn skill. The armory repaints when the skill
      // catalog loads, so wait for live gems, then assign the first one.
      await cdp.waitFor(`document.querySelector('#skill-armory .skill-gem[aria-pressed="false"]:not(:disabled)') &&
        document.querySelector('#skill-watermark').textContent.startsWith('Assigned 0/')`, 'raise skill armory', 8000);
      await cdp.eval(`document.querySelector('#skill-armory .skill-gem[aria-pressed="false"]:not(:disabled)').click()`);
      await cdp.waitFor(`document.querySelector('#skill-watermark').textContent.startsWith('Assigned 1/')`, 'raise skill assigned', 8000);
    }
    await cdp.eval(`(() => {
      const p = document.querySelector('#agent-prompt');
      if (!p.value.trim()) { p.value = 'Check the save_pass request.'; p.dispatchEvent(new Event('input', { bubbles: true })); }
      if (!document.querySelector('.agent-provider-cb:checked')) document.querySelector('.agent-provider-cb[value="goose"]').click();
      document.querySelector('#agent-pass').checked = ${checked ? 'true' : 'false'};
      window.__hushSent = null;
      window.__hushFetch = window.fetch;
      window.fetch = (u, o) => {
        if (String(u).endsWith('/api/agent') && o && o.method === 'POST') {
          window.__hushSent = String(o.body);
          window.fetch = window.__hushFetch;
          return Promise.reject(new Error('test refused the save'));
        }
        return window.__hushFetch(u, o);
      };
      document.querySelector('#agent-save').click();
    })()`);
    const got = await cdp.waitFor(`window.__hushSent ? { raw: window.__hushSent } :
      (document.querySelector('#agent-err').textContent && document.querySelector('#agent-err').textContent !== 'Could not save that robot.' ?
        { err: document.querySelector('#agent-err').textContent  } : null)`, 'save_pass request on ' + label, 8000);
    if (!got.raw) fail(`robot save sent no request on ${label}: ${got.err}`);
    const raw = got.raw;
    await cdp.eval(`(() => { if (window.__hushFetch) window.fetch = window.__hushFetch;
      const c = document.querySelector('#agent-close'); if (c) c.click(); })()`);
    const body = JSON.parse(raw);
    return { title: label, save_pass: body.save_pass, action: body.action || 'create', box: checked };
  };
  const checkSavePass = (st, want, why) => {
    console.log('save_pass request: ' + JSON.stringify(st));
    check(st.save_pass === want, `robot save sends save_pass ${want} on ${st.title} with the box ${st.box ? 'checked' : 'unchecked'} (${why}): ${JSON.stringify(st)}`);
  };
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
    const port = await freePort();
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
    // #234: import promises the matching npub and shows the full key before save.
    check(t.includes('Paste your secret key (nsec). We show the matching public key (npub) before anything is saved.'),
      'import card promises matching npub before save');
    // Short junk (under the nsec1 length floor): refuse immediately, no API.
    await cdp.eval(`(() => { const i = document.querySelector('#nsec-in'); i.value = 'nsec1example';
      i.dispatchEvent(new Event('input')); })()`);
    await sleep(50);
    let previewBad = await cdp.eval(`document.querySelector('#npub-preview').textContent`);
    check(previewBad === 'That does not look like a valid secret key.',
      `import preview rejects short junk nsec: ${JSON.stringify(previewBad)}`);
    // Long junk hits preview API and must still refuse (not blank).
    await cdp.eval(`(() => { const i = document.querySelector('#nsec-in');
      i.value = 'nsec1exampleexampleexampleexampleexampleexampleexamplexx';
      i.dispatchEvent(new Event('input')); })()`);
    await sleep(500);
    previewBad = await cdp.eval(`document.querySelector('#npub-preview').textContent`);
    check(previewBad === 'That does not look like a valid secret key.',
      `import preview rejects long junk nsec via API: ${JSON.stringify(previewBad)}`);
    const knownNsec = 'nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5';
    const knownNpub = 'npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg';
    await cdp.eval(`(() => { const i = document.querySelector('#nsec-in'); i.value = ${JSON.stringify(knownNsec)};
      i.dispatchEvent(new Event('input')); })()`);
    await cdp.waitFor(`(document.querySelector('#npub-preview').textContent || '').includes(${JSON.stringify(knownNpub)})`,
      'import preview shows full matching npub', 5000);
    const previewOk = await cdp.eval(`document.querySelector('#npub-preview').textContent`);
    check(previewOk.includes('Matching public key (npub):') && previewOk.includes(knownNpub),
      `import preview shows full npub: ${previewOk}`);
    // F-A (Gauge P2-2): at 375 the full npub must not sidescroll the page
    // or overflow the gate card. Mutant dropping #npub-preview wrap CSS fails.
    await cdp.send('Emulation.setDeviceMetricsOverride',
      { width: 375, height: 800, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
    const faPin = await cdp.eval(`(() => {
      const doc = document.documentElement;
      const prev = document.querySelector('#npub-preview');
      const card = prev && prev.closest('.card');
      if (!prev || !card) return { err: 'missing preview/card' };
      const pr = prev.getBoundingClientRect();
      const cr = card.getBoundingClientRect();
      return {
        docSW: doc.scrollWidth, iw: window.innerWidth,
        prevSW: prev.scrollWidth, prevCW: prev.clientWidth,
        cardSW: card.scrollWidth, cardCW: card.clientWidth,
        prevRight: Math.round(pr.right * 10) / 10,
        cardRight: Math.round(cr.right * 10) / 10,
        hasNpub: (prev.textContent || '').includes(${JSON.stringify(knownNpub)})
      };
    })()`);
    check(faPin.hasNpub && !faPin.err,
      `F-A: full npub still showing at 375: ${JSON.stringify(faPin)}`);
    check(faPin.docSW <= faPin.iw,
      `F-A: document.scrollWidth <= innerWidth at 375: ${JSON.stringify(faPin)}`);
    check(faPin.prevSW <= faPin.prevCW + 1,
      `F-A: #npub-preview fits its box at 375: ${JSON.stringify(faPin)}`);
    check(faPin.cardSW <= faPin.cardCW + 1,
      `F-A: .gate .card fits at 375: ${JSON.stringify(faPin)}`);
    check(faPin.prevRight <= faPin.cardRight + 1,
      `F-A: preview right edge inside card at 375: ${JSON.stringify(faPin)}`);
    await cdp.send('Emulation.setDeviceMetricsOverride',
      { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await sleep(200);
    await cdp.click('#back-import');
    await cdp.waitFor(`!!document.querySelector('#create-id')`, 'landing after import');

    await cdp.click('#create-id');
    await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup');
    {
      const createdTitle = await gateText();
      check(createdTitle.includes('Your unique identity key has been created'),
        'create backup title says has been created');
      check(!createdTitle.includes('has been imported'),
        'create backup title is not the import variant');
    }
    const checked = await cdp.eval(`document.querySelector('#save-pass').checked`);
    check(checked === false, 'backup checkbox renders unchecked without pass');
    const disabled = await cdp.eval(`document.querySelector('#save-pass').disabled`);
    check(disabled === true, 'backup checkbox is disabled without pass');
    Object.assign(contrasts, { backup: await sample(['#ack-key', '#reveal-key', '#copy-key']) });
    t = await gateText();
    check(!t.includes('Checked to save'), 'backup label drops the Checked-to-save line without pass');
    check(!t.includes('Uncheck the box'), 'backup drops the Uncheck line without pass');
    const vis = await gateVisible();
    check(vis.split(NOPASS_REASON).length === 2, 'backup shows the no-pass reason exactly once');
    check(!noPassRestated(vis), 'backup does not restate the no-pass reason');
    check(!/\bpass\b/i.test(await gateVisible()), 'backup shows no bare pass without pass');
    check(t.includes(NEVER_SHARE), 'backup keeps the never-share warning without pass');
    check((t.match(/Copy it now/g) || []).length === 1, 'backup says Copy it now once without pass');
    // Pre-walk: the disabled label dims with its checkbox.
    const offLabel = await cdp.eval(`(() => { const l = document.querySelector('#save-pass').closest('label');
      return l.classList.contains('is-off') && parseFloat(getComputedStyle(l).opacity) < 0.8; })()`);
    check(offLabel === true, 'backup dims the disabled save label without pass');
    check(await cdp.eval(`!document.querySelector('#gate details')`), 'backup shows no how-to-find line without pass');
    await cdp.shot('backup-nopass');

    // No-pass "I saved it" must send save_pass false. The request is
    // recorded and still delivered, so setup continues.
    await cdp.eval(`(() => {
      window.__hushAck = null;
      window.__hushAckFetch = window.fetch;
      window.fetch = (u, o) => {
        try {
          if (o && o.method === 'POST' && String(u).endsWith('/api/identity')) {
            const b = JSON.parse(String(o.body || ''));
            if (b && b.action === 'ack_backup') window.__hushAck = b;
          }
        } catch (e) { /* the real request still goes out */ }
        return window.__hushAckFetch(u, o);
      };
    })()`);
    await cdp.click('#ack-key');
    await cdp.waitFor(`!!document.querySelector('#vibe-name')`, 'vibe step');
    const ack = await cdp.eval(`window.__hushAck`);
    check(ack && ack.action === 'ack_backup' && ack.save_pass === false,
      `no-pass ack sends save_pass false: ${JSON.stringify(ack)}`);
    await cdp.eval(`(() => { if (window.__hushAckFetch) window.fetch = window.__hushAckFetch; })()`);
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
    // r5: the probe drawer grows to ~1100px, so give it a window tall
    // enough that the fixed #quick-bar (now part of the test) stays below.
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: 1400, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
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
      // Content-bottom latch (F-C): bare probe has no padding, so ±3px
      // still flips the cue; blank row-padding alone does not.
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
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
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

    // Pre-walk r4/r5 (Ops FAIL-A, FAIL-1): the fade shows iff some content
    // row reaches past the visible bottom of the drawer: its client box
    // bottom, or the top of the fixed #quick-bar where that overlays the
    // drawer (phones). The visible bottom is found by hit-testing
    // (elementFromPoint), not from the code under test. Helpers:
    //   fadeProbe(target): add a probe row and grow it until the last row
    //     ends at visible bottom + target px;
    //   fadeState(): class, last row bottom, visible bottom and the fade's
    //     computed bottom, all at scrollTop 0.
    const fadeProbe = (target) => cdp.eval(`(async () => {
      const d = document.querySelector('#fo-drawer');
      const frames = () => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(r, 60))));
      // The drawer must be open and on screen for the hit test.
      if (!document.querySelector('#hive').classList.contains('nav-open')) document.querySelector('#nav-toggle').click();
      for (let i = 0; i < 40; i++) { await frames(); const q = d.getBoundingClientRect(); if (d.clientHeight && q.left >= -0.5 && q.right <= innerWidth + 0.5) break; }
      d.scrollTop = 0;
      let probe = document.querySelector('#fade-band-probe');
      if (!probe) { probe = document.createElement('div'); probe.id = 'fade-band-probe'; probe.style.cssText = 'height:0;flex:none'; d.appendChild(probe); }
      probe.style.height = '0px'; await frames();
      const r = d.getBoundingClientRect(); const inner = r.top + d.clientTop; const x = Math.round(r.left + r.width / 2);
      let vis = inner + d.clientHeight;
      for (let y = Math.floor(vis) - 1; y > inner; y--) { const e = document.elementFromPoint(x, y); if (e && d.contains(e)) { vis = y + 1; break; } }
      const last = () => { let m = 0; for (const c of d.children) if (c.getClientRects().length) m = Math.max(m, c.getBoundingClientRect().bottom); return m; };
      // Rows may sit on an auto margin, so grow the probe step by step.
      for (let i = 0; i < 300 && last() < vis + ${target} - 0.25; i++) {
        probe.style.height = (parseFloat(probe.style.height) + Math.max(1, Math.round(vis + ${target} - last()))) + 'px'; await frames(); }
      return { vis, last: last(), probe: probe.style.height, open: d.clientHeight > 0 && r.left >= -0.5 && r.right <= innerWidth + 0.5 };
    })()`, true);
    const fadeState = `(() => { const d = document.querySelector('#fo-drawer'); d.scrollTop = 0;
      const r = d.getBoundingClientRect(); const inner = r.top + d.clientTop; const bottom = inner + d.clientHeight; const x = Math.round(r.left + r.width / 2);
      let vis = bottom; for (let y = Math.floor(bottom) - 1; y > inner; y--) { const e = document.elementFromPoint(x, y); if (e && d.contains(e)) { vis = y + 1; break; } }
      let last = 0; const visit = (el) => { if (!el.getClientRects().length) return; let hasKid = false;
        for (const c of el.children) { if (!c.getClientRects().length) continue; hasKid = true; visit(c); }
        if (hasKid) return; const br = el.getBoundingClientRect(); const cs = getComputedStyle(el);
        const inset = (parseFloat(cs.paddingBottom) || 0) + (parseFloat(cs.borderBottomWidth) || 0);
        last = Math.max(last, br.bottom - inset); };
      for (const c of d.children) visit(c);
      const after = getComputedStyle(d, '::after');
      return { sh: d.scrollHeight, ch: d.clientHeight, last: Math.round(last * 10) / 10, vis: Math.round(vis * 10) / 10, under: Math.round((bottom - vis) * 10) / 10,
        on: d.classList.contains('is-overflowing'), fade: parseFloat(after.bottom), pb: parseFloat(getComputedStyle(d).paddingBottom),
        cueDisp: after.display, cueBg: after.backgroundImage || after.background,
        onscreen: r.left >= -0.5 && r.right <= innerWidth + 0.5 }; })()`;
    const frames2 = `new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(r, 40))))`;
    const fadeRemove = () => cdp.eval(`(() => { const p = document.querySelector('#fade-band-probe'); if (p) p.remove(); })()`);
    const fadeCheck = (r, where) => {
      check(r.onscreen, `drawer is on screen for the fade check at ${where}: ${JSON.stringify(r)}`);
      const hidden = r.last > r.vis + 0.5;
      check(r.on === hidden, `drawer fade is ${hidden ? 'on' : 'off'} when ${hidden ? 'content reaches past' : 'every content edge is above'} the visible bottom at ${where}: ${JSON.stringify(r)}`);
      // #229 W6/W9: when the cue class is on, ::after must stay visible (not display:none / background:none).
      if (r.on) {
        check(r.cueDisp !== 'none', `drawer fade ::after display when overflowing at ${where}: ${JSON.stringify(r)}`);
        check(r.cueBg && r.cueBg !== 'none', `drawer fade ::after background when overflowing at ${where}: ${JSON.stringify(r)}`);
      }
      // The fade ends at the visible bottom (sticky, measured from the
      // content box): bottom = (px under the quick-bar) - padding-bottom.
      if (r.on)
        check(Math.abs(r.fade - (r.under - r.pb)) <= 1, `drawer fade ends at the visible bottom at ${where}: ${JSON.stringify(r)}`);
    };

    // r4 (Ops FAIL-A): only bottom padding overflowing must not set the
    // fade. The probe puts the last row at the visible bottom at 1440x900
    // (Ops's default window) and 375x778, then the window grows 1px at a
    // time through the padding band (1440 h 900-908, 375 h 778-792): every
    // row shows, so the class stays off. r5: 1px shorter, the row is 1px
    // under and the class must be on (Ops's 1440x883 nit, same test).
    const fadeBand = [];
    for (const [bw, h0, h1] of [[1440, 900, 908], [375, 778, 792]]) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: bw, height: h0, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
      const prep = await fadeProbe(0);
      check(prep.open && Math.abs(prep.last - prep.vis) <= 1, `fade band probe puts the last row at the visible bottom at ${bw}x${h0}: ${JSON.stringify(prep)}`);
      for (let h = h0 - 2; h <= h1; h++) {
        await cdp.send('Emulation.setDeviceMetricsOverride', { width: bw, height: h, deviceScaleFactor: 1, mobile: false });
        fadeBand.push(Object.assign({ w: bw, h }, await cdp.eval(`(async () => { await ${frames2}; return ${fadeState}; })()`, true)));
      }
      await fadeRemove();
    }
    console.log('drawer fade padding band: ' + JSON.stringify(fadeBand.map((r) => `${r.w}x${r.h} sh${r.sh} ch${r.ch} vis${r.vis} last${r.last} ${r.on ? 'on' : 'off'}`)));
    for (const [bw, h0] of [[1440, 900], [375, 778]]) {
      const rows = fadeBand.filter((r) => r.w === bw);
      for (const r of rows) fadeCheck(r, `${bw}x${r.h}`);
      const one = rows.find((r) => r.h === h0 - 1);
      check(one && one.on && one.last > one.vis + 0.5 && one.last <= one.vis + 1.5, `drawer fade shows when content is 1px under at ${bw}x${h0 - 1}: ${JSON.stringify(one)}`);
      const band = rows.filter((r) => r.h >= h0);
      check(band.filter((r) => r.sh > r.ch + 1).length >= 5, `padding band exercised at ${bw} (scrollHeight > clientHeight): ${JSON.stringify(band)}`);
      for (const r of band) check(!r.on, `drawer fade stays off when only padding overflows at ${bw}x${r.h}: ${JSON.stringify(r)}`);
    }

    // r5 (Ops 1440x883 nit): at 1440x883 a last row exactly at the edge
    // gets no fade and a row 1px under gets it (content bottom).
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 883, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
    const at883 = [];
    for (const target of [0, 1]) {
      const pr = await fadeProbe(target);
      // The probe row is new and unobserved; nudge the window so the
      // drawer's ResizeObserver re-runs the overflow test.
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 884, deviceScaleFactor: 1, mobile: false });
      await cdp.eval(`${frames2}`, true);
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 883, deviceScaleFactor: 1, mobile: false });
      at883.push(Object.assign({ target, prep: Math.round((pr.last - pr.vis) * 10) / 10 }, await cdp.eval(`(async () => { await ${frames2}; return ${fadeState}; })()`, true)));
      await fadeRemove();
    }
    console.log('drawer fade at 1440x883: ' + JSON.stringify(at883));
    check(at883[0].prep === 0 && !at883[0].on, `1440x883: last row at the edge, no fade: ${JSON.stringify(at883[0])}`);
    check(at883[1].prep === 1 && at883[1].on, `1440x883: last content 1px under the edge, fade on: ${JSON.stringify(at883[1])}`);
    for (const r of at883) fadeCheck(r, '1440x883');

    // r5 (Ops FAIL-1): at phone widths the fixed #quick-bar covers the
    // drawer bottom. The probe puts the last row at the quick-bar top at
    // h 780 (field-office), then every height 700-812 at 375, 414, 480,
    // 560 and 640 (Gauge P2-1: every width that has the quick-bar),
    // in field-office and dark, must have the fade on iff content reaches
    // past the quick-bar top, with the fade ending at that edge. The
    // sweep must include heights where rows hide only under the quick-bar
    // (inside the client box: r4 left the fade off there) and 375x812.
    const phone = [];
    for (const pw of [375, 414, 480, 560, 640]) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: pw, height: 780, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
      const pr = await fadeProbe(0);
      check(pr.open && Math.abs(pr.last - pr.vis) <= 1, `phone fade probe puts the last row at the visible bottom at ${pw}x780: ${JSON.stringify(pr)}`);
      for (let h = 700; h <= 812; h++) {
        await cdp.send('Emulation.setDeviceMetricsOverride', { width: pw, height: h, deviceScaleFactor: 1, mobile: false });
        const both = await cdp.eval(`(async () => { const html = document.documentElement; const keep = html.getAttribute('data-theme'); const out = {};
          for (const th of ['field-office', 'dark']) { html.setAttribute('data-theme', th); await ${frames2}; out[th] = ${fadeState}; }
          html.setAttribute('data-theme', keep); await ${frames2}; return out; })()`, true);
        for (const th of ['field-office', 'dark']) phone.push(Object.assign({ w: pw, h, th }, both[th]));
      }
      await fadeRemove();
    }
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await sleep(300);
    if (VIEW_W > 640) await cdp.eval(`(() => { const d = document.querySelector('#fo-drawer'); d.scrollTop = 0; })()`);
    const underOnly = phone.filter((r) => r.last > r.vis + 0.5 && r.last <= r.vis + r.under);
    console.log(`drawer fade phone sweep: ${phone.length} states, ${phone.filter((r) => r.on).length} on, ${underOnly.length} with rows hidden only under the quick-bar; ` +
      JSON.stringify(phone.filter((r) => r.w === 375 && [728, 750, 777, 812].includes(r.h)).map((r) => `${r.th} ${r.w}x${r.h} vis${r.vis} last${r.last} under${r.under} ${r.on ? 'on' : 'off'} fade${r.fade}`)));
    check(phone.length === 5 * 113 * 2, `phone fade sweep covered 375/414/480/560/640 x h700-812 x 2 themes: ${phone.length}`);
    for (const pw of [375, 414, 480, 560, 640]) for (const th of ['field-office', 'dark'])
      check(underOnly.filter((r) => r.w === pw && r.th === th).length >= 10, `phone sweep has rows hidden only under the quick-bar at ${pw} ${th}`);
    check(phone.some((r) => r.w === 375 && r.h === 812), 'phone sweep includes 375x812');
    for (const r of phone) fadeCheck(r, `${r.w}x${r.h} ${r.th}`);

    // F-C' (Gauge P2-4): Ops false-cue band at 375 (fo h699–705). Real
    // drawer content (no fade-band probe): when the last .fo-person text is
    // fully above the visible bottom, is-overflowing MUST be off. The r3
    // direct-children walk counted nested .fo-person padding and stayed on.
    await fadeRemove();
    for (const h of [699, 702, 705]) {
      await cdp.send('Emulation.setDeviceMetricsOverride',
        { width: 375, height: h, deviceScaleFactor: 1, mobile: false });
      await sleep(250);
      const fc = await cdp.eval(`(async () => {
        const frames = () => new Promise((r) => requestAnimationFrame(() =>
          requestAnimationFrame(() => setTimeout(r, 40))));
        const d = document.querySelector('#fo-drawer');
        if (!document.querySelector('#hive').classList.contains('nav-open'))
          document.querySelector('#nav-toggle').click();
        for (let i = 0; i < 40; i++) {
          await frames();
          const q = d.getBoundingClientRect();
          if (d.clientHeight && q.left >= -0.5 && q.right <= innerWidth + 0.5) break;
        }
        d.scrollTop = 0;
        document.documentElement.setAttribute('data-theme', 'field-office');
        await frames();
        const persons = [...d.querySelectorAll('.fo-person')];
        const row = persons[persons.length - 1];
        if (!row) return { err: 'no fo-person' };
        const name = row.querySelector('.fo-person-name') || row;
        const tr = name.getBoundingClientRect();
        const box = d.getBoundingClientRect();
        const inner = box.top + d.clientTop;
        let vis = inner + d.clientHeight;
        const x = Math.round(box.left + box.width / 2);
        for (let y = Math.floor(vis) - 1; y > inner; y--) {
          const e = document.elementFromPoint(x, y);
          if (e && d.contains(e)) { vis = y + 1; break; }
        }
        const textBottom = tr.bottom;
        const textFullyVisible = textBottom <= vis + 0.5;
        return {
          on: d.classList.contains('is-overflowing'),
          textBottom: Math.round(textBottom * 10) / 10,
          vis: Math.round(vis * 10) / 10,
          textFullyVisible,
          nPeople: persons.length,
          name: (name.textContent || '').trim()
        };
      })()`, true);
      check(!fc.err && fc.nPeople > 0,
        `F-C' setup at 375x${h}: ${JSON.stringify(fc)}`);
      if (fc.textFullyVisible)
        check(!fc.on,
          `F-C': fade off when last person fully visible at 375x${h} fo: ${JSON.stringify(fc)}`);
    }
    await cdp.send('Emulation.setDeviceMetricsOverride',
      { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await sleep(200);

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
    // Pre-walk r5 (Gauge P2-2): the connecting state is the badge as the
    // page is served, before the first status poll: read it from the
    // served HTML, so the lamp is checked amber while connecting too.
    const initBadge = await cdp.eval(`fetch(location.href).then((r) => r.text()).then((t) => {
      const e = new DOMParser().parseFromString(t, 'text/html').querySelector('#badge');
      return e ? { cls: e.className, text: e.textContent, aria: e.getAttribute('aria-label'), title: e.title } : null; })`, true);
    check(initBadge && initBadge.cls === '' && initBadge.aria === 'connecting…', `served badge starts connecting with no state class: ${JSON.stringify(initBadge)}`);
    const dot = await cdp.eval(`((init) => { const b = document.querySelector('header #badge'); const h = document.querySelector('header');
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
        b.className = init.cls; b.textContent = init.text; b.title = init.title; b.setAttribute('aria-label', init.aria);
        const conn = Object.assign(m(), { aria: b.getAttribute('aria-label'), cls: b.className });
        out[th] = { ok, bad, conn };
      }
      html.setAttribute('data-theme', theme);
      b.textContent = keep[0]; b.title = keep[1]; b.setAttribute('aria-label', keep[2]); b.className = keep[3];
      return out; })(${JSON.stringify(initBadge || {})})`);
    console.log('badge lamp at 375: ' + JSON.stringify(dot));
    const rgb = (c) => (String(c).match(/\d+(\.\d+)?/g) || []).slice(0, 3).map(Number);
    const green = (c) => { const [r, g, b] = rgb(c); return g > r + 40 && g > b + 40; };
    const amber = (c) => { const [r, g, b] = rgb(c); return r >= g && g > b + 40 && r > 150; };
    for (const th of ['field-office', 'dark']) {
      const { ok, bad, conn } = dot[th];
      for (const [st, v] of [['ok', ok], ['bad', bad], ['connecting', conn]]) {
        check(v.fs === 0 && v.w >= 10 && v.w <= 16 && v.h >= 10 && v.h <= 16, `${st} badge is a ~12px lamp with no text at 375 (${th}): ${JSON.stringify(v)}`);
        check(v.rimW >= 1 && /rgb/.test(v.glow) && v.glow !== 'none', `${st} badge lamp has a rim and glow (${th}): ${JSON.stringify(v)}`);
        check(v.seat >= 0 && v.seat <= 20 && Math.abs(v.mid) <= 4, `${st} badge lamp sits at the header's right edge (${th}): ${JSON.stringify(v)}`);
      }
      check(green(ok.fill), `listening badge lamp is green at 375 (${th}): ${JSON.stringify(ok)}`);
      check(amber(bad.fill), `not-listening badge lamp is amber at 375 (${th}): ${JSON.stringify(bad)}`);
      check(amber(conn.fill) && conn.aria === 'connecting…', `connecting badge lamp is amber at 375 (${th}): ${JSON.stringify(conn)}`);
      check(bad.role === 'img' && bad.aria === 'relay unreachable' && bad.title === 'relay unreachable', `bad badge keeps its state text (${th}): ${JSON.stringify(bad)}`);
    }
    if (VIEW_W > 480) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
    }
    await cdp.shot('hive-fo');
    if (VIEW_W === 1440) await badgeFit('after login', 'npub1');

    // #241 B1: New channel — backdrop (drawer itself) closes; inside .panel stays open.
    {
      const nc = await cdp.eval(`(() => {
        const d = document.getElementById('new-chan-drawer');
        if (!d) return { err: 'missing drawer' };
        d.classList.add('show');
        const panel = d.querySelector('.panel');
        const pr = panel.getBoundingClientRect();
        // Backdrop: top-left of the fixed inset drawer (outside the panel).
        let bx = 20, by = Math.min(800, Math.floor(window.innerHeight - 20));
        let hit = document.elementFromPoint(bx, by);
        if (hit && panel.contains(hit)) {
          bx = 20; by = 20;
          hit = document.elementFromPoint(bx, by);
        }
        if (hit) hit.dispatchEvent(new PointerEvent('pointerdown', {
          bubbles: true, cancelable: true, clientX: bx, clientY: by, pointerId: 1
        }));
        const afterBackdrop = d.classList.contains('show');
        d.classList.add('show');
        const ix = pr.left + pr.width / 2, iy = pr.top + 40;
        const hitIn = document.elementFromPoint(ix, iy);
        if (hitIn) hitIn.dispatchEvent(new PointerEvent('pointerdown', {
          bubbles: true, cancelable: true, clientX: ix, clientY: iy, pointerId: 2
        }));
        const afterInside = d.classList.contains('show');
        d.classList.remove('show');
        return {
          hitId: hit && hit.id, hitClass: hit && hit.className,
          afterBackdrop, afterInside,
          hitIn: hitIn && (hitIn.id || hitIn.tagName)
        };
      })()`);
      console.log('new-chan outside click: ' + JSON.stringify(nc));
      check(!nc.err, `new-chan drawer present for outside-click pin: ${JSON.stringify(nc)}`);
      check(nc.afterBackdrop === false,
        `#241 B1 backdrop click closes new-chan drawer: ${JSON.stringify(nc)}`);
      check(nc.afterInside === true,
        `#241 B1 inside-panel click keeps new-chan open: ${JSON.stringify(nc)}`);
    }

    // #241 r4 F3: Settings — focus enters dialog, Tab stays inside, Esc returns to Kit.
    {
      const sf = await cdp.eval(`(() => {
        const kit = document.getElementById('rail-toggle');
        const settings = document.getElementById('settings');
        if (!kit || !settings || typeof openSettings !== 'function' || typeof closeSettings !== 'function'
            || typeof settingsFocusables !== 'function')
          return { err: 'missing settings a11y helpers' };
        kit.focus();
        openSettings();
        const afterOpen = {
          show: settings.classList.contains('show'),
          inDialog: !!(document.activeElement && settings.contains(document.activeElement)),
          ae: document.activeElement && (document.activeElement.id || document.activeElement.tagName)
        };
        const list = settingsFocusables();
        let wrapForward = false, wrapBack = false;
        if (list.length >= 2) {
          list[list.length - 1].focus();
          document.dispatchEvent(new KeyboardEvent('keydown', {
            key: 'Tab', code: 'Tab', bubbles: true, cancelable: true
          }));
          wrapForward = document.activeElement === list[0];
          list[0].focus();
          document.dispatchEvent(new KeyboardEvent('keydown', {
            key: 'Tab', code: 'Tab', bubbles: true, cancelable: true, shiftKey: true
          }));
          wrapBack = document.activeElement === list[list.length - 1];
        }
        window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
        const afterEsc = {
          show: settings.classList.contains('show'),
          backOnKit: document.activeElement === kit,
          ae: document.activeElement && (document.activeElement.id || document.activeElement.tagName)
        };
        openSettings();
        const closeBtn = document.getElementById('settings-close');
        if (closeBtn) closeBtn.click();
        const afterClose = {
          show: settings.classList.contains('show'),
          backOnKit: document.activeElement === kit
        };
        return { afterOpen, wrapForward, wrapBack, afterEsc, afterClose, nFocusable: list.length };
      })()`);
      console.log('settings a11y: ' + JSON.stringify(sf));
      check(!sf.err, `#241 F3 settings helpers present: ${JSON.stringify(sf)}`);
      check(sf.afterOpen && sf.afterOpen.show && sf.afterOpen.inDialog,
        `#241 F3 openSettings moves focus into dialog: ${JSON.stringify(sf)}`);
      check(sf.wrapForward === true && sf.wrapBack === true,
        `#241 F3 Tab trap wraps inside Settings: ${JSON.stringify(sf)}`);
      check(sf.afterEsc && sf.afterEsc.show === false && sf.afterEsc.backOnKit,
        `#241 F3 Esc closes Settings and returns focus to Kit: ${JSON.stringify(sf)}`);
      check(sf.afterClose && sf.afterClose.show === false && sf.afterClose.backOnKit,
        `#241 F3 Close returns focus to Kit: ${JSON.stringify(sf)}`);
    }

    // #241 r5/r6: real CDP mouse click and Escape key helpers.
    const realClick = async (sel) => {
      const pt = await cdp.eval(`(() => { const e = document.querySelector('${sel}');
        if (!e) return null; e.scrollIntoView({ block: 'center' }); const r = e.getBoundingClientRect();
        if (!r.width || !r.height) return null;
        return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
      check(!!pt, `#241 ${sel} is visible for a real click`);
      if (!pt) return;
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: pt.x, y: pt.y });
      await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: pt.x, y: pt.y, button: 'left', clickCount: 1, buttons: 1 });
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: pt.x, y: pt.y, button: 'left', clickCount: 1, buttons: 0 });
      await sleep(250);
    };
    const realEsc = async () => {
      await cdp.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27 });
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27 });
      await sleep(250);
    };

    // #241 r5 B4: the real user path. Real CDP mouse clicks on the Kit stamp
    // and then on Settings inside #kit-menu (so the opener is #settings-btn,
    // which the Kit menu hides), then a real Escape key, then the same with a
    // real click on Close. Focus must land on #rail-toggle each time.
    {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
      const state = () => cdp.eval(`(() => { const a = document.activeElement;
        return { show: document.getElementById('settings').classList.contains('show'),
          kitHidden: document.getElementById('kit-menu').hidden,
          ae: a ? (a.id || a.tagName) : null,
          inDialog: !!(a && document.getElementById('settings').contains(a)) }; })()`);
      const runs = {};
      for (const how of ['esc', 'close']) {
        await realClick('#rail-toggle');
        const kitOpen = await state();
        await realClick('#settings-btn');
        const opened = await state();
        if (how === 'esc') await realEsc();
        else await realClick('#settings-close');
        const after = await state();
        runs[how] = { kitOpen, opened, after };
      }
      console.log('settings real path: ' + JSON.stringify(runs));
      for (const how of ['esc', 'close']) {
        const r = runs[how];
        check(r.kitOpen.kitHidden === false,
          `#241 B4 (${how}) real click on #rail-toggle opens the Kit menu: ${JSON.stringify(r)}`);
        check(r.opened.show === true && r.opened.inDialog === true,
          `#241 B4 (${how}) real click on #settings-btn opens Settings with focus inside: ${JSON.stringify(r)}`);
        check(r.after.show === false && r.after.ae === 'rail-toggle',
          `#241 B4 (${how}) Settings closes and focus returns to #rail-toggle on the Kit-menu path: ${JSON.stringify(r)}`);
      }
    }

    // #241 r6/r7: every Settings Tab stop shows a visible keyboard ring
    // when reached with a real Tab, on every shipped theme (the Settings
    // radios). The ring is drawn on the control itself, or on the visible
    // track (.slider) for a switch. r7 (Gauge B5, Ops F3 #turn-host): the
    // ring colour, resolved to sRGB and composited, must reach WCAG 1.4.11
    // 3:1 against the effective background it sits on (first opaque
    // ancestor, .panel), and must sit outside the control (offset >= 0) so
    // that is the background it is drawn on. Only the focused switch track
    // is ringed, and a real mouse click on a switch or a radio shows no ring.
    {
      const tabKey = async (shift) => {
        const mod = shift ? 8 : 0;
        await cdp.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9, nativeVirtualKeyCode: 9, modifiers: mod });
        await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9, nativeVirtualKeyCode: 9, modifiers: mod });
        await sleep(120);
      };
      const SWITCHES = ['turn-on', 'turn-daemon', 'vibe-public', 'dev-log'];
      const STOPS = ['turn-on', 'turn-daemon', 'turn-host', 'vibe-public', 'vibe-rotate', 'dev-log', 'theme', 'settings-close'];
      const THEMES = await cdp.eval(`Array.prototype.map.call(document.querySelectorAll("#settings input[name='theme']"), (i) => i.value)`);
      check(JSON.stringify(THEMES.slice().sort()) === JSON.stringify(['christmas', 'color-blind', 'dark', 'desert', 'dracula', 'field-office', 'light', 'monochrome']),
        `#241 r7 Settings offers the 8 shipped themes: ${JSON.stringify(THEMES)}`);
      const ringState = `(() => { const a = document.activeElement;
        const px = (c) => { const cv = document.createElement('canvas'); cv.width = 1; cv.height = 1;
          const x = cv.getContext('2d'); x.clearRect(0, 0, 1, 1); x.fillStyle = c; x.fillRect(0, 0, 1, 1);
          const d = x.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255]; };
        const over = (top, bot) => [0, 1, 2].map((i) => top[i] * top[3] + bot[i] * (1 - top[3]));
        const lum = (c) => { const f = (v) => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
          return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2]); };
        const ratio = (p, q) => { const a1 = lum(p), b1 = lum(q); return (Math.max(a1, b1) + 0.05) / (Math.min(a1, b1) + 0.05); };
        const effBg = (el) => { const layers = []; const images = []; let from = null;
          for (let n = el; n && n.nodeType === 1; n = n.parentElement) {
            const cs = getComputedStyle(n); const c = px(cs.backgroundColor);
            if (cs.backgroundImage !== 'none') images.push((n.id ? '#' + n.id : n.className || n.tagName) + ':' + cs.backgroundImage.slice(0, 40));
            if (c[3] > 0) { layers.push(c); if (c[3] >= 1) { from = n.id ? '#' + n.id : (n.className || n.tagName); break; } } }
          let bg = [255, 255, 255];
          for (let i = layers.length - 1; i >= 0; i--) bg = over(layers[i], bg);
          return { bg: bg.map(Math.round), from, images }; };
        const ring = (el) => { const cs = getComputedStyle(el); const r = el.getBoundingClientRect();
          return { style: cs.outlineStyle, width: parseFloat(cs.outlineWidth) || 0, offset: parseFloat(cs.outlineOffset) || 0,
            color: cs.outlineColor, w: r.width, h: r.height }; };
        const isSwitch = !!(a && a.nextElementSibling && a.nextElementSibling.classList.contains('slider'));
        const target = a && a !== document.body ? (isSwitch ? a.nextElementSibling : a) : null;
        const id = !a ? null : (a.name === 'theme' ? 'theme' : a.id);
        let contrast = null;
        if (target) { const cs = getComputedStyle(target); const e = effBg(target.parentElement); const rc = px(cs.outlineColor);
          const seen = over(rc, e.bg).map(Math.round);
          contrast = { ring: rc.slice(0, 3).concat([Math.round(rc[3] * 100) / 100]), bg: e.bg, from: e.from, images: e.images,
            ratio: Math.round(ratio(seen, e.bg) * 100) / 100 }; }
        const others = Array.prototype.map.call(document.querySelectorAll('#settings .switch .slider'), (s) => s === target ? null : ring(s).style).filter((x) => x);
        return { id, value: a && a.name === 'theme' ? a.value : undefined, inSettings: !!(a && document.getElementById('settings').contains(a)),
          fv: !!(a && a.matches(':focus-visible')), isSwitch, ring: target ? ring(target) : null, contrast, othersStyles: others }; })()`;
      const ringRuns = {};
      const mouseRuns = {};
      const threadRuns = {};
      const threadMouse = {};
      const keepTheme = await cdp.eval(`document.documentElement.getAttribute('data-theme')`);
      for (const th of THEMES) {
        await cdp.eval(`applyTheme(${JSON.stringify(th)})`);
        await realClick('#rail-toggle');
        await realClick('#settings-btn');
        const seen = {};
        const order = [];
        for (let i = 0; i < 20; i++) {
          await tabKey(false);
          const s = await cdp.eval(ringState);
          order.push(s.id);
          if (!seen[s.id]) seen[s.id] = s;
        }
        ringRuns[th] = { seen, order };
        if (th === 'field-office' || th === 'dark') {
          // Real mouse clicks: the Public vibe track (twice, so the setting
          // ends where it started) and the checked theme radio (no change).
          // The vibe save repaints from the session, which re-applies the
          // profile theme, so the theme under test is set again before reading.
          const before = await cdp.eval(`document.getElementById('vibe-public').checked`);
          const clicks = [];
          for (const sel of ['#vibe-public + .slider', '#vibe-public + .slider', `#settings input[name="theme"][value="${th}"]`]) {
            await realClick(sel);
            await sleep(300);
            await cdp.eval(`applyTheme(${JSON.stringify(th)})`);
            await sleep(100);
            const c = await cdp.eval(ringState);
            c.theme = await cdp.eval(`document.documentElement.getAttribute('data-theme')`);
            c.clicked = sel;
            clicks.push(c);
          }
          const after = await cdp.eval(`document.getElementById('vibe-public').checked`);
          mouseRuns[th] = { before, after, clicks };
        }
        await realEsc();
        // #241 r7 (Ops #281): the thread composer #thread-msg, reached with
        // real Tab keys from the thread's Close button, shows the same ring.
        await cdp.eval(`(() => { const ev = (lastEvents || []).find((e) => !e.reply_to);
          openThreadPane(ev ? ev.id : 'r7-ring-probe'); document.getElementById('thread-close').focus(); })()`);
        await sleep(200);
        let tm = null;
        const tpath = [];
        for (let i = 0; i < 12 && !tm; i++) {
          await tabKey(false);
          const s = await cdp.eval(ringState);
          tpath.push(s.id);
          if (s.id === 'thread-msg') tm = s;
        }
        threadRuns[th] = { tm, tpath };
        if (th === 'field-office' || th === 'dark') {
          await cdp.eval(`document.getElementById('thread-close').focus()`);
          await realClick('#thread-msg');
          const c = await cdp.eval(ringState);
          c.theme = await cdp.eval(`document.documentElement.getAttribute('data-theme')`);
          threadMouse[th] = c;
        }
        await cdp.eval(`closeThreadPane()`);
        await sleep(150);
      }
      await cdp.eval(`applyTheme(${JSON.stringify(keepTheme || 'field-office')})`);
      console.log('settings focus rings: ' + JSON.stringify(ringRuns));
      console.log('thread composer rings: ' + JSON.stringify(threadRuns));
      console.log('thread composer mouse (text field: browsers show focus-visible on any focus): ' + JSON.stringify(threadMouse));
      const table = {};
      for (const th of THEMES) {
        const { seen, order } = ringRuns[th];
        table[th] = {};
        const stopIds = Object.keys(seen);
        check(JSON.stringify(stopIds.slice().sort()) === JSON.stringify(STOPS.slice().sort()) && order.every((x) => STOPS.includes(x)),
          `#241 r7 Settings Tab stops are the 8 known controls (${th}): ${JSON.stringify(order)}`);
        for (const id of STOPS) {
          const s = seen[id];
          if (s && s.contrast) table[th][id] = s.contrast.ratio;
          check(!!s && s.inSettings && s.fv && s.ring && s.ring.style !== 'none' && s.ring.width >= 2 && s.ring.w > 0 && s.ring.h > 0,
            `#241 r7 real Tab to ${id} shows a visible ring (${th}): ${JSON.stringify(s)}`);
          check(!!s && s.ring && s.ring.offset >= 0,
            `#241 r7 the ring on ${id} sits outside the control, on the panel background (${th}): ${JSON.stringify(s)}`);
          check(!!s && s.contrast && s.contrast.images.length === 0 && !!s.contrast.from,
            `#241 r7 ${id} ring background is a plain colour, so the contrast is computable (${th}): ${JSON.stringify(s)}`);
          check(!!s && s.contrast && s.contrast.ratio >= 3,
            `#241 r7 ${id} ring contrast vs its background is at least 3:1 (${th}): ${JSON.stringify(s)}`);
          check(!!s && s.othersStyles.every((x) => x === 'none'),
            `#241 r6 no other switch track is ringed while ${id} has focus (${th}): ${JSON.stringify(s)}`);
        }
        const t = threadRuns[th].tm;
        if (t && t.contrast) table[th]['thread-msg'] = t.contrast.ratio;
        check(!!t && t.fv && t.ring && t.ring.style !== 'none' && t.ring.width >= 2 && t.ring.w > 0 && t.ring.h > 0 && t.ring.offset >= 0,
          `#241 r7 real Tab to #thread-msg shows a visible ring outside the field (${th}): ${JSON.stringify(threadRuns[th])}`);
        check(!!t && t.contrast && t.contrast.images.length === 0 && !!t.contrast.from && t.contrast.ratio >= 3,
          `#241 r7 #thread-msg ring contrast vs its background is at least 3:1 (${th}): ${JSON.stringify(threadRuns[th])}`);
        if (seen.theme) check(seen.theme.value === th, `#241 r7 the theme Tab stop is the checked radio (${th}): ${JSON.stringify(seen.theme)}`);
      }
      console.log('settings ring contrast: ' + JSON.stringify(table));
      console.log('settings focus mouse: ' + JSON.stringify(mouseRuns));
      for (const th of ['field-office', 'dark']) {
        const m = mouseRuns[th];
        check(!!m && m.clicks.length === 3 && m.clicks.every((c) => c.theme === th && c.inSettings && !c.fv && c.ring && c.ring.style === 'none')
          && m.clicks[0].id === 'vibe-public' && m.clicks[1].id === 'vibe-public' && m.clicks[2].id === 'theme',
          `#241 r7 a real mouse click on a switch or a radio focuses it with no ring (${th}): ${JSON.stringify(m)}`);
        check(!!m && m.after === m.before,
          `#241 r7 the two mouse clicks leave Public vibe as it was (${th}): ${JSON.stringify(m)}`);
      }
    }

    // #241 r8 (Gauge B6): forced-state sweep over every .panel input,
    // textarea and select in the page (all drawers, open or not), on all 8
    // themes. CDP forces :focus and :focus-visible on each one (as Gauge's
    // sweep does), then the computed ring must be the same one on every
    // field: solid, exactly 2px, offset exactly 2px (outside the control),
    // colour equal to the theme's --fg, and >= 3:1 against its effective
    // background. Covers controls Tab never reaches in this test,
    // e.g. untyped #new-chan and the file inputs.
    {
      const SWEEP_SEL = '.panel input:not([type="hidden"]), .panel textarea, .panel select';
      const MUST = ['new-chan', 'prof-avatar', 'agent-file', 'turn-host'];
      const themes = await cdp.eval(`Array.prototype.map.call(document.querySelectorAll("#settings input[name='theme']"), (i) => i.value)`);
      await cdp.send('DOM.enable');
      await cdp.send('CSS.enable');
      const sweepEval = `(() => {
        const px = (c) => { const cv = document.createElement('canvas'); cv.width = 1; cv.height = 1;
          const x = cv.getContext('2d'); x.clearRect(0, 0, 1, 1); x.fillStyle = c; x.fillRect(0, 0, 1, 1);
          const d = x.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255]; };
        const over = (top, bot) => [0, 1, 2].map((i) => top[i] * top[3] + bot[i] * (1 - top[3]));
        const lum = (c) => { const f = (v) => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
          return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2]); };
        const ratio = (p, q) => { const a1 = lum(p), b1 = lum(q); return (Math.max(a1, b1) + 0.05) / (Math.min(a1, b1) + 0.05); };
        const effBg = (el) => { const layers = []; let images = 0; let from = null;
          for (let n = el; n && n.nodeType === 1; n = n.parentElement) {
            const cs = getComputedStyle(n); const c = px(cs.backgroundColor);
            if (cs.backgroundImage !== 'none') images++;
            if (c[3] > 0) { layers.push(c); if (c[3] >= 1) { from = n.id ? '#' + n.id : (n.className || n.tagName); break; } } }
          let bg = [255, 255, 255];
          for (let i = layers.length - 1; i >= 0; i--) bg = over(layers[i], bg);
          return { bg: bg.map(Math.round), from, images }; };
        return Array.prototype.map.call(document.querySelectorAll(${JSON.stringify(SWEEP_SEL)}), (el) => {
          const cs = getComputedStyle(el); const e = effBg(el.parentElement); const rc = px(cs.outlineColor);
          const fg = px(cs.getPropertyValue('--fg').trim());
          const host = el.closest('.drawer, .stage');
          return { id: el.id || null, name: el.name || null, type: el.getAttribute('type'), tag: el.tagName.toLowerCase(),
            host: host ? host.id : null, fv: el.matches(':focus-visible'), style: cs.outlineStyle,
            width: parseFloat(cs.outlineWidth) || 0, offset: parseFloat(cs.outlineOffset) || 0,
            fgRing: rc.every((v, i) => v === fg[i]), rgb: rc.slice(0, 3).join(','),
            from: e.from, images: e.images, ratio: Math.round(ratio(over(rc, e.bg).map(Math.round), e.bg) * 100) / 100 }; }); })()`;
      const sweep = {};
      const keepTheme8 = await cdp.eval(`document.documentElement.getAttribute('data-theme')`);
      for (const th of themes) {
        await cdp.eval(`applyTheme(${JSON.stringify(th)})`);
        await sleep(150);
        const doc = await cdp.send('DOM.getDocument', { depth: -1 });
        const q = doc.root ? await cdp.send('DOM.querySelectorAll', { nodeId: doc.root.nodeId, selector: SWEEP_SEL }) : {};
        const ids = q.nodeIds || [];
        for (const nodeId of ids)
          await cdp.send('CSS.forcePseudoState', { nodeId, forcedPseudoClasses: ['focus', 'focus-visible'] });
        const rows = await cdp.eval(sweepEval);
        for (const nodeId of ids)
          await cdp.send('CSS.forcePseudoState', { nodeId, forcedPseudoClasses: [] });
        const bad = rows.filter((r) => !(r.fv && r.style === 'solid' && r.width === 2 && r.offset === 2 && r.fgRing && r.images === 0 && r.from && r.ratio >= 3));
        sweep[th] = { total: rows.length, nodes: ids.length, ringed: rows.length - bad.length,
          min: rows.length ? Math.min.apply(null, rows.map((r) => r.ratio)) : null,
          seen: MUST.filter((id) => rows.some((r) => r.id === id)), bad };
      }
      await cdp.eval(`applyTheme(${JSON.stringify(keepTheme8 || 'field-office')})`);
      console.log('panel field sweep: ' + JSON.stringify(Object.fromEntries(Object.entries(sweep).map(([k, v]) =>
        [k, { total: v.total, ringed: v.ringed, min: v.min, bad: v.bad.map((b) => (b.id || b.name || b.tag) + ':' + b.style + ' ' + b.width + 'px off ' + b.offset + (b.fgRing ? ' fg' : ' rgb(' + b.rgb + ')') + ' r ' + b.ratio) }]))));
      check(themes.length === 8, `#241 r8 sweep runs on the 8 shipped themes: ${JSON.stringify(themes)}`);
      for (const th of themes) {
        const v = sweep[th];
        check(v.total > 0 && v.total === v.nodes && v.seen.length === MUST.length,
          `#241 r8 sweep found every .panel field incl ${MUST.join(', ')} (${th}): ${JSON.stringify({ total: v.total, nodes: v.nodes, seen: v.seen })}`);
        check(v.bad.length === 0,
          `#241 r8 every .panel input, textarea and select gets the same ring (solid 2px --fg, offset 2px) at >= 3:1 under forced :focus-visible (${th}): ${v.ringed}/${v.total}; ${JSON.stringify(v.bad)}`);
      }
    }

    // #241 r6: at 390px, a keyboard open of Settings (Kit, then Enter on
    // Settings) leaves the panel at its title: heading on screen, scrollTop 0.
    // First a real keyboard visit in dark: open, Shift+Tab wraps to Close
    // (which scrolls the panel down to it), Enter closes. Chrome keeps the
    // panel's scroll offset while it is hidden, so without a reset the next
    // open lands scrolled with the title above the view (the Ops path).
    {
      const keepTheme390 = await cdp.eval(`document.documentElement.getAttribute('data-theme')`);
      await cdp.eval(`document.documentElement.setAttribute('data-theme', 'dark')`);
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
      await sleep(400);
      const key390 = async (key, code, vk, mod, text) => {
        const down = { type: text ? 'keyDown' : 'rawKeyDown', key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers: mod || 0 };
        if (text) { down.text = text; down.unmodifiedText = text; }
        await cdp.send('Input.dispatchKeyEvent', down);
        await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers: mod || 0 });
        await sleep(400);
      };
      const kbOpen390 = async () => {
        await realClick('#rail-toggle');
        await cdp.eval(`document.getElementById('settings-btn').focus()`);
        await key390('Enter', 'Enter', 13, 0, '\r');
      };
      await kbOpen390();
      await key390('Tab', 'Tab', 9, 8);
      const preScroll = await cdp.eval(`(() => { const d = document.getElementById('settings'); const p = d.querySelector('.panel');
        return { ae: document.activeElement && document.activeElement.id, panel: p.scrollTop, drawer: d.scrollTop }; })()`);
      await key390('Enter', 'Enter', 13, 0, '\r');
      const preClosed = await cdp.eval(`!document.getElementById('settings').classList.contains('show')`);
      console.log('settings 390 pre-scroll: ' + JSON.stringify(Object.assign({ closed: preClosed }, preScroll)));
      check(preScroll.ae === 'settings-close' && (preScroll.panel > 0 || preScroll.drawer > 0) && preClosed,
        `#241 r6 at 390 Shift+Tab reaches Close with the panel scrolled to it, and Enter closes: ${JSON.stringify(preScroll)}`);
      await kbOpen390();
      const t390 = await cdp.eval(`(() => { const d = document.getElementById('settings'); const p = d.querySelector('.panel');
        const h = p.querySelector('h2'); const hr = h.getBoundingClientRect(); const pr = p.getBoundingClientRect();
        return { show: d.classList.contains('show'), ae: document.activeElement && document.activeElement.id,
          scrollTop: p.scrollTop, drawerScroll: d.scrollTop, hTop: hr.top, hBottom: hr.bottom, pTop: pr.top, vh: innerHeight,
          ring: (() => { const a = document.activeElement; const sl = a && a.nextElementSibling;
            if (!sl || !sl.classList.contains('slider')) return null;
            const cs = getComputedStyle(sl); const r = sl.getBoundingClientRect();
            return { fv: a.matches(':focus-visible'), style: cs.outlineStyle, width: parseFloat(cs.outlineWidth) || 0,
              w: r.width, h: r.height, inView: r.top >= 0 && r.bottom <= innerHeight }; })() }; })()`);
      console.log('settings 390 open: ' + JSON.stringify(t390));
      check(t390.show && t390.ae === 'turn-on',
        `#241 r6 keyboard open at 390 shows Settings with focus on #turn-on: ${JSON.stringify(t390)}`);
      check(!!t390.ring && t390.ring.fv && t390.ring.style !== 'none' && t390.ring.width >= 2 && t390.ring.w > 0 && t390.ring.h > 0 && t390.ring.inView,
        `#241 r6 keyboard open lands on #turn-on with a visible ring on its track (dark, 390): ${JSON.stringify(t390)}`);
      check(t390.scrollTop === 0 && t390.drawerScroll === 0 && t390.hTop >= t390.pTop - 1 && t390.hTop >= 0 && t390.hBottom <= t390.vh,
        `#241 r6 Settings opens at its title at 390 (heading in view, not scrolled): ${JSON.stringify(t390)}`);
      await realEsc();
      await cdp.eval(`document.documentElement.setAttribute('data-theme', ${JSON.stringify(keepTheme390 || 'field-office')})`);
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
      await sleep(300);
    }

    // Pre-walk r4 (Gauge B1), no pass installed: the robot editor never
    // claims to save the key, on Raise or any Edit path.
    await cdp.waitFor(`!!document.querySelector('#robot-list .robot-card[data-slug="coach"]')`, 'robot cards');
    check((await sess(port, R.headers)).pass_available === false, 'phase A relay reports no pass');
    checkRobotPassHidden(await robotPassBox('Raise a robot', `document.querySelector('#raise-agent').click()`), 'no pass');
    checkRobotPassHidden(await robotPassBox('Edit locked robot', editRobot('coach')), 'no pass');
    checkRobotPassHidden(await robotPassBox('Edit Major', editRobot('sgt-major-payne')), 'no pass');
    checkSavePass(await savePassSent('Raise a robot', `document.querySelector('#raise-agent').click()`, true), false, 'no pass, box hidden');
    checkSavePass(await savePassSent('Edit locked robot', editRobot('coach'), true), false, 'no pass, update');
    await cdp.eval(`document.querySelector('#agent-close').click()`);

    // Walk B2-B4. These read painted behaviour. B3 injects roster rows in
    // the same turn it measures, because tick() rebuilds that list every
    // second; the rows use the real .fo-person rule.
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
    await sleep(200);
    await cdp.eval(`['agent-drawer','inv-expand-drawer'].forEach((id) => { const e = document.getElementById(id); if (e) e.classList.remove('show'); })`);
    await cdp.click('#qb-1');
    await cdp.waitFor(`!!document.querySelector('#inv-expand-drawer.show') && !!document.querySelector('#robot-inventory-full .inv-item[data-slug="coach"]')`, 'expanded inventory coach');
    const editIdle = await cdp.eval(`(() => { const b = document.querySelector('#inv-expand-edit'); return { dis: b.disabled, aria: b.getAttribute('aria-label'), title: b.title, text: b.textContent.trim() }; })()`);
    console.log('edit idle: ' + JSON.stringify(editIdle));
    check(editIdle.dis === true && editIdle.text === 'Edit' && editIdle.aria === 'Edit the selected robot' && editIdle.title === 'Select a robot first',
      `Edit stays disabled until a robot is selected: ${JSON.stringify(editIdle)}`);
    const menu = await cdp.eval(`(() => {
      const el = document.querySelector('#robot-inventory-full .inv-item[data-slug="coach"]');
      const r = el.getBoundingClientRect();
      el.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: r.left + 10, clientY: r.top + 10, button: 2 }));
      const m = document.querySelector('#inv-menu');
      const mr = m.getBoundingClientRect();
      const hit = document.elementFromPoint(mr.left + mr.width / 2, mr.top + 12);
      return {
        show: m.classList.contains('show'),
        z: getComputedStyle(m).zIndex,
        dz: getComputedStyle(document.querySelector('#inv-expand-drawer')).zIndex,
        inMenu: !!(hit && hit.closest && hit.closest('#inv-menu')),
        label: (m.querySelector('button[data-act="edit"]') || { textContent: '' }).textContent.trim()
      };
    })()`);
    console.log('inv menu: ' + JSON.stringify(menu));
    check(menu.show && Number(menu.z) > Number(menu.dz) && menu.inMenu && menu.label === 'edit',
      `inventory menu is above the drawer and hittable: ${JSON.stringify(menu)}`);
    const editRight = await cdp.eval(`(() => { const b = document.querySelector('#inv-expand-edit'); return { dis: b.disabled, aria: b.getAttribute('aria-label'), title: b.title, text: b.textContent.trim() }; })()`);
    console.log('edit after right-click: ' + JSON.stringify(editRight));
    check(!editRight.dis && editRight.text === 'Edit' && editRight.aria === 'Edit Coach' && editRight.title === 'Edit Coach',
      `right-click enables Edit for that robot: ${JSON.stringify(editRight)}`);
    await cdp.eval(`document.querySelector('#inv-menu button[data-act="edit"]').click()`);
    await cdp.waitFor(`document.querySelector('#agent-drawer.show') && document.querySelector('#agent-name').value === 'Coach'`, 'edit from menu');
    await cdp.eval(`document.querySelector('#agent-close').click()`);
    const help = await cdp.eval(`document.querySelector('#inv-expand-help').textContent`);
    check(help.includes('Select a robot and press Edit, or double-click it.') && help.includes('Right-click and choose edit.') && help.includes('Expanded 8×5 grid.'),
      `inventory help states the edit paths: ${help}`);
    await cdp.eval(`document.querySelector('#inv-expand').click()`);
    await cdp.waitFor(`!!document.querySelector('#inv-expand-drawer.show') && !!document.querySelector('#robot-inventory-full .inv-item[data-slug="coach"]')`, 'inventory for Edit button');
    const pt = await cdp.eval(`(() => { const el = document.querySelector('#robot-inventory-full .inv-item[data-slug="coach"]'); const r = el.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: pt.x, y: pt.y, button: 'left', clickCount: 1, buttons: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: pt.x, y: pt.y, button: 'left', clickCount: 1, buttons: 0 });
    await sleep(150);
    const editBtn = await cdp.eval(`(() => { const b = document.querySelector('#inv-expand-edit'); return { dis: b.disabled, aria: b.getAttribute('aria-label'), title: b.title, text: b.textContent.trim() }; })()`);
    console.log('edit button: ' + JSON.stringify(editBtn));
    check(!editBtn.dis && editBtn.text === 'Edit' && editBtn.aria === 'Edit Coach' && editBtn.title === 'Edit Coach',
      `Edit button names the selected robot: ${JSON.stringify(editBtn)}`);
    await cdp.eval(`document.querySelector('#inv-expand-edit').click()`);
    await cdp.waitFor(`document.querySelector('#agent-drawer.show') && document.querySelector('#agent-name').value === 'Coach' && document.querySelector('#agent-title').textContent.startsWith('Edit')`, 'edit from button');
    await cdp.eval(`document.querySelector('#agent-close').click()`);
    await cdp.eval(`document.querySelector('#inv-expand').click()`);
    await cdp.waitFor(`!!document.querySelector('#inv-expand-drawer.show') && !!document.querySelector('#robot-inventory-full .inv-item[data-slug="coach"]')`, 'inventory for double-click');
    const pt2 = await cdp.eval(`(() => { const el = document.querySelector('#robot-inventory-full .inv-item[data-slug="coach"]'); const r = el.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: pt2.x, y: pt2.y, button: 'left', clickCount: 1, buttons: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: pt2.x, y: pt2.y, button: 'left', clickCount: 1, buttons: 0 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: pt2.x, y: pt2.y, button: 'left', clickCount: 2, buttons: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: pt2.x, y: pt2.y, button: 'left', clickCount: 2, buttons: 0 });
    await cdp.waitFor(`document.querySelector('#agent-drawer.show') && document.querySelector('#agent-name').value === 'Coach'`, 'edit from double-click', 4000);
    await cdp.eval(`document.querySelector('#agent-close').click()`);
    await cdp.eval(`const d = document.querySelector('#inv-expand-drawer'); if (d) d.classList.remove('show');`);

    // PE-4.1 FULL1/FULL2/KEEP1 (Gauge P2-2/P2-3): seed 32 favorites on Coach,
    // assert #fav-full next to Save; new-name refuse sends no request and keeps
    // #fav-name; same-name save still posts and clears the field.
    await cdp.eval(`document.querySelector('#robot-list .robot-card[data-slug="coach"] .robot-actions button').click()`);
    await cdp.waitFor(`document.querySelector('#agent-drawer.show') && document.querySelector('#agent-title').textContent.startsWith('Edit') && document.querySelector('#agent-name').value === 'Coach'`, 'edit Coach for favorites pins');
    const favSeed = await cdp.eval(`(async () => {
      const skill = (equippedSkills && equippedSkills[0]) || 'system:canvas-coach';
      for (let i = 0; i < 32; i++) {
        await api('/api/loadout', { action: 'save', robot: editSlug,
          name: 'Cap ' + i, skill_0: skill });
      }
      await refreshFavorites();
      return { n: favList.length, slug: editSlug, first: (favList[0] && favList[0].name) || '' };
    })()`, true);
    check(favSeed.n === 32, `seeded 32 Coach favorites for FULL pin, got ${JSON.stringify(favSeed)}`);
    const fullPin = await cdp.eval(`(() => {
      const note = document.querySelector('#fav-full');
      const save = document.querySelector('#fav-save');
      const row = document.querySelector('#fav-strip .fav-row');
      if (!note || !save || !row) return { err: 'missing nodes' };
      return {
        hidden: !!note.hidden,
        text: (note.textContent || '').trim(),
        inRow: row.contains(note) && row.contains(save),
        display: getComputedStyle(note).display
      };
    })()`);
    check(!fullPin.err && fullPin.inRow && !fullPin.hidden && fullPin.display !== 'none'
      && fullPin.text.includes('FULL — 32'),
      `FULL1 #fav-full visible next to Save at 32: ${JSON.stringify(fullPin)}`);
    await cdp.eval(`(() => {
      const err = document.querySelector('#agent-err');
      if (err) err.textContent = 'Favorite saved on this relay. Save the robot to keep the loadout.';
      const i = document.querySelector('#fav-name');
      i.value = 'Brand New Cap';
      i.dispatchEvent(new Event('input', { bubbles: true }));
      window.__favSent = null;
      const real = window.fetch;
      window.fetch = (u, o) => {
        if (String(u).includes('/api/loadout') && o && o.method === 'POST') {
          try {
            const body = JSON.parse(String(o.body || '{}'));
            if (body.action === 'save') window.__favSent = body;
          } catch (e) { window.__favSent = { parse: true }; }
        }
        return real.call(window, u, o);
      };
      document.querySelector('#fav-save').click();
      window.fetch = real;
    })()`);
    await sleep(200);
    const keepPin = await cdp.eval(`({ name: document.querySelector('#fav-name').value,
      sent: window.__favSent,
      fullHidden: document.querySelector('#fav-full').hidden,
      fullText: (document.querySelector('#fav-full').textContent || '').trim(),
      agentErr: (document.querySelector('#agent-err').textContent || '') })`);
    check(keepPin.name === 'Brand New Cap',
      `KEEP1 fav-name kept after refused new save: ${JSON.stringify(keepPin)}`);
    check(keepPin.sent === null,
      `FULL2 new-name refuse at 32 sent no save request: ${JSON.stringify(keepPin)}`);
    check(!keepPin.fullHidden && keepPin.fullText.includes('FULL — 32'),
      `FULL2 notice still shown after refuse: ${JSON.stringify(keepPin)}`);
    check(keepPin.agentErr === '',
      `ERR1 refuse clears #agent-err: ${JSON.stringify(keepPin)}`);
    await cdp.eval(`(async () => {
      const i = document.querySelector('#fav-name');
      i.value = 'Cap 0';
      i.dispatchEvent(new Event('input', { bubbles: true }));
      window.__favSent = null;
      const real = window.fetch;
      window.fetch = (u, o) => {
        if (String(u).includes('/api/loadout') && o && o.method === 'POST') {
          try {
            const body = JSON.parse(String(o.body || '{}'));
            if (body.action === 'save') window.__favSent = body;
          } catch (e) { window.__favSent = { parse: true }; }
        }
        return real.call(window, u, o);
      };
      document.querySelector('#fav-save').click();
      await new Promise((r) => setTimeout(r, 800));
      window.fetch = real;
    })()`, true);
    const samePin = await cdp.eval(`({ sent: window.__favSent,
      name: document.querySelector('#fav-name').value })`);
    check(samePin.sent && samePin.sent.name === 'Cap 0',
      `same-name save at FULL still posts: ${JSON.stringify(samePin)}`);
    check(samePin.name === '',
      `KEEP1 success clears fav-name: ${JSON.stringify(samePin)}`);
    await cdp.eval(`document.querySelector('#agent-close').click()`);

    const themeKeep = await cdp.eval(`document.documentElement.getAttribute('data-theme')`);
    const qbWant = ['Inventory', 'Character', 'New channel', 'Stop'];
    for (const [w, h] of [[375, 812], [1440, 900]]) {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
      await sleep(200);
      for (const th of ['field-office', 'dark']) {
        const rows = await cdp.eval(`((th) => { document.documentElement.setAttribute('data-theme', th);
          return [...document.querySelectorAll('#quick-bar .qb-slot')].map((btn) => {
            const lab = btn.querySelector('.qb-label');
            const kbd = btn.querySelector('kbd');
            const range = document.createRange();
            range.selectNodeContents(lab);
            const tr = range.getBoundingClientRect();
            const br = btn.getBoundingClientRect();
            const x = Math.max(br.left + 2, Math.min(tr.right - 1, br.right - 2));
            const hit = document.elementFromPoint(x, (tr.top + tr.bottom) / 2);
            return { id: btn.id, text: lab.textContent, sw: btn.scrollWidth, cw: btn.clientWidth,
              kbd: kbd ? getComputedStyle(kbd).display : '',
              inside: tr.width > 0 && tr.right <= br.right + 1.5 && tr.left >= br.left - 1.5,
              hit: !!(hit && btn.contains(hit)), aria: btn.getAttribute('aria-keyshortcuts'), title: btn.title };
          }); })(${JSON.stringify(th)})`);
        console.log(`qb ${th} ${w}: ` + JSON.stringify(rows));
        check(rows.map((r) => r.text).join('|') === qbWant.join('|'), `quick-bar labels at ${w} ${th}: ${JSON.stringify(rows)}`);
        for (const r of rows) {
          check(r.sw <= r.cw + 1 && r.inside && r.hit, `quick-bar ${r.id} label fits at ${w} ${th}: ${JSON.stringify(r)}`);
          check(r.aria === String(qbWant.indexOf(r.text) + 1) && r.title.startsWith(r.text), `quick-bar ${r.id} keeps its shortcut: ${JSON.stringify(r)}`);
          if (w <= 480)
            check(r.kbd === 'none', `key chip hidden at ${w}: ${JSON.stringify(r)}`);
          else
            check(r.kbd !== 'none', `key chip stays at ${w}: ${JSON.stringify(r)}`);
        }
      }
    }

    const logProbe = async (w, h, th, force) => {
      await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
      await sleep(150);
      const expr = '(' + LOG_PROBE_FN + ')(' + JSON.stringify(th) + ',' + (force ? 'true' : 'false') + ')';
      return await cdp.eval(expr);
    };
    for (const th of ['field-office', 'dark']) {
      const phone = await logProbe(375, 812, th, false);
      const tall = await logProbe(375, 812, th, true);
      const desk = await logProbe(1440, 900, th, false);
      console.log(`log ${th}: ` + JSON.stringify({ phone, tall, desk }));
      check(phone.people >= 8 && phone.logH >= 140 && phone.inView, `375 log stays up with 8 people (${th}): ${JSON.stringify(phone)}`);
      check(phone.people >= 8 && phone.moved && phone.docH > 812 && phone.overflowY !== 'visible',
        `375 unforced 8-row page scrolls (${th}): ${JSON.stringify(phone)}`);
      check(tall.logH >= 140 && tall.docH > tall.inner && tall.moved && tall.overflowY !== 'hidden',
        `375 page scrolls when the roster is taller than the screen (${th}): ${JSON.stringify(tall)}`);
      check(desk.logH >= 300 && desk.inView, `1440 log stays up with 8 people (${th}): ${JSON.stringify(desk)}`);
    }
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: VIEW_W, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
    await cdp.eval(`document.documentElement.setAttribute('data-theme', ${JSON.stringify(themeKeep)})`);
    await sleep(200);

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
    // Pre-walk r5 (Ops FAIL-2): no code reads the picture input and Save
    // profile sends no picture, so the whole Picture row is hidden: not
    // rendered, not focusable, not in the accessibility tree, and no
    // placeholder copy in the profile.
    const pic = await cdp.eval(`(() => { const row = document.querySelector('#prof-avatar-row'); const inp = document.querySelector('#prof-avatar');
      const lab = document.querySelector('label[for="prof-avatar"]'); inp && inp.focus();
      return { row: !!row, hidden: !!row && row.hidden, display: row ? getComputedStyle(row).display : '', inRow: !!row && row.contains(inp) && row.contains(lab),
        rects: (inp ? inp.getClientRects().length : 0) + (lab ? lab.getClientRects().length : 0), focused: document.activeElement === inp,
        text: document.querySelector('#profile').innerText }; })()`);
    const ax = await cdp.send('Accessibility.getFullAXTree', {}).catch(() => null);
    const axNames = ax && ax.nodes ? ax.nodes.filter((n) => !n.ignored).map((n) => (n.name && n.name.value) || '') : null;
    check(pic.row && pic.hidden && pic.display === 'none' && pic.inRow && pic.rects === 0 && !pic.focused,
      `profile Picture row is hidden and not focusable: ${JSON.stringify(Object.assign({}, pic, { text: undefined }))}`);
    check(!/Picture|Not available yet|Choose File/.test(pic.text), `profile shows no Picture row or placeholder copy: ${JSON.stringify(pic.text.slice(0, 400))}`);
    check(axNames && axNames.length > 20 && !axNames.some((n) => /^Picture$|Not available yet/.test(n)), `profile Picture row is not in the accessibility tree`);
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

    // #234: after import, backup title is the import variant (not "created").
    await cdp.click('#use-id');
    await cdp.waitFor(`!!document.querySelector('#nsec-in')`, 'import after logout');
    await cdp.eval(`(() => { const i = document.querySelector('#nsec-in');
      i.value = 'nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5';
      i.dispatchEvent(new Event('input')); })()`);
    await sleep(400);
    await cdp.click('#do-import');
    await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup after re-import');
    t = await gateText();
    check(t.includes('Your identity key has been imported'),
      'backup after import uses imported title');
    check(!t.includes('has been created'),
      'backup after import does not say has been created');
    // F-B: reload boots splash; tick adopts session (incl. identity_imported);
    // Begin must then show the import backup title — not "created".
    // Waiting for #save-pass right after reload is wrong (splash has #begin).
    await cdp.send('Page.reload', {});
    await cdp.waitFor(`!!document.querySelector('#begin')`, 'splash after import reload');
    await sleep(400); // let tick() assign session + syncIdentityViaImport
    await cdp.click('#begin');
    await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup after import reload Begin');
    t = await gateText();
    check(t.includes('Your identity key has been imported'),
      'backup after import reload Begin still says imported');
    check(!t.includes('has been created'),
      'backup after import reload Begin does not say created');
    // B1 / Gauge P2-3: raw API logout with NO applySession — tick() must
    // poll session and route logged_out → landing (backup would stick otherwise).
    await cdp.eval(`(async () => {
      await fetch('/api/identity', { method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'logout' }) });
    })()`, true);
    await cdp.waitFor(`!!document.querySelector('#create-id')`,
      'landing after import-title logout via tick', 15000);

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
    checkSavePass(await savePassSent('Raise a robot', `document.querySelector('#raise-agent').click()`, true), true, 'box shown and checked');
    checkSavePass(await savePassSent('Raise a robot', `document.querySelector('#raise-agent').click()`, false), false, 'box shown, unchecked');
    checkSavePass(await savePassSent('Edit robot', editRobot('coach-copy'), true), false, 'update, box hidden');
    checkSavePass(await savePassSent('Edit locked robot', editRobot('coach'), true), false, 'update, box hidden');
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
