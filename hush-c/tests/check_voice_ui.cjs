// check_voice_ui.cjs: VOICE V1 capability-probe pins in a real browser.
// Two throwaway relays: "piper" has a stub piper binary on its PATH, "bare"
// has an empty PATH dir (no whisper, no piper). Pins:
//   U1 (P1.3)  voiceCaps() resolves within 2.5 s while getVoices() stays [].
//   U1b (2.2)  voiceSynthCount() alone gives up on an empty getVoices()
//              after its own 2 s wait (resolves 0 in under 2.3 s), so the
//              2.4 s outer cap in voiceCaps() is a second fence, not the only one.
//   U2 (D9)    the probe is mic-only: 0 getUserMedia calls, permissions are
//              queried for "microphone" only, enumerateDevices is used.
//   U3 (P1.4)  robot voice picker shows when whisper:false but tts non-empty.
//   U4 (P1.4)  picker stays hidden when tts is empty (gate is TTS, not STT).
//   U5 (P1.2)  stt:false,tts:[] -> only MUTE enabled, others carry a title.
//   U6 (P1.1)  /api/status in the page has stt bool, tts array, voices int.
//   U7 chip    relay drawer chip reads in plain words (Speech-to-text /
//              Read aloud / Microphone lines, no STT/TTS jargon), painted
//              with theme tokens (field-office = dossier khaki), never white.
//   U7b        ticks leave an unchanged chip alone (no aria-live churn).
//   U8 (D9, CoS 13:54) a real click on Test mic makes exactly one
//              getUserMedia call with constraints {"audio":true} (no video),
//              stops every returned track, and the chip says so in words.
//   U8b (Ops #363) every getUserMedia failure maps to a friendly line; no
//              raw DOMException name (…Error) shows anywhere in the drawer.
//   U8c        while the browser is asked, the chip says "Waiting…" and the
//              button is aria-busy; U8d: no getUserMedia -> "cannot open".
//   U9 copy    voiceCapsLines() words for every state: probing, ready, none,
//              denied, unsupported (+ prompt, unknown, piper without voices).
//   U10 icons  topic icon + state mark per chip line and a mic icon on Test
//              mic, stroked in currentColor (no literal colour), >= 12px,
//              inside the viewport, >= 3:1 on their background, and the chip
//              border >= 3:1 against the panel behind it (CoS 19:56), in all
//              8 themes x 1440/390.
// Chrome runs with its own temp profile on its own port; its pid and dir
// are logged, and only processes and dirs this script made are removed.
// Env: HUSH_RELAY_BIN, HUSH_CHROME_BIN, VOICE_UI_SHOTS (an existing dir:
// U10 also writes voice-<theme>-<width>.png there for review).
'use strict';
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');

const RELAY = process.env.HUSH_RELAY_BIN || path.join(__dirname, '..', 'hush-relay');
const PROBE_LIMIT_MS = 2500;
const SYNTH_LIMIT_MS = 2300;
const HARNESS_FENCE_MS = 6000;
const TICK_WAIT_MS = 2500;
const CONFIGS = [
  { name: '1440-field-office', w: 1440, h: 900, theme: 'field-office' },
  { name: '390-field-office', w: 390, h: 844, theme: 'field-office' },
  { name: '1440-dark', w: 1440, h: 900, theme: 'dark' },
];
// Field-office tokens the chip must resolve to (check_palette_tokens.py S2).
const FO = { fg: 'rgb(31, 28, 18)', muted: 'rgb(67, 63, 39)', surface2: 'rgb(214, 199, 157)', line: 'rgb(75, 74, 44)' };
const WHITE = 'rgb(255, 255, 255)';
const THEMES = ['dark', 'light', 'color-blind', 'dracula', 'desert', 'monochrome', 'christmas', 'field-office'];
const ICON_WIDTHS = [[1440, 900], [390, 844]];
const ICON_MIN_PX = 12;
const ICON_MIN_RATIO = 3;
// Anything that reads like a raw DOMException name or an engineering readout.
const RAW_ERROR = /[A-Za-z]+Error\b|DOMException/;
const JARGON = /\b(STT|TTS)\b|\((granted|prompt|denied|unknown)\)|\bunknown\b|\binputs?\b/i;
const TEST_OK = 'Mic opened and closed. Nothing was recorded.';
const TEST_BLOCKED = "Blocked. Allow the microphone for this page in your browser's site settings, then try again.";
const TEST_MISSING = 'No microphone found. Plug one in, then try again.';
const TEST_BUSY = 'Another app is using the mic. Close it, then try again.';
const TEST_FAILED = 'The mic test did not work. Try again.';
const MIC_ERRORS = [
  ['NotAllowedError', 'blocked', TEST_BLOCKED], ['SecurityError', 'blocked', TEST_BLOCKED],
  ['NotFoundError', 'off', TEST_MISSING], ['OverconstrainedError', 'off', TEST_MISSING],
  ['NotReadableError', 'off', TEST_BUSY], ['AbortError', 'off', TEST_BUSY],
  ['TypeError', 'off', TEST_FAILED], ['constructor', 'off', TEST_FAILED],
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const liveProcs = [];
const tempDirs = [];
function noteTemp(dir) {
  tempDirs.push(dir);
  return dir;
}
// Stops only processes this script spawned, waits for each to exit (so
// Chrome no longer writes its profile), then removes only dirs it made.
async function stopAll() {
  const procs = liveProcs.splice(0);
  await Promise.all(procs.map((p) => new Promise((resolve) => {
    if (p.exitCode !== null || p.signalCode !== null) return resolve();
    const t = setTimeout(() => { try { p.kill('SIGKILL'); } catch (e) { /* gone */ } resolve(); }, 5000);
    p.once('exit', () => { clearTimeout(t); resolve(); });
    try { p.kill('SIGTERM'); } catch (e) { clearTimeout(t); resolve(); }
  })));
  await sleep(200);
  while (tempDirs.length) {
    const dir = tempDirs.pop();
    try { fs.rmSync(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }); } catch (e) { /* best effort */ }
    if (fs.existsSync(dir)) console.error('VOICE-UI could not remove ' + dir);
  }
}
async function fail(msg) {
  console.error('voice UI check failed: ' + msg);
  await stopAll();
  process.exit(1);
}
process.on('SIGINT', () => { fail('interrupted'); });
process.on('SIGTERM', () => { fail('terminated'); });

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
  throw new Error('no Chrome on PATH (need google-chrome or chromium) and HUSH_CHROME_BIN unset');
}

async function launchChrome(chrome) {
  const port = await freePort();
  const userDir = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'vv1-chrome-')));
  const env = Object.assign({}, process.env);
  delete env.DBUS_SESSION_BUS_ADDRESS;
  const proc = spawn(chrome,
    ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run',
      '--disable-dev-shm-usage', '--disable-software-rasterizer',
      `--remote-debugging-port=${port}`, `--user-data-dir=${userDir}`, 'about:blank'],
    { stdio: ['ignore', 'ignore', 'ignore'], env });
  liveProcs.push(proc);
  console.log(`VOICE-UI chrome pid=${proc.pid} port=${port} profile=${userDir}`);
  let exited = false;
  proc.once('exit', () => { exited = true; });
  const t0 = Date.now();
  while (!exited && Date.now() - t0 < 30000) {
    try {
      const res = await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(400) });
      if (res.ok) {
        const page = (await res.json()).find((t) => t.type === 'page');
        if (page)
          return page.webSocketDebuggerUrl;
      }
    } catch (e) { /* not listening yet */ }
    await sleep(100);
  }
  return await fail('Chrome DevTools never answered');
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
    const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!awaitPromise });
    if (r.exceptionDetails)
      throw new Error('page eval threw: ' + JSON.stringify(r.exceptionDetails).slice(0, 300));
    return r.result ? r.result.value : null;
  }
  async waitFor(expr, label, timeout) {
    const t0 = Date.now();
    for (;;) {
      let v = null;
      try { v = await this.eval(expr); } catch (e) { /* page busy */ }
      if (v)
        return v;
      if (Date.now() - t0 > (timeout || 60000))
        return await fail('timeout waiting for ' + (label || expr));
      await sleep(150);
    }
  }
}

// Installed before any page script: getVoices() stays [] with no
// voiceschanged, and every mic/camera entry point is counted.
const SPY = `(() => {
  window.__vv1 = { gum: 0, constraints: [], stopped: 0, enumerate: 0, perms: [], mode: 'ok', release: null };
  /* Harness fence: a hung probe resolves fallback after ms, so a broken
     page fails a pin instead of hanging this script. */
  window.__vv1within = (p, ms, fallback) => Promise.race([p, new Promise((r) => setTimeout(() => r(fallback), ms))]);
  try {
    if (window.speechSynthesis) speechSynthesis.getVoices = () => [];
  } catch (e) { /* no synth */ }
  const md = navigator.mediaDevices;
  if (md) {
    const realEnum = md.enumerateDevices && md.enumerateDevices.bind(md);
    /* mode: 'ok' | 'hang' (resolves on __vv1.release()) | 'silent' (no
       tracks) | 'reject:<name>' (DOMException with that name). */
    md.getUserMedia = function (c) {
      const v = window.__vv1;
      v.gum++;
      v.constraints.push(JSON.stringify(c));
      const track = { kind: 'audio', stop() { v.stopped++; } };
      const stream = { getTracks: () => [track] };
      if (v.mode.indexOf('reject:') === 0) return Promise.reject(new DOMException('mic refused', v.mode.slice(7)));
      if (v.mode === 'silent') return Promise.resolve({ getTracks: () => [] });
      if (v.mode === 'hang') return new Promise((r) => { v.release = () => r(stream); });
      return Promise.resolve(stream);
    };
    md.enumerateDevices = function () { window.__vv1.enumerate++; return realEnum ? realEnum() : Promise.resolve([]); };
  }
  if (navigator.permissions && navigator.permissions.query) {
    const realQuery = navigator.permissions.query.bind(navigator.permissions);
    navigator.permissions.query = function (d) { window.__vv1.perms.push(d && d.name); return realQuery(d); };
  }
})();`;

// Chip lines as "LABEL value" (label upper-cased the way CSS shows it).
const CHIP_READ = `(() => { const e = document.getElementById('voice-caps'); const cs = getComputedStyle(e);
  const r = e.getBoundingClientRect();
  const lines = [...e.querySelectorAll('.voice-cap')].map((l) => ({ key: l.getAttribute('data-cap'), state: l.getAttribute('data-state'),
    text: l.querySelector('.voice-cap-k').textContent.toUpperCase() + ' ' + l.querySelector('.voice-cap-v').textContent }));
  return { text: e.innerText, lines, fg: cs.color, bg: cs.backgroundColor, border: cs.borderTopColor, w: r.width, h: r.height }; })()`;
// Every visible word and every title/aria-label/alt in the relay drawer.
const DRAWER_TEXT = `(() => { const d = document.getElementById('relay-drawer');
  return d.innerText + ' ' + [...d.querySelectorAll('*')].map((n) => ['title', 'aria-label', 'alt'].map((a) => n.getAttribute(a) || '').join(' ')).join(' '); })()`;
// [label, caps patch, line key, mark, words]. Base caps: relay with nothing,
// browser probed, mic supported, 1 input, permission prompt.
const COPY_ROWS = [
  ['probing', { probing: true }, 'mic', 'wait', 'Checking\u2026'],
  ['ready stt', { stt: true }, 'stt', 'ok', 'Ready (Whisper).'],
  ['ready piper', { tts: ['piper', 'espeak'], voices: 12 }, 'tts', 'ok', 'Ready (Piper, eSpeak; 12 voices).'],
  ['ready espeak', { tts: ['espeak'] }, 'tts', 'ok', 'Ready (eSpeak).'],
  ['ready mic', { mic: { inputs: 2, permission: 'granted', supported: true } }, 'mic', 'ok', '2 found, allowed.'],
  ['prompt mic', {}, 'mic', 'ok', '1 found. Your browser will ask before using it.'],
  ['none stt', {}, 'stt', 'off', 'Not installed on this relay.'],
  ['none tts', {}, 'tts', 'off', 'Not installed on this relay.'],
  ['piper no voices', { tts: ['piper'], voices: 0 }, 'tts', 'off', 'Piper is installed, but no voices are.'],
  ['none mic', { mic: { inputs: 0, permission: 'prompt', supported: true } }, 'mic', 'off', 'None found. Plug one in.'],
  ['unknown mic', { mic: { inputs: null, permission: 'unknown', supported: true } }, 'mic', 'wait', 'Not checked yet. Press Test mic.'],
  ['denied mic', { mic: { inputs: 1, permission: 'denied', supported: true } }, 'mic', 'blocked', "Blocked. Allow it for this page in your browser's site settings."],
  ['unsupported mic', { mic: { inputs: null, permission: 'unknown', supported: false } }, 'mic', 'off', 'This browser cannot use a microphone on this page.'],
  ['test silent', { micCheck: 'silent' }, 'test', 'off', 'The mic sent no sound. Check it, then try again.'],
  ['no test line', {}, 'test', null, null],
];
const COPY_CASES = COPY_ROWS.length;
const COPY_TABLE = `(async () => { const rows = ${JSON.stringify(COPY_ROWS)}; const bad = []; let n = 0;
  const raw = ${RAW_ERROR}; const jargon = ${JARGON};
  for (const [name, patch, key, mark, words] of rows) {
    const caps = Object.assign({ probing: false, stt: false, tts: [], voices: 0, mic: { inputs: 1, permission: 'prompt', supported: true },
      browserVoices: 0, browserStt: false, micCheck: '' }, patch);
    const lines = voiceCapsLines(caps); n++;
    const ln = lines.find((l) => l.key === key) || { mark: null, text: null };
    const all = lines.map((l) => l.label + ' ' + l.text).join(' | ');
    if (ln.mark !== mark || ln.text !== words || raw.test(all) || jargon.test(all)) bad.push({ name, got: ln, all });
  }
  return { n, bad }; })()`;

// Opens the drawer in THEME with a Mic test line showing, then measures every
// voice icon: stroke resolves to the element colour (currentColor), no
// literal stroke/fill attribute, >= ICON_MIN_PX, inside the viewport, and
// stroke vs composited background >= ICON_MIN_RATIO.
const ICON_MEASURE = `(async () => {
  for (const d of document.querySelectorAll('.drawer.show')) d.classList.remove('show');
  document.documentElement.setAttribute('data-theme', THEME);
  await voiceBrowserCaps(); voiceBrowser.micCheck = 'blocked';
  document.getElementById('relay-drawer').classList.add('show'); paintRelayDrawer(lastStatus);
  document.getElementById('voice-mic-test').scrollIntoView({ block: 'center' });
  await new Promise((r) => setTimeout(r, 120));
  // Canvas normalises any CSS colour (rgb, oklch, ...) to sRGB bytes.
  const cv = document.createElement('canvas').getContext('2d', { willReadFrequently: true });
  const rgba = (s) => { if (!s || s === 'none') return null; cv.clearRect(0, 0, 1, 1); cv.fillStyle = '#000'; cv.fillStyle = s;
    cv.fillRect(0, 0, 1, 1); const d = cv.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255]; };
  const lin = (v) => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
  const lum = (c) => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
  const over = (t, u) => [0, 1, 2].map((i) => t[i] * t[3] + u[i] * (1 - t[3])).concat(1);
  const bgOf = (el) => { const layers = []; for (let n = el; n && n.nodeType === 1; n = n.parentElement) {
      const c = rgba(getComputedStyle(n).backgroundColor); if (c && c[3] > 0) { layers.push(c); if (c[3] >= 1) break; } }
    let base = [255, 255, 255, 1]; for (let i = layers.length - 1; i >= 0; i--) base = layers[i][3] >= 1 ? layers[i] : over(layers[i], base); return base; };
  const icons = [...document.querySelectorAll('#voice-caps svg, #voice-mic-test svg')].map((s) => {
    const cs = getComputedStyle(s); const r = s.getBoundingClientRect(); const stroke = rgba(cs.stroke); const bg = bgOf(s);
    const col = stroke ? over(stroke, bg) : null;
    const ratio = col ? (Math.max(lum(col), lum(bg)) + 0.05) / (Math.min(lum(col), lum(bg)) + 0.05) : 0;
    const literal = [s, ...s.querySelectorAll('*')].some((n) => n.hasAttribute('stroke') || n.hasAttribute('fill') || n.hasAttribute('style'));
    const where = s.closest('#voice-mic-test') ? 'button' : (s.getAttribute('data-role') || '?');
    const ok = !!stroke && cs.stroke === cs.color && !literal && r.width >= ${ICON_MIN_PX} && r.height >= ${ICON_MIN_PX}
      && r.left >= 0 && r.right <= innerWidth && ratio >= ${ICON_MIN_RATIO} && s.getAttribute('aria-hidden') === 'true';
    return { where, icon: s.getAttribute('data-icon'), ok, ratio: Math.round(ratio * 100) / 100, w: r.width, h: r.height, stroke: cs.stroke, color: cs.color, literal };
  });
  const chip = document.getElementById('voice-caps'); const ccs = getComputedStyle(chip);
  const behind = bgOf(chip.parentElement); const edge = rgba(ccs.borderTopColor);
  const edgeCol = edge ? over(edge, behind) : behind;
  const border = { width: parseFloat(ccs.borderTopWidth), ratio: Math.round(((Math.max(lum(edgeCol), lum(behind)) + 0.05) / (Math.min(lum(edgeCol), lum(behind)) + 0.05)) * 100) / 100,
    color: ccs.borderTopColor, same: ['Right', 'Bottom', 'Left'].every((s) => ccs['border' + s + 'Color'] === ccs.borderTopColor) };
  voiceBrowser.micCheck = ''; document.getElementById('relay-drawer').classList.remove('show');
  return { theme: document.documentElement.getAttribute('data-theme'), icons, border,
    topics: icons.filter((x) => x.where === 'topic').map((x) => x.icon), marks: icons.filter((x) => x.where === 'mark').length,
    button: (icons.find((x) => x.where === 'button') || {}).icon,
    minRatio: Math.min(...icons.map((x) => x.ratio)), minPx: Math.min(...icons.map((x) => Math.min(x.w, x.h))) };
})()`;

async function startRelay(label, binDir, extraEnv) {
  const home = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), `vv1-${label}-home-`)));
  const cfg = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), `vv1-${label}-cfg-`)));
  fs.mkdirSync(path.join(home, 'voices', 'piper'), { recursive: true });
  fs.writeFileSync(path.join(home, 'voices', 'piper', 'en_US-amy-low.onnx'), '');
  const port = await freePort();
  const env = Object.assign({}, process.env, {
    PATH: binDir, HUSH_HOME: home, HUSH_CONFIG_DIR: cfg, HUSH_PASS_HELPER: '/nonexistent-hush-pass-helper',
  }, extraEnv || {});
  delete env.HUSH_WHISPER;
  const relay = spawn(RELAY, ['--no-open', String(port)], { env, stdio: 'ignore' });
  liveProcs.push(relay);
  console.log(`VOICE-UI relay ${label} pid=${relay.pid} port=${port} home=${home}`);
  const tokenFile = path.join(home, 'session.token');
  for (let i = 0; i < 200; i++) {
    try {
      if ((await fetch(`http://127.0.0.1:${port}/`)).ok && fs.existsSync(tokenFile))
        return { port, token: fs.readFileSync(tokenFile, 'utf8').trim() };
    } catch (e) { /* not up */ }
    await sleep(100);
  }
  return await fail(`relay ${label} did not come up on ${port}`);
}

async function main() {
  const work = noteTemp(fs.mkdtempSync(path.join(os.tmpdir(), 'vv1-bin-')));
  const piperBin = path.join(work, 'piper-bin');
  const bareBin = path.join(work, 'bare-bin');
  fs.mkdirSync(piperBin);
  fs.mkdirSync(bareBin);
  fs.writeFileSync(path.join(piperBin, 'piper'), '#!/bin/sh\nexit 0\n', { mode: 0o755 });
  const relays = { piper: await startRelay('piper', piperBin), bare: await startRelay('bare', bareBin) };

  const cdp = new Cdp(await launchChrome(resolveChrome()));
  await cdp.connect();
  await cdp.send('Page.enable', {});
  await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: SPY });
  const jsClick = async (sel) => {
    await cdp.waitFor(`!!document.querySelector('${sel}')`, sel);
    await cdp.eval(`document.querySelector('${sel}').click()`);
  };
  const results = [];
  const record = (pin, cfg, ok, info) => {
    results.push({ pin, cfg, ok });
    console.log(`${ok ? 'PIN ok  ' : 'PIN FAIL'} ${pin} ${cfg} ${JSON.stringify(info)}`);
  };

  const onboarded = {};
  const openHive = async (label, c) => {
    const base = `http://127.0.0.1:${relays[label].port}/`;
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: c.w, height: c.h, deviceScaleFactor: 1, mobile: false });
    // #336: the relay hands its cookie only to the one-time pairing link, so
    // seed the cookie a paired window holds. Cookies ignore the port, so the
    // two relays share one slot: set this relay's token before every visit.
    await cdp.send('Network.setCookie', { name: 'hush_session', value: relays[label].token,
      url: base, httpOnly: true, sameSite: 'Strict' });
    await cdp.send('Page.navigate', { url: base });
    if (!onboarded[label]) {
      // State-driven gate walk: a tick() repaint can replace a gate button
      // between our query and click, so retry whichever step is showing.
      const t0 = Date.now();
      for (;;) {
        const step = await cdp.eval(`(() => {
          if (document.querySelector('#hive.show')) return 'hive';
          for (const id of ['meet-payne', 'do-vibe', 'ack-key', 'create-id', 'begin']) {
            const e = document.getElementById(id);
            if (!e) continue;
            if (id === 'do-vibe') document.getElementById('vibe-name').value = 'VOICEHIVE';
            e.click();
            return id;
          }
          return 'none';
        })()`).catch(() => 'busy');
        if (step === 'hive')
          break;
        if (Date.now() - t0 > 60000)
          return await fail(`gate walk stuck on ${label} at step ${step}`);
        await sleep(400);
      }
      onboarded[label] = true;
    }
    await cdp.eval(`localStorage.setItem('hush-theme', ${JSON.stringify(c.theme)})`);
    await cdp.send('Page.reload', {});
    await sleep(200);
    await cdp.waitFor(`!!document.querySelector('#hive.show') && document.documentElement.getAttribute('data-theme') === ${JSON.stringify(c.theme)} && lastStatus && lastStatus.ok`, `hive ${label} at ${c.name}`);
  };

  for (const c of CONFIGS) {
    // ---- piper relay: whisper false, tts ["piper"], voices 1
    await openHive('piper', c);
    const st = await cdp.eval(`(async () => (await fetch('/api/status')).json())()`, true);
    record('U6', c.name + '/piper', typeof st.stt === 'boolean' && Array.isArray(st.tts) && Number.isInteger(st.voices)
      && st.whisper === false && JSON.stringify(st.tts) === '["piper"]' && st.voices === 1,
      { whisper: st.whisper, stt: st.stt, tts: st.tts, voices: st.voices });

    const probe = await cdp.eval(`(async () => {
      voiceBrowserProbe = null; voiceBrowser = null;
      const t0 = performance.now();
      const caps = await __vv1within(voiceCaps(lastStatus), ${HARNESS_FENCE_MS}, { browserVoices: undefined, mic: null });
      return { ms: Math.round(performance.now() - t0), caps, spy: window.__vv1,
        synthVoices: window.speechSynthesis ? speechSynthesis.getVoices().length : null };
    })()`, true);
    record('U1', c.name, probe.ms < PROBE_LIMIT_MS && probe.synthVoices === 0 && probe.caps.browserVoices !== undefined,
      { ms: probe.ms, synthVoices: probe.synthVoices, browserVoices: probe.caps.browserVoices });
    const synth = await cdp.eval(`(async () => { const t0 = performance.now(); const n = await __vv1within(voiceSynthCount(), ${HARNESS_FENCE_MS}, -1);
      return { ms: Math.round(performance.now() - t0), n }; })()`, true);
    record('U1b', c.name, synth.n === 0 && synth.ms < SYNTH_LIMIT_MS, synth);
    const spy = probe.spy;
    record('U2', c.name, spy.gum === 0 && spy.enumerate >= 1 && spy.perms.length >= 1
      && spy.perms.every((n) => n === 'microphone'),
      { getUserMedia: spy.gum, enumerateDevices: spy.enumerate, permissions: spy.perms, mic: probe.caps.mic });

    await cdp.waitFor(`ttsReady === true`, 'ttsReady on piper relay');
    const pick = await cdp.eval(`(() => { paintVoiceRow(''); const w = document.getElementById('agent-voice-wrap');
      return { hidden: w.hidden, options: document.getElementById('agent-voice').options.length, whisperReady, ttsReady }; })()`);
    record('U3', c.name, pick.hidden === false && pick.options > 1 && pick.whisperReady === false, pick);

    await jsClick('#stats');
    await cdp.waitFor(`document.getElementById('relay-drawer').classList.contains('show') && !!document.querySelector('#voice-caps [data-cap=tts]') && document.querySelector('#voice-caps [data-cap=mic]').getAttribute('data-state') !== 'wait'`, 'chip');
    const chip = await cdp.eval(CHIP_READ);
    const lineOf = (k) => (chip.lines.find((l) => l.key === k) || {}).text;
    const textOk = lineOf('stt') === 'SPEECH-TO-TEXT Not installed on this relay.'
      && lineOf('tts') === 'READ ALOUD Ready (Piper; 1 voice).'
      && /^MICROPHONE (None found\. Plug one in\.|\d+ found(, allowed\.|\. Your browser will ask before using it\.)|Not checked yet\. Press Test mic\.)$/.test(lineOf('mic'))
      && chip.lines.length === 3 && !JARGON.test(chip.text) && !RAW_ERROR.test(chip.text);
    const paintOk = c.theme === 'field-office'
      ? ((chip.fg === FO.fg || chip.fg === FO.muted) && chip.bg === FO.surface2 && chip.border === FO.line)
      : (chip.bg !== WHITE && chip.fg !== WHITE);
    record('U7', c.name, textOk && paintOk && chip.w > 0 && chip.h > 0 && chip.bg !== WHITE, chip);
    // U7b: ticks (1 s) do not rebuild an unchanged chip, so the aria-live
    // region is not re-announced every second.
    await cdp.eval(`window.__vv1row = document.querySelector('#voice-caps .voice-cap')`);
    const t0 = Date.now();
    await sleep(TICK_WAIT_MS);
    const kept = await cdp.eval(`({ same: document.querySelector('#voice-caps .voice-cap') === window.__vv1row, live: document.getElementById('voice-caps').getAttribute('aria-live'), role: document.getElementById('voice-caps').getAttribute('role') })`);
    record('U7b', c.name, kept.same && kept.live === 'polite' && kept.role === 'status', Object.assign({ waitedMs: Date.now() - t0 }, kept));
    const mt = await cdp.eval(`(() => { const e = document.getElementById('voice-mic-test');
      e.scrollIntoView({ block: 'center' }); const r = e.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2, h: r.height, before: window.__vv1.gum }; })()`);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: mt.x, y: mt.y });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: mt.x, y: mt.y, button: 'left', clickCount: 1, buttons: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: mt.x, y: mt.y, button: 'left', clickCount: 1, buttons: 0 });
    await cdp.waitFor(`(() => { const t = document.querySelector('#voice-caps [data-cap=test]'); return !!t && t.getAttribute('data-state') === 'ok'; })()`, 'mic test result', 10000);
    const mic = await cdp.eval(`({ spy: window.__vv1, test: document.querySelector('#voice-caps [data-cap=test] .voice-cap-v').textContent,
      busy: document.getElementById('voice-mic-test').getAttribute('aria-busy') })`);
    record('U8', c.name, mt.before === 0 && mic.spy.gum === 1 && mic.spy.constraints.length === 1
      && mic.spy.constraints[0] === '{"audio":true}' && mic.spy.stopped === 1 && mic.test === TEST_OK && mic.busy === null,
      { before: mt.before, getUserMedia: mic.spy.gum, constraints: mic.spy.constraints, stopped: mic.spy.stopped, buttonH: mt.h, test: mic.test });

    // U8b: each failure name -> its friendly line; nothing raw anywhere in the drawer.
    const denied = [];
    for (const [name, mark, want] of MIC_ERRORS) {
      const got = await cdp.eval(`(async () => { window.__vv1.mode = ${JSON.stringify('reject:' + name)};
        const code = await __vv1within(voiceMicCheck(), ${HARNESS_FENCE_MS}, 'hung');
        const t = document.querySelector('#voice-caps [data-cap=test]');
        return { code, mark: t && t.getAttribute('data-state'), text: t && t.querySelector('.voice-cap-v').textContent, drawer: DRAWER_TEXT() }; })()`.replace('DRAWER_TEXT()', DRAWER_TEXT), true);
      denied.push({ name, code: got.code, ok: got.mark === mark && got.text === want && !RAW_ERROR.test(got.drawer) && got.drawer.indexOf(name) < 0, text: got.text });
    }
    record('U8b', c.name, denied.length === MIC_ERRORS.length && denied.every((d) => d.ok), denied);

    // U8c: pending state while the browser asks.
    await cdp.eval(`window.__vv1.mode = 'hang'; document.getElementById('voice-mic-test').click()`);
    await cdp.waitFor(`typeof window.__vv1.release === 'function'`, 'hang armed', 10000);
    const pend = await cdp.eval(`(() => { const t = document.querySelector('#voice-caps [data-cap=test]');
      return { mark: t.getAttribute('data-state'), text: t.querySelector('.voice-cap-v').textContent,
        busy: document.getElementById('voice-mic-test').getAttribute('aria-busy'), again: window.__vv1.gum }; })()`);
    // Fenced: with the busy guard broken the second call would hang on the
    // held getUserMedia, and the pin must fail rather than hang.
    const again = await cdp.eval(`(async () => { const n = window.__vv1.gum; const first = window.__vv1.release;
      const r = await __vv1within(voiceMicCheck(), ${HARNESS_FENCE_MS}, 'hung');
      const extra = window.__vv1.gum - n; if (window.__vv1.release !== first) window.__vv1.release();
      window.__vv1.release = first; return { r, extra }; })()`, true);
    await cdp.eval(`window.__vv1.release(); window.__vv1.release = null; window.__vv1.mode = 'ok'`);
    await cdp.waitFor(`document.querySelector('#voice-caps [data-cap=test]').getAttribute('data-state') === 'ok'`, 'hang released', 10000);
    const after = await cdp.eval(`document.getElementById('voice-mic-test').getAttribute('aria-busy')`);
    record('U8c', c.name, pend.mark === 'wait' && pend.text === 'Waiting for your browser\u2026' && pend.busy === 'true'
      && again.r === 'pending' && again.extra === 0 && after === null, { pend, again, after });

    // U8d: no getUserMedia at all (plain-http host, old browser).
    const unsup = await cdp.eval(`(async () => { const md = navigator.mediaDevices; const keep = md.getUserMedia; md.getUserMedia = undefined;
      try { const code = await voiceMicCheck(); const t = document.querySelector('#voice-caps [data-cap=test]');
        return { code, mark: t.getAttribute('data-state'), text: t.querySelector('.voice-cap-v').textContent }; }
      finally { md.getUserMedia = keep; } })()`, true);
    // ...and the automatic probe flags it, so the Microphone line says so too.
    const unsupProbe = await cdp.eval(`(async () => { const md = navigator.mediaDevices; const keep = md.getUserMedia; md.getUserMedia = undefined;
      try { voiceBrowserProbe = null; voiceBrowser = null; const caps = await __vv1within(voiceCaps(lastStatus), ${HARNESS_FENCE_MS}, null);
        paintRelayDrawer(lastStatus); const t = document.querySelector('#voice-caps [data-cap=mic]');
        return { supported: caps && caps.mic.supported, mark: t.getAttribute('data-state'), text: t.querySelector('.voice-cap-v').textContent }; }
      finally { md.getUserMedia = keep; voiceBrowserProbe = null; voiceBrowser = null; await voiceBrowserCaps(); } })()`, true);
    record('U8d', c.name, unsup.code === 'unsupported' && unsup.mark === 'off'
      && unsup.text === 'This browser cannot open a microphone on this page.'
      && unsupProbe.supported === false && unsupProbe.mark === 'off'
      && unsupProbe.text === 'This browser cannot use a microphone on this page.', { unsup, unsupProbe });

    // U9: the words for every state, from the pure chip model.
    const copy = await cdp.eval(COPY_TABLE, true);
    record('U9', c.name, copy.bad.length === 0 && copy.n === COPY_CASES, copy);
    await cdp.eval(`document.getElementById('relay-close').click()`);

    // ---- bare relay: no whisper, no piper
    await openHive('bare', c);
    await sleep(300);
    const bare = await cdp.eval(`(async () => {
      const st = await (await fetch('/api/status')).json();
      paintVoiceRow('');
      const caps = await __vv1within(voiceCaps(st), ${HARNESS_FENCE_MS}, null);
      const modes = caps ? voiceTargetModes(caps) : { none: {}, cos: {}, all: {}, one: {} };
      return { st: { stt: st.stt, tts: st.tts, voices: st.voices, whisper: st.whisper }, ttsReady,
        hidden: document.getElementById('agent-voice-wrap').hidden, modes };
    })()`, true);
    record('U4', c.name, bare.ttsReady === false && bare.hidden === true && JSON.stringify(bare.st.tts) === '[]',
      { ttsReady: bare.ttsReady, hidden: bare.hidden, st: bare.st });
    const m = bare.modes;
    record('U5', c.name, bare.st.stt === false && m.none.enabled === true
      && ['cos', 'all', 'one'].every((k) => m[k].enabled === false && /Whisper/.test(m[k].title)),
      { st: bare.st, modes: m });
  }

  // ---- U10: icons in every theme at both widths (piper relay, 4 chip lines).
  await openHive('piper', CONFIGS[0]);
  for (const [w, hgt] of ICON_WIDTHS) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: w, height: hgt, deviceScaleFactor: 1, mobile: false });
    for (const theme of THEMES) {
      const ic = await cdp.eval(ICON_MEASURE.replace('THEME', JSON.stringify(theme)), true);
      if (process.env.VOICE_UI_SHOTS) {
        // Optional review shots: repaint with the Mic test line and capture.
        await cdp.eval(`(() => { voiceBrowser.micCheck = 'blocked'; document.getElementById('relay-drawer').classList.add('show');
          paintRelayDrawer(lastStatus); document.getElementById('voice-mic-test').scrollIntoView({ block: 'center' }); })()`);
        await sleep(150);
        const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
        fs.writeFileSync(path.join(process.env.VOICE_UI_SHOTS, `voice-${theme}-${w}.png`), Buffer.from(shot.data, 'base64'));
        await cdp.eval(`voiceBrowser.micCheck = ''; document.getElementById('relay-drawer').classList.remove('show')`);
      }
      const want = { topic: ['stt', 'tts', 'mic', 'test'], mark: 4, button: 'mic' };
      const ok = ic.theme === theme && JSON.stringify(ic.topics) === JSON.stringify(want.topic) && ic.marks === want.mark
        && ic.button === want.button && ic.icons.length === 9 && ic.icons.every((x) => x.ok)
        && ic.border.width >= 2 && ic.border.same && ic.border.ratio >= ICON_MIN_RATIO;
      record('U10', `${theme}@${w}`, ok, { theme: ic.theme, n: ic.icons.length, minRatio: ic.minRatio, minPx: ic.minPx, border: ic.border,
        bad: ic.icons.filter((x) => !x.ok) });
    }
  }

  await stopAll();
  console.log('VOICE-UI cleanup: spawned processes stopped, temp dirs removed');
  const bad = results.filter((r) => !r.ok);
  const per = {};
  for (const r of results) {
    per[r.pin] = per[r.pin] || { pass: 0, fail: 0 };
    per[r.pin][r.ok ? 'pass' : 'fail']++;
  }
  console.log('VOICE-UI SUMMARY ' + JSON.stringify(per));
  if (!results.length) {
    console.error('voice UI check failed: no pin ran');
    process.exit(1);
  }
  if (bad.length) {
    console.error(`voice UI check failed: ${bad.length}/${results.length} pin runs failed: ` + bad.map((r) => `${r.pin} ${r.cfg}`).join('; '));
    process.exit(1);
  }
  console.log(`voice UI check passed: ${results.length}/${results.length} pin runs (${Object.keys(per).join(',')} x ${CONFIGS.length} configs)`);
}

main().catch((e) => fail(e && e.stack ? e.stack : String(e)));
