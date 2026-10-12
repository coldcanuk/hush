// check_approve_ui.cjs: behaviour proof that the Settings "Robot turns"
// radio mirrors the saved approval_mode (#279 r2, Gauge P2-2). Drives
// headless system Chrome over CDP with the Node standard library only (no
// npm packages, nothing installed). Every assertion reads the rendered
// radio, never page source.
//
//   U1 a page load shows the saved setting (boot sync)
//   U2 a change made elsewhere reaches an open page (1 s tick sync)
//   U3 a tick during a Settings post does not flip the radio back
//   U4 the radio shows what the server saved, not what was clicked
//   U5 a refused post snaps the radio back to the saved setting
//   U7 (#314) in light, field office and dark, at 1440 and 390 wide, the
//      help line under Robot turns (and under Theme) sits at least 8 px
//      below the pills
//   U8 (#314) at 390 wide with the root font at 125% to 250%, no
//      Robot turns pill clips its text or crosses the panel's content edge
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN
// (required; tests/check_approve_ui.sh resolves it), HUSH_TEST_WAIT_S
// (Chrome DevTools ready deadline, default 30).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const RELAY = process.env.HUSH_RELAY_BIN || path.join(ROOT, 'hush-relay');
const CHROME = process.env.HUSH_CHROME_BIN || '';
const EVERY = 'approve_every_action';
const AUTO = 'auto_approve';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const procs = [];
const dirs = [];

/* A child that ignores SIGTERM this long gets SIGKILL. */
const STOP_WAIT_MS = 5000;

/* Stops one child this check started and resolves once it has exited, so
 * nothing writes into its temp dir after the dir is removed. */
function stopProc(p) {
  return new Promise((resolve) => {
    if (p.exitCode !== null || p.signalCode !== null) { resolve(); return; }
    const timer = setTimeout(() => { try { p.kill('SIGKILL'); } catch (e) { /* gone */ } },
      STOP_WAIT_MS);
    p.once('exit', () => { clearTimeout(timer); resolve(); });
    try { p.kill('SIGTERM'); } catch (e) { clearTimeout(timer); resolve(); }
  });
}
/* Stops every child first, then removes the temp HOME and Chrome profile. */
async function cleanup() {
  await Promise.all(procs.splice(0).map(stopProc));
  for (const d of dirs.splice(0)) {
    try { fs.rmSync(d, { recursive: true, force: true }); } catch (e) { /* best effort */ }
  }
}
class CheckFailed extends Error {}
/* Ends the current step; the top level cleans up and exits 1. */
function fail(msg) {
  throw new CheckFailed(msg);
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
function tmpDir(tag) {
  const d = fs.mkdtempSync(path.join(os.tmpdir(), tag));
  dirs.push(d);
  return d;
}

/* Starts the relay in a throwaway HOME and walks it to a ready vibe. */
async function startRelay() {
  const home = tmpDir('hush-approve-ui-');
  const env = Object.assign({}, process.env, {
    HOME: home, HUSH_HOME: path.join(home, '.hush'),
    HUSH_CONFIG_DIR: path.join(home, '.config', 'hush'),
    HUSH_PASS_HELPER: path.join(ROOT, 'tests', 'fake-pass.sh'),
    HUSH_FAKE_PASS_DIR: path.join(home, 'pass'), HUSH_AUTO_UPDATE: '0',
  });
  delete env.XDG_CONFIG_HOME;
  for (const d of [env.HUSH_HOME, env.HUSH_CONFIG_DIR, env.HUSH_FAKE_PASS_DIR])
    fs.mkdirSync(d, { recursive: true });
  const port = await freePort();
  const tokenFile = path.join(env.HUSH_HOME, 'session.token');
  const headers = () => {
    try {
      return { 'X-Hush-Token': fs.readFileSync(tokenFile, 'utf8').trim(),
               'Content-Type': 'application/json' };
    } catch (e) { return { 'Content-Type': 'application/json' }; }
  };
  procs.push(spawn(RELAY, ['--no-open', String(port)], { env, stdio: 'ignore' }));
  const base = `http://127.0.0.1:${port}`;
  for (let i = 0; ; i++) {
    try {
      if ((await fetch(base + '/api/session', { headers: headers() })).ok) break;
    } catch (e) { /* not up */ }
    if (i > 200) fail('relay did not come up');
    await sleep(50);
  }
  const post = async (p, body) => {
    const r = await fetch(base + p, { method: 'POST', headers: headers(), body: JSON.stringify(body) });
    if (!r.ok) fail(`POST ${p} ${JSON.stringify(body)} -> ${r.status}`);
    return r.json();
  };
  await post('/api/identity', { action: 'create' });
  await post('/api/identity', { action: 'ack_backup', save_pass: true });
  await post('/api/vibe', { name: 'HQ', about: 'approve ui' });
  await post('/api/profile', { first_name: 'Chuck', last_name: 'P', email: '', organization: '', theme: 'dark' });
  const mode = async () =>
    (await (await fetch(base + '/api/session', { headers: headers() })).json()).approval_mode;
  return { base, post, mode };
}

/* Launches headless Chrome and returns a minimal CDP client on its page. */
async function startChrome() {
  if (!CHROME) fail('HUSH_CHROME_BIN is unset (run tests/check_approve_ui.sh)');
  const port = await freePort();
  const env = Object.assign({}, process.env);
  delete env.DBUS_SESSION_BUS_ADDRESS;
  procs.push(spawn(CHROME, ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
    '--disable-dev-shm-usage', '--disable-software-rasterizer', `--remote-debugging-port=${port}`,
    `--user-data-dir=${tmpDir('hush-approve-ui-chrome-')}`, 'about:blank'], { stdio: 'ignore', env }));
  const waitS = Number(process.env.HUSH_TEST_WAIT_S || '30');
  let page = null;
  for (const t0 = Date.now(); !page; await sleep(200)) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      page = list.find((t) => t.type === 'page') || null;
    } catch (e) { /* not up */ }
    if (!page && Date.now() - t0 > waitS * 1000) fail('Chrome DevTools never answered');
  }
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let id = 0;
  const waiters = new Map();
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data.toString());
    if (m.id && waiters.has(m.id)) { waiters.get(m.id)(m.result || {}); waiters.delete(m.id); }
  };
  const send = (method, params) => new Promise((resolve) => {
    const n = ++id;
    waiters.set(n, resolve);
    ws.send(JSON.stringify({ id: n, method, params: params || {} }));
  });
  const evaluate = async (expr) => {
    const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    return r.exceptionDetails ? undefined : (r.result ? r.result.value : undefined);
  };
  await send('Page.enable');
  return { send, evaluate };
}

const RADIO = `(() => { const c = document.querySelector("input[name='approval']:checked");
  return c ? c.value : ""; })()`;
const CLICK = (v) => `document.querySelector("input[name='approval'][value='${v}']").click()`;
/* Freezes the 1 s tick (its /api/session fetch never answers) and answers
 * POST /api/profile with `reply` (a function body run in the page). */
const STUB = (reply) => `(() => { const f = window.fetch;
  window.fetch = (u, o) => {
    if (String(u).includes('/api/session')) return new Promise(() => {});
    if (String(u).includes('/api/profile') && o && o.method === 'POST') { ${reply} }
    return f(u, o);
  }; return true; })()`;

async function waitRadio(page, want, ms, msg) {
  let got = '';
  for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(100)) {
    got = await page.evaluate(RADIO);
    if (got === want) return;
  }
  fail(`${msg} (radio=${got}, want ${want})`);
}
/* U7/U8 (#314) viewports, themes and root text sizes. */
const WIDE_W = 1440;
const PHONE_W = 390;
const VIEW_H = 900;
const MIN_HELP_GAP_PX = 8;
const GAP_THEMES = ['light', 'field-office', 'dark'];
/* 125% and 150% are the sizes #314 names; on headless Chrome a nowrap pill
 * still fits there (it takes its own row), so 200% and 250% are where the
 * old nowrap rule pushes "Approve every action" past the panel. */
const BIG_TEXT = ['125%', '150%', '200%', '250%'];
/* Settle time after a viewport, theme or font change before measuring. */
const LAYOUT_MS = 200;

async function settings(cdp, width, theme, fontSize) {
  await cdp.send('Emulation.setDeviceMetricsOverride',
    { width, height: VIEW_H, deviceScaleFactor: 1, mobile: false });
  await cdp.evaluate(`(() => { applyTheme(${JSON.stringify(theme)});
    document.documentElement.style.fontSize = ${JSON.stringify(fontSize)};
    openSettings(); return true; })()`);
  await sleep(LAYOUT_MS);
}
/* Pixels from the bottom of group to the top of the help line under it. */
const GAP = (group) => `(() => { const g = document.getElementById(${JSON.stringify(group)});
  const h = g && g.nextElementSibling;
  if (!g || !h || !h.classList.contains('help')) return null;
  return Math.round(h.getBoundingClientRect().top - g.getBoundingClientRect().bottom); })()`;

/* U7: the help line under each Settings radio group keeps a visible gap. */
async function checkHelpGap(cdp) {
  for (const theme of GAP_THEMES) {
    for (const width of [WIDE_W, PHONE_W]) {
      await settings(cdp, width, theme, '');
      const robot = await cdp.evaluate(GAP('approval-list'));
      if (robot === null || robot < MIN_HELP_GAP_PX)
        fail(`U7 help line ${robot} px under Robot turns (${theme}, ${width})`);
      const look = await cdp.evaluate(GAP('theme-list'));
      if (look === null || look < MIN_HELP_GAP_PX)
        fail(`U7 help line ${look} px under Theme (${theme}, ${width})`);
    }
  }
}
/* Labels of #approval-list that clip their text or leave the panel. */
const CLIPPED = `(() => { const panel = document.querySelector('#settings .panel');
  const edge = panel.getBoundingClientRect().right - parseFloat(getComputedStyle(panel).paddingRight);
  return [...document.querySelectorAll('#approval-list label')]
    .filter((l) => l.scrollWidth > l.clientWidth || l.getBoundingClientRect().right > edge)
    .map((l) => l.textContent.trim()); })()`;

/* U8: at 390 px and large text every Robot turns pill fits. */
async function checkFit390(cdp) {
  for (const size of BIG_TEXT) {
    await settings(cdp, PHONE_W, 'field-office', size);
    const clipped = await cdp.evaluate(CLIPPED);
    if (!Array.isArray(clipped)) fail('U8 could not measure the Robot turns pills');
    if (clipped.length) fail(`U8 ${clipped.join(', ')} clips at 390 (root font ${size})`);
  }
}

async function load(cdp, base) {
  await cdp.send('Page.navigate', { url: base + '/' });
  for (let i = 0; i < 120; i++) {
    if (await cdp.evaluate(`typeof session !== 'undefined' && !!session.logged_in &&
        !!document.querySelector("input[name='approval']")`)) return;
    await sleep(100);
  }
  fail('the page never loaded a logged-in session');
}

(async () => {
  const relay = await startRelay();
  const cdp = await startChrome();
  await relay.post('/api/profile', { approval_mode: EVERY });
  await load(cdp, relay.base);
  await waitRadio(cdp, EVERY, 3000, 'U1 a page load must show the saved setting');
  await relay.post('/api/profile', { approval_mode: AUTO });
  await waitRadio(cdp, AUTO, 3500, 'U2 a change made elsewhere must reach the open page');

  // U3: the post waits 2.5 s before it is sent; ticks keep reading AUTO.
  await cdp.evaluate(`(() => { const f = window.fetch;
    window.fetch = (u, o) => (String(u).includes('/api/profile') && o && o.method === 'POST')
      ? new Promise((r) => setTimeout(r, 2500)).then(() => f(u, o)) : f(u, o); return true; })()`);
  await cdp.evaluate(CLICK(EVERY));
  await sleep(1800);
  const mid = await cdp.evaluate(RADIO);
  if (mid !== EVERY) fail(`U3 a tick during the Settings post must not flip the radio back (radio=${mid})`);
  for (let i = 0; (await relay.mode()) !== EVERY; i++) {
    if (i > 40) fail('U3 the click must save approve_every_action');
    await sleep(100);
  }
  await sleep(1500);
  await waitRadio(cdp, EVERY, 1000, 'U3 the saved setting shows after the post');

  // U4: the server answers the click with a different saved value.
  await relay.post('/api/profile', { approval_mode: AUTO });
  await load(cdp, relay.base);
  await waitRadio(cdp, AUTO, 3000, 'U4 setup');
  await cdp.evaluate(STUB(`return Promise.resolve(new Response(JSON.stringify(
      Object.assign({}, session, { approval_mode: '${AUTO}' })), { status: 200,
      headers: { 'Content-Type': 'application/json' } }));`));
  await cdp.evaluate(CLICK(EVERY));
  await waitRadio(cdp, AUTO, 1500, 'U4 the radio must show what the server saved, not what was clicked');

  // U5: the server refuses the click.
  await load(cdp, relay.base);
  await waitRadio(cdp, AUTO, 3000, 'U5 setup');
  await cdp.evaluate(STUB(`return Promise.resolve(new Response(
      'approval_mode must be auto_approve or approve_every_action.\\n', { status: 400 }));`));
  await cdp.evaluate(CLICK(EVERY));
  await sleep(300);
  await waitRadio(cdp, AUTO, 1500, 'U5 a refused post must snap the radio back');
  if ((await relay.mode()) !== AUTO) fail('U5 a refused post must not change the setting');
  await checkHelpGap(cdp);
  await checkFit390(cdp);
  await cleanup();
  console.log('approve UI checks ok');
  process.exit(0);
})().catch(async (e) => {
  await cleanup();
  const why = e instanceof CheckFailed ? e.message : (e && e.stack ? e.stack : String(e));
  console.error('approve UI check failed: ' + why);
  process.exit(1);
});
