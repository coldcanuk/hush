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
    const beginRatio = RATIO_OF(contrasts.splash['#begin']);
    check(beginRatio !== null && beginRatio >= 4.5, `BEGIN contrast ${JSON.stringify(contrasts.splash['#begin'])} < 4.5`);
    await cdp.shot('splash-begin');

    await cdp.click('#begin');
    await cdp.waitFor(`document.querySelector('#gate').textContent.includes('Use an existing key')`, 'landing');
    t = await gateText();
    check(!t.includes('did not survive the restart'), 'virgin landing shows no restart note');

    await cdp.click('#create-id');
    await cdp.waitFor(`!!document.querySelector('#save-pass')`, 'backup');
    const checked = await cdp.eval(`document.querySelector('#save-pass').checked`);
    check(checked === false, 'backup checkbox renders unchecked without pass');
    Object.assign(contrasts, { backup: await sample(['#ack-key', '#reveal-key', '#copy-key']) });
    t = await gateText();
    check(!t.includes('Checked to save'), 'backup label drops the Checked-to-save line without pass');
    check(!t.includes('Uncheck the box'), 'backup drops the Uncheck line without pass');
    check(t.includes('Saving to pass is unavailable'), 'backup shows the no-pass reason');
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
    await cdp.click('#hive-close');
    await cdp.waitFor(`!!document.querySelector('#hive-leave.show')`, 'leave chooser');
    Object.assign(contrasts, { leave: await sample(['#leave-exit', '#leave-close', '#leave-cancel']) });
    await cdp.click('#leave-cancel');

    await cdp.click('#profile-btn');
    await cdp.waitFor(`document.querySelector('#profile.show')`, 'profile drawer');
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
    check(t.includes('Checked to save'), 'backup keeps the Checked-to-save line with pass');
    await cdp.shot('backup-withpass');
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
