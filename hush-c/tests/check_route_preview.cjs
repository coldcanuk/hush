// Ops M1 F1/FU1 unit pin (modelled on check_ask_void.cjs): the composer's
// route line. The real paintRoute, paintRoutePick, pickLabel and convoLabel
// are cut out of demo/index.html, never copied here; only their inputs
// (the composer value, the route composerRoute would pick, the log) and a
// recording DOM are stubbed. tests/check_ch0_ui.cjs measures the painted
// line in Chrome (computed display, focus, the 390 gutter).
'use strict';
const fs = require('fs');

const fail = (msg) => { console.error('route preview check failed: ' + msg); process.exit(1); };
const html = fs.readFileSync(process.argv[2] || 'demo/index.html', 'utf8');

// Static: the chooser starts hidden, and a [hidden] rule beats the flex rule.
const pickTag = (html.match(/<select id="route-pick"[^>]*>/g) || []);
if (pickTag.length !== 1 || !/\shidden(\s|>)/.test(pickTag[0]))
  fail('#route-pick must start hidden in the markup: ' + JSON.stringify(pickTag));
if (!/\.route-preview\[hidden\][^{]*\{[^}]*display:\s*none/.test(html))
  fail('a .route-preview[hidden] rule must set display: none');

const grab = (name) => {
  const head = '\n    function ' + name + '(';
  const i = html.indexOf(head);
  if (i < 0 || html.indexOf(head, i + 1) >= 0) fail('demo/index.html must define ' + name + ' exactly once');
  const j = html.indexOf('\n    }\n', i);
  if (j < 0) fail('no end found for ' + name);
  return html.slice(i, j + 7);
};
const src = ['convoLabel', 'pickLabel', 'paintRoute', 'paintRoutePick'].map(grab).join('');

const el = (id) => ({ id, hidden: false, textContent: '', value: '', dataset: {}, options: [],
  replaceChildren(...k) { this.options = k; }, get children() { return this.options; } });
const els = { msg: el('msg'), 'route-preview': el('route-preview'), 'route-text': el('route-text'), 'route-pick': el('route-pick') };
const document = { createElement: () => ({ value: '', textContent: '' }) };
let nextRoute = null;
const env = {
  $: (id) => els[id],
  document,
  assembleMentionContent: (raw) => ({ content: raw, mentions: [] }),
  mentionRoster: () => [],
  composerRoute: () => nextRoute,
  prettyMentions: (t) => t,
  lastEvents: [{ id: 'RA', content: '@Ace first task about the supply manifest for the north depot' },
    { id: 'RB', content: '@Bea side task' }],
  channel: 'route-room',
};
const names = Object.keys(env);
const api = new Function(...names, src + '\nreturn { paintRoute, pickLabel, convoLabel };')(...names.map((k) => env[k]));

const ace = { name: 'Ace', npub: 'npubace', root: 'RA' };
const bea = { name: 'Bea', npub: 'npubbea', root: 'RB' };
const state = () => ({ box: els['route-preview'].hidden, text: els['route-text'].textContent,
  pick: els['route-pick'].hidden, opts: els['route-pick'].options.length,
  shown: (els['route-pick'].options.find((o) => o.value === els['route-pick'].value) || {}).textContent || '' });

// 1. A draft with two robots: the chooser shows, the text asks for a choice.
els.msg.value = 'plain two';
nextRoute = { kind: 'choose', list: [ace, bea] };
api.paintRoute();
const choose = state();
if (choose.box || choose.text !== 'Choose who this goes to:' || choose.pick || choose.opts !== 4)
  fail('a two-robot draft must show "Choose who this goes to:" and the chooser: ' + JSON.stringify(choose));

// 2. Picked Ace: the chooser carries the label, the text does not repeat it.
nextRoute = { kind: 'continue', convo: ace, list: [ace, bea] };
api.paintRoute();
const picked = state();
if (picked.box || picked.pick || picked.text !== '' || !/^to Ace, continuing /.test(picked.shown))
  fail('with the chooser showing Ace, the route text must not repeat the label: ' + JSON.stringify(picked));
if (picked.shown.length > api.convoLabel(ace).length || picked.shown.length > 50)
  fail('a chooser option must use the short label: ' + JSON.stringify(picked.shown));

// 3. An @mention route with no chooser: the text carries the full label.
nextRoute = { kind: 'continue', convo: ace };
api.paintRoute();
const named = state();
if (named.box || !named.pick || named.text !== api.convoLabel(ace))
  fail('a route with no chooser must say where Send posts: ' + JSON.stringify(named));

// 4. The composer empties (after a send, or in another channel): nothing is
// left: the line hides, its text is reset, the chooser hides with 0 options.
nextRoute = { kind: 'choose', list: [ace, bea] };
api.paintRoute();
els.msg.value = '';
nextRoute = { kind: 'new' };
api.paintRoute();
const idle = state();
if (!idle.box || idle.text !== '' || !idle.pick || idle.opts !== 0)
  fail('an empty composer must hide the line, reset its text and empty the chooser: ' + JSON.stringify(idle));

console.log('route preview check passed: ' + JSON.stringify({ choose, picked, named, idle }));
