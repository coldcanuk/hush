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
//   U7 chip    relay drawer chip text names STT/TTS, painted with theme
//              tokens (field-office = dossier khaki), never white.
//   U8 (D9, CoS 13:54) a real click on Test mic makes exactly one
//              getUserMedia call with constraints {"audio":true} (no video),
//              stops every returned track, and the chip shows the result.
// Chrome runs with its own temp profile on its own port; its pid and dir
// are logged, and only processes and dirs this script made are removed.
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
const CONFIGS = [
  { name: '1440-field-office', w: 1440, h: 900, theme: 'field-office' },
  { name: '390-field-office', w: 390, h: 844, theme: 'field-office' },
  { name: '1440-dark', w: 1440, h: 900, theme: 'dark' },
];
// Field-office tokens the chip must resolve to (check_palette_tokens.py S2).
const FO = { fg: 'rgb(31, 28, 18)', muted: 'rgb(67, 63, 39)', surface2: 'rgb(214, 199, 157)', line: 'rgb(75, 74, 44)' };
const WHITE = 'rgb(255, 255, 255)';

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
  window.__vv1 = { gum: 0, constraints: [], stopped: 0, enumerate: 0, perms: [] };
  /* Harness fence: a hung probe resolves fallback after ms, so a broken
     page fails a pin instead of hanging this script. */
  window.__vv1within = (p, ms, fallback) => Promise.race([p, new Promise((r) => setTimeout(() => r(fallback), ms))]);
  try {
    if (window.speechSynthesis) speechSynthesis.getVoices = () => [];
  } catch (e) { /* no synth */ }
  const md = navigator.mediaDevices;
  if (md) {
    const realEnum = md.enumerateDevices && md.enumerateDevices.bind(md);
    md.getUserMedia = function (c) {
      window.__vv1.gum++;
      window.__vv1.constraints.push(JSON.stringify(c));
      const track = { kind: 'audio', stop() { window.__vv1.stopped++; } };
      return Promise.resolve({ getTracks: () => [track] });
    };
    md.enumerateDevices = function () { window.__vv1.enumerate++; return realEnum ? realEnum() : Promise.resolve([]); };
  }
  if (navigator.permissions && navigator.permissions.query) {
    const realQuery = navigator.permissions.query.bind(navigator.permissions);
    navigator.permissions.query = function (d) { window.__vv1.perms.push(d && d.name); return realQuery(d); };
  }
})();`;

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
    await cdp.waitFor(`document.getElementById('relay-drawer').classList.contains('show') && /TTS piper/.test(document.getElementById('voice-caps').textContent) && /Browser voices/.test(document.getElementById('voice-caps').textContent)`, 'chip');
    const chip = await cdp.eval(`(() => { const e = document.getElementById('voice-caps'); const cs = getComputedStyle(e);
      const r = e.getBoundingClientRect();
      return { text: e.textContent, fg: cs.color, bg: cs.backgroundColor, border: cs.borderTopColor, w: r.width, h: r.height }; })()`);
    const textOk = chip.text.indexOf('STT none · TTS piper (1 voice)') === 0 && /\nMic /.test(chip.text);
    const paintOk = c.theme === 'field-office'
      ? ((chip.fg === FO.fg || chip.fg === FO.muted) && chip.bg === FO.surface2 && chip.border === FO.line)
      : (chip.bg !== WHITE && chip.fg !== WHITE);
    record('U7', c.name, textOk && paintOk && chip.w > 0 && chip.h > 0 && chip.bg !== WHITE, chip);
    const mt = await cdp.eval(`(() => { const e = document.getElementById('voice-mic-test');
      e.scrollIntoView({ block: 'center' }); const r = e.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2, h: r.height, before: window.__vv1.gum }; })()`);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: mt.x, y: mt.y });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: mt.x, y: mt.y, button: 'left', clickCount: 1, buttons: 1 });
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: mt.x, y: mt.y, button: 'left', clickCount: 1, buttons: 0 });
    await cdp.waitFor(`/test ok/.test(document.getElementById('voice-caps').textContent)`, 'mic test result', 10000);
    const mic = await cdp.eval(`({ spy: window.__vv1, text: document.getElementById('voice-caps').textContent })`);
    record('U8', c.name, mt.before === 0 && mic.spy.gum === 1 && mic.spy.constraints.length === 1
      && mic.spy.constraints[0] === '{"audio":true}' && mic.spy.stopped === 1,
      { before: mt.before, getUserMedia: mic.spy.gum, constraints: mic.spy.constraints, stopped: mic.spy.stopped, buttonH: mt.h });
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
