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
//   U8 (#314) at 390 wide in light, field office and dark, with the root
//      font at 100% to 250%, no Robot turns pill clips its text or crosses
//      the panel's content edge, its text sits at least 3 px inside its
//      border top and bottom, "Auto-approve" stays on one line, and at
//      100% every pill stays on one line
//
// Env: HUSH_RELAY_BIN (default <repo>/hush-relay), HUSH_CHROME_BIN
// (required; tests/check_approve_ui.sh resolves it), HUSH_TEST_WAIT_S
// (Chrome DevTools ready deadline, default 30).
//
//   U6 (#312) under Approve every action, the owner's mention shows a
//      neutral "<Robot> · waiting for your OK" pill (no 👍) while a turn
//      waits, the 👍 after the owner's Yes, and no pill or bar after a No.
//      A two-robot mention shows "waiting" only from each robot's own ask:
//      the queued robot shows no pill until the relay asks it, and the relay
//      asks it after the first robot's No, Yes with no runtime, or Yes and
//      run (stub runtime). Each end state is checked live and after a reload.
//      The state is per mention.
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
/* U6: the 👍 shows 2 s after a mention; waiting past that proves it stays hidden. */
const HELD_WAIT_MS = 3000;
const ACK_WAIT_MS = 6000;
const ASK_HAPPY = 'Approval needed: Happy wants to take a turn. Reply Yes or No in this thread.';
const RAN_HAPPY = 'No selected provider is ready for Happy.';
const DECLINED_HAPPY = 'Turn declined: Happy ';
const WAITING_COPY = ' · waiting for your OK';
const SAY_YES = 'tell a joke';
const SAY_PAIR = 'both answer here';
const SAY_NO = 'skip this one';
const SAY_IN = 'one more in here';
const SAY_PAIR_NO = 'first one skips';
const SAY_PAIR_RUN = 'stub runtime pair';
const STUB_REPLY = 'Stub runtime reply.';
const askOf = (name) => 'Approval needed: ' + name + ' wants ';
const ranOf = (name) => 'No selected provider is ready for ' + name + '.';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const procs = [];
const dirs = [];
/* Every temp dir this check made, so the end can prove none is left. */
const made = [];

/* A child that ignores SIGTERM this long gets SIGKILL. */
const STOP_WAIT_MS = 5000;

/* Stops one child this check started and resolves once it has exited.
 * That narrows writes into its temp dir after the dir is removed; it does
 * not rule them out (a Chrome helper may still be writing). */
function stopProc(p) {
  return new Promise((resolve) => {
    /* A Chrome group gets SIGKILL once its main process exits, so no helper
     * outlives it; removeDir then checks the profile stays gone. */
    const done = () => { if (p.group) signalProc(p, 'SIGKILL'); resolve(); };
    if (p.exitCode !== null || p.signalCode !== null) { done(); return; }
    const timer = setTimeout(() => signalProc(p, 'SIGKILL'), STOP_WAIT_MS);
    p.once('exit', () => { clearTimeout(timer); done(); });
    signalProc(p, 'SIGTERM');
  });
}
/* Signals one child this check started; Chrome is spawned detached, so its
 * signal goes to its own process group (the helpers that write into its
 * profile go with it). Only children this check spawned are signalled. */
function signalProc(p, sig) {
  try { if (p.group) process.kill(-p.pid, sig); else p.kill(sig); }
  catch (e) { try { p.kill(sig); } catch (e2) { /* gone */ } }
}
/* A removed temp dir that something re-creates is removed again for this long. */
const RM_WAIT_MS = 3000;
/* Removes d and checks it stays gone; returns false if it is still there. */
async function removeDir(d) {
  for (const t0 = Date.now(); Date.now() - t0 < RM_WAIT_MS; await sleep(100)) {
    try { fs.rmSync(d, { recursive: true, force: true, maxRetries: 5, retryDelay: 50 }); } catch (e) { /* retry */ }
    await sleep(100);
    if (!fs.existsSync(d)) return true;
  }
  return !fs.existsSync(d);
}
/* Stops every child first, then removes the temp HOMEs and Chrome profile.
 * Returns the dirs that could not be removed. */
async function cleanup() {
  await Promise.all(procs.splice(0).map(stopProc));
  for (const d of dirs.splice(0)) await removeDir(d);
  return made.filter((d) => fs.existsSync(d));
}
/* Last resort on any exit path the async cleanup did not reach. */
process.on('exit', () => {
  for (const p of procs) signalProc(p, 'SIGKILL');
  for (const d of dirs) { try { fs.rmSync(d, { recursive: true, force: true }); } catch (e) { /* best effort */ } }
});
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
  made.push(d);
  return d;
}

/* U6 stub runtime: a `codex` script in the throwaway HOME, first on the
 * relay's PATH, so codex robots run a turn that ends with one reply. No real
 * runtime starts; grok-build robots stay "no provider ready" (no grok login). */
function stubRuntime(home) {
  const bin = path.join(home, 'bin');
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(bin, 'codex'), '#!/bin/sh\nprintf "%s\\n" "' + STUB_REPLY + '"\n', { mode: 0o755 });
  fs.mkdirSync(path.join(home, '.codex'), { recursive: true });
  fs.writeFileSync(path.join(home, '.codex', 'auth.json'), '{"ok":true}\n');
  return bin;
}

/* The relay's session token file, for events(); the check runs one relay. */
let eventsTokenFile = '';

/* Starts the relay in a throwaway HOME and walks it to a ready vibe. */
async function startRelay() {
  const home = tmpDir('hush-approve-ui-');
  const env = Object.assign({}, process.env, {
    HOME: home, HUSH_HOME: path.join(home, '.hush'),
    HUSH_CONFIG_DIR: path.join(home, '.config', 'hush'),
    HUSH_PASS_HELPER: path.join(ROOT, 'tests', 'fake-pass.sh'),
    HUSH_FAKE_PASS_DIR: path.join(home, 'pass'), HUSH_AUTO_UPDATE: '0',
  });
  env.PATH = stubRuntime(home) + path.delimiter + (process.env.PATH || '');
  delete env.XDG_CONFIG_HOME;
  for (const d of [env.HUSH_HOME, env.HUSH_CONFIG_DIR, env.HUSH_FAKE_PASS_DIR])
    fs.mkdirSync(d, { recursive: true });
  eventsTokenFile = path.join(env.HUSH_HOME, 'session.token');
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
    `--user-data-dir=${tmpDir('hush-approve-ui-chrome-')}`, 'about:blank'], { stdio: 'ignore', env, detached: true }));
  procs[procs.length - 1].group = true;
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
/* 100% is the default phone view (both pills must stay one line there);
 * 125% and 150% are the sizes #314 names; 200% and 250% are where the old
 * nowrap rule pushed "Approve every action" past the panel. */
const FIT_TEXT = ['100%', '125%', '150%', '200%', '250%'];
const MIN_TEXT_INSET_PX = 3;
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
/* Each #approval-list label: its text, whether it clips or leaves the
 * panel, its text's line count, and the gap from the text's glyph box to
 * the inside of the border, top and bottom. */
const PILL_FIT = `(() => { const panel = document.querySelector('#settings .panel');
  const edge = panel.getBoundingClientRect().right - parseFloat(getComputedStyle(panel).paddingRight);
  return [...document.querySelectorAll('#approval-list label')].map((l) => {
    const box = l.getBoundingClientRect();
    const r = document.createRange();
    r.selectNodeContents(l); r.setStartAfter(l.querySelector('input'));
    const rects = [...r.getClientRects()].filter((x) => x.width > 0);
    const top = Math.min(...rects.map((x) => x.top));
    const bottom = Math.max(...rects.map((x) => x.bottom));
    return { text: l.textContent.trim(), lines: new Set(rects.map((x) => Math.round(x.top))).size,
      clip: l.scrollWidth > l.clientWidth || box.right > edge,
      insetTop: top - (box.top + l.clientTop),
      insetBottom: box.top + l.clientTop + l.clientHeight - bottom };
  }); })()`;

/* The first U8 complaint about one pill at one theme and text size. */
function pillFault(p, size) {
  if (p.clip) return 'clips';
  if (!(p.insetTop >= MIN_TEXT_INSET_PX && p.insetBottom >= MIN_TEXT_INSET_PX))
    return `text inset ${p.insetTop.toFixed(1)}/${p.insetBottom.toFixed(1)} px (< ${MIN_TEXT_INSET_PX})`;
  if (p.text === 'Auto-approve' && p.lines !== 1) return `breaks onto ${p.lines} lines`;
  if (size === '100%' && p.lines !== 1) return `wraps onto ${p.lines} lines at default size`;
  return '';
}

/* U8: at 390 px, default and large text, every Robot turns pill fits. */
async function checkFit390(cdp) {
  for (const theme of GAP_THEMES) {
    for (const size of FIT_TEXT) {
      await settings(cdp, PHONE_W, theme, size);
      const pills = await cdp.evaluate(PILL_FIT);
      if (!Array.isArray(pills) || pills.length !== 2) fail('U8 could not measure the Robot turns pills');
      for (const p of pills) {
        const why = pillFault(p, size);
        if (why) fail(`U8 ${p.text} ${why} at 390 (${theme}, root font ${size})`);
      }
    }
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

/* U6 (#312). The ack pills under the owner's stream note that says `say`,
 * as [{ cls, text }] (a held pill would still show a 👍, whatever its class). */
const PILLS_ON = (say) => `[...document.querySelectorAll('#stream .note:not(.reply)')]
  .filter((n) => n.textContent.includes(${JSON.stringify(say)}))
  .flatMap((n) => [...n.querySelectorAll('.robot-ack')])
  .map((p) => ({ cls: p.className, text: p.textContent }))`;
/* Ack bars (even empty ones) under that note. */
const BARS_ON = (say) => `[...document.querySelectorAll('#stream .note:not(.reply)')]
  .filter((n) => n.textContent.includes(${JSON.stringify(say)}))
  .reduce((k, n) => k + n.querySelectorAll('.robot-acks').length, 0)`;
/* True when the waiting pill under `say` uses the theme's own ink and rule
 * (--muted text, --line dashed border), checked against a probe element. */
const WAIT_THEMED = (say) => `(() => {
  const n = [...document.querySelectorAll('#stream .note:not(.reply)')]
    .find((x) => x.textContent.includes(${JSON.stringify(say)}));
  const p = n && n.querySelector('.robot-ack.waiting');
  if (!p) return false;
  const probe = document.createElement('span');
  probe.style.color = 'var(--muted)'; probe.style.borderColor = 'var(--line)';
  document.body.appendChild(probe);
  const want = getComputedStyle(probe); const got = getComputedStyle(p);
  const ok = got.color === want.color && got.borderTopColor === want.borderTopColor &&
    got.borderTopStyle === 'dashed';
  probe.remove();
  return ok;
})()`;
/* The neutral waiting pill for robot name: exact UI_SPEC copy, no emoji. */
const isWaiting = (p, name) => /\bwaiting\b/.test(p.cls) && p.text === name + WAITING_COPY &&
  !/\bemoji\b/.test(p.cls);
/* A settled ack pill for robot name (👍 for the first robot, 🎯 the second). */
const isDone = (p, name, emoji) => /\bemoji\b/.test(p.cls) && p.text === emoji + name;
/* The pills under `say` match `want` ([[kind, name, emoji]]) in order. */
async function pillsAre(cdp, say, want) {
  const pills = await cdp.evaluate(PILLS_ON(say));
  const ok = Array.isArray(pills) && pills.length === want.length && want.every(([kind, name, emoji], i) =>
    (kind === 'wait' ? isWaiting(pills[i], name) : isDone(pills[i], name, emoji)));
  return { ok, pills };
}
/* Waits up to ACK_WAIT_MS for the pills under `say` to match `want`. */
async function waitPills(cdp, say, want, msg) {
  let last = null;
  for (const t0 = Date.now(); Date.now() - t0 < ACK_WAIT_MS; await sleep(100)) {
    last = await pillsAre(cdp, say, want);
    if (last.ok) return;
  }
  fail(`${msg} (${JSON.stringify(last && last.pills)})`);
}
/* Past the 2 s 👍 delay the pills under `say` still match `want`. */
async function holdPills(cdp, say, want, msg) {
  await sleep(HELD_WAIT_MS);
  const got = await pillsAre(cdp, say, want);
  if (!got.ok) fail(`${msg} (${JSON.stringify(got.pills)})`);
}

/* Every note the relay holds. */
async function events(relay) {
  return (await (await fetch(relay.base + '/api/events', { headers: { 'X-Hush-Token': fs.readFileSync(eventsTokenFile, 'utf8').trim() } })).json()).events || [];
}
/* Waits for a relay note that starts with `head` and whose id is not in `skip`. */
async function waitEvent(relay, head, msg, skip) {
  for (const t0 = Date.now(); Date.now() - t0 < ACK_WAIT_MS; await sleep(100)) {
    const hit = (await events(relay)).find((e) =>
      String(e.content || '').startsWith(head) && !(skip || []).includes(e.id));
    if (hit) return hit;
  }
  return fail(msg);
}
/* Posts the owner's note `say` naming robots (in a thread when replyTo). */
async function mention(relay, robots, say, replyTo) {
  const ev = { kind: 1, channel: 'welcome', content: robots.map((r) => 'nostr:' + r.npub).join(' ') + ' ' + say };
  robots.forEach((r, i) => { ev['mention_' + i] = r.npub; });
  if (replyTo) ev.reply_to = replyTo;
  await relay.post('/api/event', ev);
}
async function answer(relay, root, text) {
  await relay.post('/api/event', { kind: 1, channel: 'welcome', reply_to: root, content: text });
}
async function askIds(relay) {
  return (await events(relay)).filter((e) => String(e.content || '').startsWith('Approval needed: ')).map((e) => e.id);
}

/* U6: a held turn shows the waiting pill; the owner's Yes brings the 👍. */
async function checkHeldAck(relay, cdp) {
  await relay.post('/api/profile', { approval_mode: EVERY });
  const bots = {};
  /* Happy and Scout have no runtime ready; Ace and Bolt run the stub. */
  for (const [name, provider] of [['Happy', 'grok-build'], ['Scout', 'grok-build'],
    ['Ace', 'codex'], ['Bolt', 'codex']]) {
    const raised = await relay.post('/api/agent', { name, system_prompt: 'Tell jokes.',
      provider, save_pass: true });
    bots[name] = (raised.agents || []).find((a) => a.name === name);
    if (!bots[name]) fail(`U6 setup: ${name} was not raised`);
  }
  await load(cdp, relay.base);
  await mention(relay, [bots.Happy], SAY_YES, '');
  const ask = await waitEvent(relay, ASK_HAPPY, 'U6 the turn must wait for approval', []);
  await holdPills(cdp, SAY_YES, [['wait', 'Happy']], 'U6 a held turn must show one neutral waiting pill, no 👍');
  if (!(await cdp.evaluate(WAIT_THEMED(SAY_YES))))
    fail('U6 the waiting pill must use the theme ink and a dashed theme rule');
  await answer(relay, ask.reply_to, 'Yes');
  await waitEvent(relay, RAN_HAPPY, 'U6 the Yes must run the turn', []);
  await waitPills(cdp, SAY_YES, [['done', 'Happy', '👍']], 'U6 the 👍 must show after the owner\'s Yes');
  return { bots, root: ask.reply_to };
}

/* Waits for the pills under `say`, reloads the page, and checks the same
 * pills come back from the stored notes. */
async function pillsLiveAndReloaded(relay, cdp, say, want, msg) {
  await waitPills(cdp, say, want, msg);
  await load(cdp, relay.base);
  await waitPills(cdp, say, want, msg + ' (after a reload)');
}
/* Thread notes (root and replies) that start with head. */
async function inThread(relay, root, head) {
  return (await events(relay)).filter((e) => (e.id === root || e.reply_to === root) &&
    String(e.content || '').startsWith(head));
}
/* Waits until root's thread holds n notes that start with head. */
async function waitInThread(relay, root, head, n, msg) {
  for (const t0 = Date.now(); Date.now() - t0 < ACK_WAIT_MS; await sleep(100)) {
    if ((await inThread(relay, root, head)).length >= n) return;
  }
  fail(msg);
}

/* U6 pair (#312): "@<lead> @<partner>" queues two turns. While the lead is
 * asked only the lead waits (the partner has no ask, so no pill). After the
 * owner's answer to the lead, the relay asks the partner in the same thread
 * and the partner's own waiting pill shows; the partner's Yes then settles
 * it. pair = { lead, partner, say, leadAnswer, leadDone, leadWant }:
 * leadDone is the relay note that ends the lead's turn, leadWant the lead's
 * pill once it ended ([] after a No). */
async function checkPair(relay, cdp, pair) {
  const { lead, partner, say } = pair;
  const seen = await askIds(relay);
  await mention(relay, [lead, partner], say, '');
  const ask = await waitEvent(relay, askOf(lead.name), `U6 pair ${say}: ${lead.name} must ask first`, seen);
  const root = ask.reply_to;
  if ((await inThread(relay, root, askOf(partner.name))).length !== 0)
    fail(`U6 pair ${say}: ${partner.name} must not be asked before ${lead.name} is answered`);
  await holdPills(cdp, say, [['wait', lead.name]],
    `U6 pair ${say}: only ${lead.name} waits; ${partner.name} has no ask yet, so no pill`);
  await answer(relay, root, pair.leadAnswer);
  await waitInThread(relay, root, pair.leadDone, 1, `U6 pair ${say}: ${pair.leadAnswer} must end ${lead.name}'s turn`);
  await waitInThread(relay, root, askOf(partner.name), 1,
    `U6 pair ${say}: after ${lead.name}'s ${pair.leadAnswer} the relay must ask ${partner.name}`);
  await pillsLiveAndReloaded(relay, cdp, say, [...pair.leadWant, ['wait', partner.name]],
    `U6 pair ${say}: ${partner.name} waits on its own ask after ${lead.name}'s ${pair.leadAnswer}`);
  await answer(relay, root, 'Yes');
  await waitInThread(relay, root, pair.partnerDone, pair.partnerDone === pair.leadDone ? 2 : 1,
    `U6 pair ${say}: the Yes must run ${partner.name}`);
  await sleep(HELD_WAIT_MS);
  const asks = (await inThread(relay, root, 'Approval needed: ')).length;
  if (asks !== 2) fail(`U6 pair ${say}: the pair must end with exactly 2 asks (got ${asks})`);
  await pillsLiveAndReloaded(relay, cdp, say, [...pair.leadWant, ['done', partner.name, '🎯']],
    `U6 pair ${say}: end state after ${partner.name}'s Yes`);
}

/* U6 pair end states: the lead's No, the lead's Yes with no runtime ready,
 * and the lead's Yes with a (stub) runtime that runs and replies. */
async function checkPairs(relay, cdp, bots) {
  const noRun = (name) => ranOf(name);
  await checkPair(relay, cdp, { lead: bots.Happy, partner: bots.Scout, say: SAY_PAIR_NO,
    leadAnswer: 'No', leadDone: DECLINED_HAPPY, leadWant: [], partnerDone: noRun('Scout') });
  await checkPair(relay, cdp, { lead: bots.Happy, partner: bots.Scout, say: SAY_PAIR,
    leadAnswer: 'Yes', leadDone: noRun('Happy'), leadWant: [['done', 'Happy', '👍']],
    partnerDone: noRun('Scout') });
  await checkPair(relay, cdp, { lead: bots.Ace, partner: bots.Bolt, say: SAY_PAIR_RUN,
    leadAnswer: 'Yes', leadDone: STUB_REPLY, leadWant: [['done', 'Ace', '👍']],
    partnerDone: STUB_REPLY });
}

/* U6 after No: a declined turn shows no pill and no ack bar; a No on a
 * later mention in the Yes thread leaves the Yes note's 👍 (per mention). */
async function checkDeclinedAck(relay, cdp, bots, yesRoot) {
  let seen = await askIds(relay);
  await mention(relay, [bots.Happy], SAY_NO, '');
  const ask = await waitEvent(relay, ASK_HAPPY, 'U6 the turn must wait for approval', seen);
  await holdPills(cdp, SAY_NO, [['wait', 'Happy']], 'U6 a held turn must show one neutral waiting pill, no 👍');
  await answer(relay, ask.reply_to, 'No');
  await waitEvent(relay, DECLINED_HAPPY, 'U6 the No must decline the turn', []);
  await holdPills(cdp, SAY_NO, [], 'U6 no ack pill after the owner\'s No');
  if ((await cdp.evaluate(BARS_ON(SAY_NO))) !== 0) fail('U6 an empty ack bar is drawn after the owner\'s No');
  seen = await askIds(relay);
  const declines = (await events(relay)).filter((e) => String(e.content).startsWith(DECLINED_HAPPY)).map((e) => e.id);
  await mention(relay, [bots.Happy], SAY_IN, yesRoot);
  const inAsk = await waitEvent(relay, ASK_HAPPY, 'U6 the in-thread mention must ask', seen);
  await answer(relay, inAsk.reply_to, 'No');
  await waitEvent(relay, DECLINED_HAPPY, 'U6 the in-thread No must decline', declines);
  await holdPills(cdp, SAY_YES, [['done', 'Happy', '👍']],
    'U6 a later No in the same thread must leave the Yes note\'s 👍');
}

/* U6 unit (#312): the page's own turnState and mentionAckPhase on fixed
 * thread notes (edge cases that are slow or awkward to drive live). */
const TURN_AT = (events, id, name, jobs) => `(() => {
  const keep = lastEvents;
  try {
    lastEvents = ${JSON.stringify(events)};
    const bot = mentionRoster().find((h) => h && h.name === ${JSON.stringify(name)});
    return ${jobs ? `mentionAckPhase(${JSON.stringify(id)}, bot, ${JSON.stringify(jobs)})`
      : `turnState(${JSON.stringify(id)}, bot)`};
  } finally { lastEvents = keep; }
})()`;
const WHO = `({ me: session.pubkey, bots: mentionRoster().filter((h) => h.kind !== 'human')
  .map((h) => ({ name: h.name, npub: h.npub, pk: h.pubkey || h.npub })) })`;
/* Builds one fixture note; more is { text, root, ms }. */
function note(id, pubkey, at, more) {
  return { id, pubkey, content: more.text, created_at: at, reply_to: more.root || '', channel: 'welcome',
    kind: 1, mentions: more.ms || [] };
}
/* Fixture threads, keyed by the case they pin. */
function turnFixtures(who) {
  const h = who.bots.find((b) => b.name === 'Happy');
  const s = who.bots.find((b) => b.name === 'Scout');
  const chap = 'c'.repeat(64);
  const askText = (name) => `Approval needed: ${name || 'Happy'} wants to take a turn. Reply Yes or No in this thread.`;
  const ask = (id, at, root, name) => note(id, chap, at, { text: askText(name), root });
  const top = (id, at, root, ms) => note(id, who.me, at, { text: 'nostr:x go', root, ms: ms || [h.npub] });
  const yes = (id, at, root, t) => note(id, who.me, at, { text: t || 'Yes', root });
  const rob = (id, at, root, t) => note(id, h.pk, at, { text: t, root });
  return {
    reply: [top('m1', 100), ask('a1', 101, 'm1'), yes('y1', 102, 'm1'), rob('r1', 103, 'm1', 'Here is a joke.')],
    intro: [top('m2', 100), ask('a2', 101, 'm2'), yes('y2', 102, 'm2'), rob('i2', 103, 'm2', 'At ease. Coach here.')],
    latest: [top('m3', 100), ask('a3', 101, 'm3'), yes('y3', 102, 'm3'), rob('r3', 103, 'm3', 'Done.'),
      ask('b3', 104, 'm3'), yes('n3', 105, 'm3', 'No'), note('d3', chap, 106, { text: 'Turn declined: Happy stood down.', root: 'm3' })],
    prefix: [top('m4', 100), ask('x4', 101, 'm4', 'Happyish'), ask('a4', 102, 'm4'), yes('y4', 103, 'm4'),
      rob('r4', 104, 'm4', 'Done.')],
    pair: [top('m5', 100, '', [h.npub, s.npub]), ask('a5', 101, 'm5')],
    pairDone: [top('m6', 100, '', [h.npub, s.npub]), ask('a6', 101, 'm6'), yes('y6', 102, 'm6'),
      rob('r6', 103, 'm6', 'Done.'), ask('b6', 104, 'm6', 'Scout'), yes('z6', 105, 'm6'),
      note('s6', s.pk, 106, { text: 'Here too.', root: 'm6' })],
    pairMid: [top('m7', 100, '', [h.npub, s.npub]), ask('a7', 101, 'm7'), yes('y7', 102, 'm7'),
      rob('r7', 103, 'm7', 'Done.'), ask('b7', 104, 'm7', 'Scout')],
    robotRoot: [rob('r8', 99, '', 'A robot top note.'), top('m8', 100, 'r8'), ask('a8', 101, 'r8')],
    twoNotes: [top('m9', 100), ask('a9', 101, 'm9'), yes('y9', 102, 'm9'), rob('f9', 103, 'm9', 'Using another provider.'),
      rob('r9', 104, 'm9', 'Done.'), top('m10', 105, 'm9'), ask('a10', 106, 'm9')],
    otherThread: [top('m11', 100), ask('a11', 101, 'm11'), rob('o11', 102, 'elsewhere', 'Done.')],
    pairAskElsewhere: [top('m12', 100, '', [h.npub, s.npub]), ask('a12', 101, 'elsewhere')],
    pairAskEarlier: [top('m13', 100), ask('a13', 101, 'm13'), top('m14', 102, 'm13', [h.npub, s.npub])],
  };
}
/* U6 unit cases: [fixture, mention id, robot, jobs, want, message]. */
const TURN_CASES = [
  ['reply', 'm1', 'Happy', null, '', 'a robot reply after Yes must settle the turn'],
  ['intro', 'm2', 'Happy', null, 'held', 'an intro must not settle the turn'],
  ['intro', 'm2', 'Happy', [{ name: 'Happy' }], 'live', 'a live job must show thinking/reacting, not the turn state'],
  ['latest', 'm3', 'Happy', null, 'declined', 'the latest settle (a No) must win over an earlier run'],
  ['prefix', 'm4', 'Happy', null, '', 'another robot\'s ask whose name starts with Happy must not count'],
  ['pair', 'm5', 'Scout', null, 'queued', 'a co-mentioned robot with no ask of its own is queued (no waiting pill)'],
  ['pair', 'm5', 'Happy', null, 'held', 'the asked robot of a pair waits'],
  ['pairAskElsewhere', 'm12', 'Scout', null, '', 'an ask in another thread must not queue this mention\'s robot'],
  ['pairAskEarlier', 'm14', 'Scout', null, '', 'an ask from an earlier mention in the thread must not queue this one\'s robot'],
  ['pairMid', 'm7', 'Scout', null, 'held', 'the second robot waits while its own ask is open'],
  ['pairDone', 'm6', 'Scout', null, '', 'the second robot settles after its own Yes and reply'],
  ['pairDone', 'm6', 'Happy', null, '', 'the first robot stays settled after the second ask'],
  ['robotRoot', 'm8', 'Happy', null, 'held', 'a robot-authored thread root must not settle a held turn'],
  ['twoNotes', 'm10', 'Happy', null, 'held', 'two notes from an earlier turn must not settle a later held turn'],
  ['twoNotes', 'm9', 'Happy', null, '', 'a later mention in the thread leaves the earlier turn settled'],
  ['otherThread', 'm11', 'Happy', null, 'held', 'a robot note in another thread must not settle this turn'],
];
/* A queued robot shows no pill at all: never a waiting pill without its own ask. */
const QUEUED_PILL = `ackPill({ name: 'Scout' }, 1, 'queued', null)`;
async function checkTurnUnits(cdp) {
  if ((await cdp.evaluate(QUEUED_PILL)) !== null)
    fail('U6 unit queued: a queued robot must show no pill (no ask to answer)');
  const who = await cdp.evaluate(WHO);
  if (!who || !who.me || !who.bots) fail('U6 unit setup: no session or roster');
  const fx = turnFixtures(who);
  for (const [key, id, name, jobs, want, msg] of TURN_CASES) {
    const got = await cdp.evaluate(TURN_AT(fx[key], id, name, jobs));
    const ok = want === 'live' ? (got === 'thinking' || got === 'reacting') : got === want;
    if (!ok) fail(`U6 unit ${key}: ${msg} (got ${JSON.stringify(got)}, want ${want})`);
  }
}

/* U6 entry: rendered held/Yes, pair, No and per-mention cases, then units. */
async function checkAcks(relay, cdp) {
  const { bots, root } = await checkHeldAck(relay, cdp);
  await checkPairs(relay, cdp, bots);
  await checkDeclinedAck(relay, cdp, bots, root);
  await checkTurnUnits(cdp);
}

(async () => {
  const relay = await startRelay();
  const cdp = await startChrome();
  await checkAcks(relay, cdp);
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
  const left = await cleanup();
  if (left.length) {
    console.error('approve UI check failed: temp dirs left behind: ' + left.join(' '));
    process.exit(1);
  }
  console.log('approve UI checks ok');
  process.exit(0);
})().catch(async (e) => {
  await cleanup();
  const why = e instanceof CheckFailed ? e.message : (e && e.stack ? e.stack : String(e));
  console.error('approve UI check failed: ' + why);
  process.exit(1);
});
