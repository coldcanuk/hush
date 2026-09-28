// UI-M12d compact-button check: opens each menu/drawer surface at 1440x900
// and 375x812 and, for every visible in-scope control, fails when the
// visual height is above its tier cap or the effective hit height (probed
// with document.elementFromPoint at centre y +/- (floor/2 - 1)) is below
// the floor (24px desktop, 44px at 375px). Prints one line per control.
// Not part of make test (needs Playwright + a Chromium executable):
//   HUSH_PLAYWRIGHT_MODULE=... HUSH_BROWSER_EXECUTABLE=... node hush-c/tests/check_ui_m12d_buttons.cjs
// Screenshots of each surface go to HUSH_TEST_UI_ARTIFACTS (or a temp dir).
const {spawn} = require('node:child_process');
const {once} = require('node:events');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const readline = require('node:readline');
const {chromium} = require(process.env.HUSH_PLAYWRIGHT_MODULE || 'playwright');
const artifacts = process.env.HUSH_TEST_UI_ARTIFACTS || fs.mkdtempSync(path.join(os.tmpdir(), 'hush-m12d-'));
fs.mkdirSync(artifacts, {recursive: true});

// First matching rule wins: [selector, visual cap px, tier label,
// optional accepted exclusive touch hit px]. The 4th value is for stacked
// rows whose 44px hits deliberately share up to 8px with a neighbour (the
// BOARDS individuals list, 36px pitch; the later row wins the shared band).
// The row passes when its exclusive band is >= that value; 35 is the 36px
// pitch minus 1px for the whole-pixel scan, and the output prints it as-is.
const RULES = [
  ['.leave-actions .btn', 999, 'pinned-44'],
  ['#nav-toggle', 999, 'pinned-44'],
  ['#canvas-k .row button', 28, 'sm'],
  ['#agent-drawer .actions .btn', 32, 'md'],
  ['.btn', 32, 'md'],
  ['#payne-provider-pills .pill button', 28, 'sm'],
  ['.pill button', 24, 'xs'],
  ['.icon-plus, .icon-minus', 28, 'sm'],
  ['.inv-btn:not(.qb-slot)', 28, 'sm'],
  ['.drawer-x', 28, 'sm'],
  ['.menu button', 28, 'sm'],
  ['.mention-box button', 28, 'sm'],
  ['.chan-options', 28, 'sm'],
  ['.chan-voice', 24, 'xs'],
  ['.chan', 28, 'sm'],
  ['.vis label', 28, 'sm'],
  ['.help button', 24, 'xs'],
  ['.mini', 28, 'sm'],
  ['.prov-row .cfg', 28, 'sm'],
  ['.prov-row', 56, 'row'],
  ['#agent-providers label', 48, 'tile'],
  ['.provider-config', 32, 'md'],
  ['.agent-pic-sheet', 24, 'xs'],
  ['.skill-life', 24, 'xs'],
  ['.inv-expand-link', 24, 'xs'],
  ['summary', 24, 'xs'],
  ['.tile-mute', 24, 'xs'],
  ['.iconbtn', 32, 'md'],
  ['.skill-facet', 32, 'md'],
  ['.think-stop', 32, 'md'],
  ['button.fo-person', 28, 'sm', 35],
  ['.switch', 24, 'xs'],
];

async function measure(page, surface, root, floor, out) {
  // Polling can re-render a list (channel rows, roster) mid-measure. A
  // detached node has no box, so the whole surface is re-measured (up to
  // three tries) rather than silently skipping it.
  let rows = [];
  for (let attempt = 0; attempt < 3; attempt++) {
    rows = await measureOnce(page, root, floor);
    if (!rows.some((row) => row.detached)) break;
    await page.waitForTimeout(400);
  }
  return report(rows, surface, floor, out);
}

async function measureOnce(page, root, floor) {
  return page.evaluate(async ({root, rules, floor}) => {
    const scope = document.querySelector(root);
    if (!scope) return [{error: 'no root ' + root}];
    const seen = new Set();
    const res = [];
    const all = scope.querySelectorAll(rules.map((r) => r[0]).join(','));
    for (const el of all) {
      if (seen.has(el)) continue;
      seen.add(el);
      const rule = rules.find((r) => el.matches(r[0]));
      if (!el.isConnected) { res.push({detached: true, sel: rule[0]}); continue; }
      const r0 = el.getBoundingClientRect();
      const cs = getComputedStyle(el);
      if (r0.width < 1 || r0.height < 1 || cs.visibility === 'hidden') continue;
      el.scrollIntoView({block: 'center', inline: 'nearest'});
      await new Promise((ok) => requestAnimationFrame(() => ok()));
      // Polling can re-render a list (e.g. channel rows) mid-measure; a
      // detached node has no box, so report it instead of mis-measuring.
      if (!el.isConnected) { res.push({detached: true, sel: rule[0]}); continue; }
      const r = el.getBoundingClientRect();
      const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
      const d = floor / 2 - 1;
      let hitBy = '';
      const probe = (y) => {
        const hit = document.elementFromPoint(cx, y);
        const ok = !!hit && (hit === el || el.contains(hit));
        if (!ok && !hitBy) hitBy = (y < cy ? 'above:' : 'below:') + (hit ? (hit.id ? '#' + hit.id : hit.tagName.toLowerCase() + (typeof hit.className === 'string' && hit.className ? '.' + hit.className.trim().split(/\s+/).join('.') : '')) : 'none');
        return ok;
      };
      let hitOk = probe(cy - d) && probe(cy + d) && probe(cy);
      // Accepted overlap: scan the exclusive hit band (1px steps) instead.
      let excl = 0;
      if (!hitOk && floor > 24 && rule[3]) {
        const own = (y) => { const h = document.elementFromPoint(cx, y); return !!h && (h === el || el.contains(h)); };
        let up = 0, down = 0;
        while (up < 40 && own(cy - up - 1)) up++;
        while (down < 40 && own(cy + down + 1)) down++;
        excl = up + down + 1;
        if (own(cy) && excl >= rule[3]) { hitOk = true; hitBy = ''; }
      }
      // A rotated control's bounding box is inflated by the rotation; its
      // visual (layout) height is offsetHeight.
      const rotated = cs.transform !== 'none';
      const vh = rotated ? el.offsetHeight : r.height;
      const name = el.id ? '#' + el.id : (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).join('.') : el.tagName.toLowerCase());
      const label = (el.getAttribute('aria-label') || el.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 18);
      res.push({sel: rule[0], tier: rule[2], cap: rule[1], name, label, h: Math.round(vh * 10) / 10, w: Math.round(r.width * 10) / 10, hitOk, hitBy, rotated, excl, accept: rule[3] || 0});
    }
    return res;
  }, {root, rules: RULES, floor});
}

function report(rows, surface, floor, out) {
  let bad = 0;
  for (const row of rows) {
    if (row.error) { out.push(`${surface} ERROR ${row.error}`); bad++; continue; }
    if (row.detached) { out.push(`${surface} ${row.sel} FAIL (re-rendered during all 3 measure tries)`); bad++; continue; }
    const capOk = row.h <= row.cap + 0.5;
    const verdict = capOk && row.hitOk ? 'ok' : (!capOk ? 'FAIL-visual>' + row.cap : '') + (!row.hitOk ? ' FAIL-hit<' + floor : '');
    if (verdict !== 'ok') bad++;
    out.push(`${surface} {${row.sel}} ${row.name}${row.label ? ' "' + row.label + '"' : ''} [${row.tier}] h=${row.h}${row.rotated ? '(rotated)' : ''} w=${row.w} ${row.excl ? 'hit-exclusive=' + row.excl + (row.hitOk ? '>=' : '<') + row.accept + ' (accepted overlap)' : 'hit' + (row.hitOk ? '>=' : '<') + floor} ${verdict}${row.hitBy ? ' (' + row.hitBy + ')' : ''}`);
  }
  return {count: rows.length, bad};
}

(async () => {
  const fixture = spawn('python3', [path.join(__dirname, 'check_collaboration.py'), '--serve-ui'], {stdio: ['pipe', 'pipe', 'inherit']});
  const exited = once(fixture, 'exit');
  const lines = readline.createInterface({input: fixture.stdout});
  let browser;
  const out = [];
  let total = 0, failed = 0;
  try {
    const first = await Promise.race([once(lines, 'line'), exited.then(() => { throw new Error('UI fixture exited before startup'); })]);
    const base = JSON.parse(first[0]).url;
    browser = await chromium.launch({executablePath: process.env.HUSH_BROWSER_EXECUTABLE, headless: true, args: ['--no-sandbox']});
    for (const [w, h] of [[1440, 900], [375, 812]]) {
      const floor = w <= 640 ? 44 : 24;
      const ctx = await browser.newContext({viewport: {width: w, height: h}});
      const page = await ctx.newPage();
      const errors = [];
      page.on('pageerror', (e) => errors.push(e.message));
      page.setDefaultTimeout(8000);
      await page.goto(base);
      await page.waitForSelector('#hive.show');
      await page.waitForTimeout(300);
      const click = (sel) => page.evaluate((s) => { const el = document.querySelector(s); if (!el) throw new Error('missing ' + s); el.click(); }, sel);
      const show = (sel) => page.evaluate((s) => document.querySelector(s).classList.add('show'), sel);
      const hide = (sel) => page.evaluate((s) => document.querySelector(s).classList.remove('show'), sel);
      const shot = (name) => page.screenshot({path: path.join(artifacts, `${name}-${w}.png`)});
      const run = async (name, root, open, close, extra) => {
        try {
          await open();
          await page.waitForTimeout(250);
          await shot(name);
          const r = await measure(page, `${w} ${name}`, root, floor, out);
          total += r.count; failed += r.bad;
          if (extra) await extra();
        } catch (e) {
          // A surface that cannot be opened is a coverage loss, not a pass.
          out.push(`${w} ${name} FAIL (surface not opened: ${e.message.split('\n')[0]})`);
          failed++;
        }
        try { if (close) await close(); } catch (e) { /* surface already gone */ }
        await page.keyboard.press('Escape').catch(() => {});
        await page.waitForTimeout(100);
      };
      await run('kit-menu', '#kit-menu', () => click('#rail-toggle'), () => click('#rail-toggle'));
      await run('boards-drawer', '#fo-drawer', () => click('#nav-toggle'), () => click('#nav-toggle'));
      await run('chan-menu', '#chan-menu', () => click('.chan-options'), () => page.evaluate(() => document.querySelector('#chan-menu').classList.remove('show')));
      await run('inv-menu', '#inv-menu', () => page.evaluate(() => {
        // The inventory can sit in the closed BOARDS drawer (off-screen), so
        // open the menu at a fixed on-screen point instead of the item's.
        const t = document.querySelector('.inv-item');
        t.dispatchEvent(new MouseEvent('contextmenu', {bubbles: true, cancelable: true, clientX: 120, clientY: 200}));
      }), () => page.evaluate(() => document.querySelector('#inv-menu').classList.remove('show')));
      await run('mention-popover', '#mention-box', async () => { await page.locator('#msg').fill(''); await page.locator('#msg').type('@'); }, () => page.locator('#msg').fill(''));
      await run('settings', '#settings', () => click('#settings-btn'), () => hide('#settings'));
      await run('stage', '#stage', () => show('#stage'), () => hide('#stage'));
      await run('thread-pane', '#thread-pane', () => show('#thread-pane'), () => hide('#thread-pane'));
      await run('code-canvas', '#code-canvas', () => page.evaluate(() => { document.querySelector('#code-canvas').classList.add('show'); const k = document.querySelector('#canvas-k'); k.hidden = false; k.classList.add('show'); }), () => page.evaluate(() => { document.querySelector('#code-canvas').classList.remove('show'); const k = document.querySelector('#canvas-k'); k.classList.remove('show'); }));
      await run('new-chan', '#new-chan-drawer', () => click('#add-chan'), () => hide('#new-chan-drawer'));
      // #stats sits inside the BOARDS drawer (hidden at 375), so phones open the relay drawer directly.
      await run('relay-drawer', '#relay-drawer', () => (w > 640 ? click('#stats') : show('#relay-drawer')), () => hide('#relay-drawer'));
      await run('leave-dialog', '#hive-leave', () => click('#hive-close'), () => hide('#hive-leave'));
      await run('profile', '#profile', () => click('#profile-btn'), () => hide('#profile'));
      await run('robot-editor', '#agent-drawer', async () => {
        await click('#inv-raise');
        await page.waitForTimeout(200);
        await page.evaluate(() => {
          const boxes = [...document.querySelectorAll('.agent-provider-cb')];
          boxes.slice(0, 2).forEach((b) => { if (!b.checked) b.click(); });
          const t = document.querySelector('#agent-provider-pills'); if (t) t.scrollIntoView({block: 'start'});
        });
      }, null, async () => {
        await page.evaluate(() => { const f = document.querySelector('#skill-forge-open'); if (f) f.scrollIntoView({block: 'start'}); });
        await page.waitForTimeout(150);
        await shot('robot-editor-skills');
      });
      await run('avatar-drawer', '#avatar-drawer', () => click('#agent-pic-enlarge'), () => hide('#avatar-drawer'));
      await run('forge', '#forge-drawer', () => click('#skill-forge-open'), () => hide('#forge-drawer'));
      await hide('#agent-drawer');
      await run('skill-sheet', '#skill-drawer', () => show('#skill-drawer'), () => hide('#skill-drawer'));
      await run('providers-hub', '#providers-hub', () => click('#providers-btn'), null);
      await run('provider-drawer', '#provider-drawer', () => click('#providers-list .prov-row .cfg'), () => hide('#provider-drawer'));
      await hide('#providers-hub');
      await run('invite-human', '#member-drawer', () => click('#invite-human'), () => hide('#member-drawer'));
      await run('manage-channel', '#manage-chan', async () => {
        await click('.chan-options');
        await click('#chan-menu button[data-act="manage"]');
        await page.waitForTimeout(200);
        await page.evaluate(() => { const d = document.querySelector('#manage-chan details'); if (d) d.open = true; });
      }, () => hide('#manage-chan'));
      await run('dev-log', '#dev-log-drawer', () => show('#dev-log-drawer'), () => hide('#dev-log-drawer'));
      await run('inv-expand', '#inv-expand-drawer', () => click('#inv-expand'), () => hide('#inv-expand-drawer'));
      await run('seed', '#seed-drawer', () => click('#inv-seed'), () => hide('#seed-drawer'));
      out.push(`${w} gate SKIP (fixture session is already ready; onboarding gate not reachable)`);
      if (errors.length) { out.push(`${w} PAGE ERRORS: ${errors.join(' | ')}`); failed++; }
      await ctx.close();
    }
  } finally {
    if (browser) await browser.close();
    fixture.stdin.end();
    await exited.catch(() => {});
  }
  console.log(out.join('\n'));
  console.log(`m12d buttons: ${total} controls measured, ${failed} failing; screenshots in ${artifacts}`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
