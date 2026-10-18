// theme_styles_dump.cjs: computed-style + token dump for every element of the
// static page (scripts off), 8 themes x {1440,390}. Not part of make test.
// usage: node tests/theme_styles_dump.cjs <index.html> <out.json>; compare two
// dumps with tests/theme_styles_diff.py. Deletes its temp Chrome profile on exit.
'use strict';
const { spawn } = require('node:child_process'); const fs = require('node:fs'); const os = require('node:os'); const path = require('node:path');
const prof = fs.mkdtempSync(path.join(os.tmpdir(), 'kbl-sty-')); let chrome = null;
function cleanup() { try { if (chrome) process.kill(-chrome.pid, 'SIGKILL'); } catch (e) {} try { fs.rmSync(prof, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 }); } catch (e) {} }
process.on('exit', cleanup); for (const s of ['SIGINT', 'SIGTERM', 'SIGHUP']) process.on(s, () => process.exit(1));
(async () => {
  const [src, out] = process.argv.slice(2); const html = path.join(prof, 'index.html'); fs.copyFileSync(src, html);
  chrome = spawn('google-chrome', ['--headless=new', '--no-sandbox', '--disable-gpu', '--remote-debugging-port=0', '--user-data-dir=' + prof, '--blink-settings=scriptEnabled=false', 'about:blank'], { detached: true, stdio: ['ignore', 'ignore', 'pipe'] });
  const port = await new Promise((res, rej) => { let b = ''; chrome.stderr.on('data', (d) => { b += d; const m = b.match(/DevTools listening on ws:\/\/[^:]+:(\d+)/); if (m) res(m[1]); }); setTimeout(() => rej(new Error('no devtools')), 20000); });
  const page = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find((t) => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl); await new Promise((r) => ws.addEventListener('open', r));
  let id = 0; const pend = new Map(); ws.addEventListener('message', (e) => { const m = JSON.parse(e.data); if (pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } });
  const send = (method, params) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Page.enable', {}); await send('Page.navigate', { url: 'file://' + html }); await new Promise((r) => setTimeout(r, 1500));
  const result = {};
  for (const [w, h] of [[1440, 900], [390, 844]]) {
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false }); await new Promise((r) => setTimeout(r, 300));
    const expr = `(() => { const themes = ['dark','light','color-blind','dracula','desert','monochrome','christmas','field-office']; const h = document.documentElement; const out = {};
      const st = document.createElement('style'); st.textContent = '*{transition:none!important;animation:none!important}'; document.head.appendChild(st);
      const els = [document.documentElement, ...document.querySelectorAll('body, body *')];
      const pathOf = (el) => { const p = []; for (let n = el; n && n.nodeType === 1; n = n.parentElement) { let i = 1; for (let s = n.previousElementSibling; s; s = s.previousElementSibling) i++; p.unshift(n.tagName.toLowerCase() + (n.id ? '#' + n.id : '') + ':' + i); } return p.join('>'); };
      for (const t of themes) { h.setAttribute('data-theme', t); const o = {};
        for (const el of els) { const cs = getComputedStyle(el); const v = {}; for (const p of cs) v[p] = cs.getPropertyValue(p); o[pathOf(el)] = v; }
        out[t] = o; }
      return JSON.stringify(out); })()`;
    const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true }); result[w] = JSON.parse(r.result.result.value);
  }
  fs.writeFileSync(out, JSON.stringify(result)); process.exit(0);
})().catch((e) => { console.error(e); process.exit(1); });
