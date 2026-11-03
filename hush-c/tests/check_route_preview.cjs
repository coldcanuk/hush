// Ops M1 F1/FU1 unit pin (modelled on check_ask_void.cjs): the composer's
// route line. The real paintRoute, paintRoutePick and convoLabel are cut
// out of demo/index.html, never copied here; only their inputs (the
// composer value, the route composerRoute would pick, the log) and a
// recording DOM are stubbed. tests/check_ch0_ui.cjs measures the painted
// line in Chrome (computed display, focus, the 390 gutter, the ellipsis,
// the bare sentence wrapping).
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
// Static: a label wider than the line ends in an ellipsis (Chrome measures it in CH0 RP2L).
if (!/\.route-preview #route-text\.route-label, \.route-preview select \{[^}]*overflow:\s*hidden;[^}]*text-overflow:\s*ellipsis;[^}]*white-space:\s*nowrap;[^}]*min-width:\s*0/.test(html))
  fail('#route-text.route-label and the chooser must carry overflow: hidden, text-overflow: ellipsis, white-space: nowrap and min-width: 0');
// Static (Gauge P2-R8-1): no rule gives nowrap or an ellipsis to #route-text
// unless it is a label; the bare Yes/No sentence and the others wrap.
for (const m of html.matchAll(/([^{}]*#route-text[^{}]*)\{([^}]*)\}/g)) {
  const bare = m[1].split(',').some((sel) => /#route-text(?![\w.:-]*\.route-label)(?!:empty)/.test(sel) && /#route-text/.test(sel));
  if (bare && /white-space:\s*nowrap|text-overflow:\s*ellipsis/.test(m[2]))
    fail('only #route-text.route-label may carry nowrap or an ellipsis: ' + JSON.stringify(m[1].trim()));
}

const grab = (name) => {
  const head = '\n    function ' + name + '(';
  const i = html.indexOf(head);
  if (i < 0 || html.indexOf(head, i + 1) >= 0) fail('demo/index.html must define ' + name + ' exactly once');
  const j = html.indexOf('\n    }\n', i);
  if (j < 0) fail('no end found for ' + name);
  return html.slice(i, j + 7);
};
const src = ['convoLabel', 'paintRoute', 'paintRoutePick'].map(grab).join('');

const classes = () => { const c = new Set(); return { toggle: (n, on) => (on ? c.add(n) : c.delete(n)), add: (n) => c.add(n),
  remove: (n) => c.delete(n), contains: (n) => c.has(n) }; };
const el = (id) => ({ id, hidden: false, textContent: '', title: '', value: '', dataset: {}, options: [], classList: classes(),
  replaceChildren(...k) { this.options = k; }, get children() { return this.options; } });
const els = { msg: el('msg'), 'route-preview': el('route-preview'), 'route-text': el('route-text'), 'route-pick': el('route-pick') };
const document = { createElement: () => ({ value: '', textContent: '', title: '' }) };
const LONG48 = 'Quartermaster of the Northern Supply Depot Fleet'; // 48 characters, the agent-name maxlength
if (LONG48.length !== 48) fail('fixture: LONG48 must be 48 characters');
let nextRoute = null;
const env = {
  $: (id) => els[id],
  document,
  assembleMentionContent: (raw) => ({ content: raw, mentions: [] }),
  mentionRoster: () => [],
  composerRoute: () => nextRoute,
  prettyMentions: (t) => t,
  lastEvents: [
    { id: 'RA', content: '@Ace first task about the supply manifest for the north depot' },
    { id: 'RB', content: '@Bea side task' },
    { id: 'RR', content: '@Research assistant  summarise the quarterly supply manifest for the north depot' },
    { id: 'RL', content: '@' + LONG48 + ' count the crates in bay seven before the convoy leaves' },
    { id: 'RO', content: '@Bea please check the plan Ace wrote' },
    { id: 'RS', content: '@Ace short one' },
    { id: 'RW', content: '@Ace ' + 'x'.repeat(60) },
    { id: 'RX', content: '@Acer is not Ace' },
    // Gauge P2-R8-2 / CoS 01:46 boundary fixtures (quote lengths after the strip).
    { id: 'N39', content: '@Ace the quick brown fox jumps over the lazy' },
    { id: 'N40', content: '@Ace the quick brown fox jumps over the lazy.' },
    { id: 'N41', content: '@Ace the quick brown fox jumps over the lazzzy' },
    { id: 'NH', content: '@Ace ' + 'y'.repeat(41) },
    { id: 'NP', content: '@Ace alpha beta gamma delta epsilon, zetaetatheta more' },
    { id: 'NE', content: '@Ace' },
    { id: 'NC', content: '@ace hi' },
    { id: 'NU', content: '@Ace ' + 'a'.repeat(38) + '\u{1F600}' + 'bbb' },
  ],
  channel: 'route-room',
};
const names = Object.keys(env);
const api = new Function(...names, src + '\nreturn { paintRoute, convoLabel };')(...names.map((k) => env[k]));

const ace = { name: 'Ace', npub: 'npubace', root: 'RA' };
const bea = { name: 'Bea', npub: 'npubbea', root: 'RB' };
const state = () => {
  const p = els['route-pick'];
  const sel = p.options.find((o) => o.value === p.value) || {};
  return { box: els['route-preview'].hidden, text: els['route-text'].textContent, textTitle: els['route-text'].title,
    label: els['route-text'].classList.contains('route-label'),
    pick: p.hidden, opts: p.options.length, shown: sel.textContent || '', optTitle: sel.title || '', pickTitle: p.title };
};

// L. The one label (convoLabel) for both routes.
const quoteOf = (label, name) => {
  const m = label.match(/^to (.*), continuing \u201c(.*)\u201d$/);
  if (!m || m[1] !== name) fail('label shape: ' + JSON.stringify(label));
  return m[2];
};
const lab = (name, root) => api.convoLabel({ name, npub: 'np' + root, root });
const labels = {};
// L1 own mention dropped; L2 another robot's mention kept; L3 boundary: "@Acer" is not "@Ace".
labels.own = lab('Ace', 'RS');
if (quoteOf(labels.own, 'Ace') !== 'short one') fail('L1 the routed robot\'s own leading mention must be dropped: ' + labels.own);
labels.other = lab('Ace', 'RO');
if (quoteOf(labels.other, 'Ace') !== '@Bea please check the plan Ace wrote') fail('L2 a mention of another robot must stay: ' + labels.other);
labels.notOwn = lab('Ace', 'RX');
if (quoteOf(labels.notOwn, 'Ace') !== '@Acer is not Ace') fail('L3 only a whole "@<name>" mention is dropped: ' + labels.notOwn);
// L4 long notes: cut at a word break within 40 characters, ending in "…".
const wordCut = (key, name, root, rest) => {
  const q = quoteOf(labels[key] = lab(name, root), name);
  if (!q.endsWith('\u2026') || q.length > 40) fail('L4 ' + key + ': a long quote must be at most 40 characters ending in "…": ' + JSON.stringify(q));
  const body = q.slice(0, -1);
  if (!rest.startsWith(body) || (rest[body.length] !== ' ' && rest[body.length] !== undefined))
    fail('L4 ' + key + ': the quote must end on a word break of the note: ' + JSON.stringify(q));
  if (body.length < 20) fail('L4 ' + key + ': the cut must keep the words that fit: ' + JSON.stringify(q));
};
wordCut('ace', 'Ace', 'RA', 'first task about the supply manifest for the north depot');
wordCut('research', 'Research assistant', 'RR', 'summarise the quarterly supply manifest for the north depot');
wordCut('long48', LONG48, 'RL', 'count the crates in bay seven before the convoy leaves');
// L5 one word longer than the cap is cut at 39 + "…".
labels.word = lab('Ace', 'RW');
if (quoteOf(labels.word, 'Ace') !== 'x'.repeat(39) + '\u2026') fail('L5 a single long word is cut at 39 characters: ' + labels.word);
// L6 the 40 boundary: 39 and 40 characters whole, 41 cut at a word break,
// 41 with no space cut hard at 39 + "…".
const exact = (key, root, want, rule) => {
  const q = quoteOf(labels[key] = lab('Ace', root), 'Ace');
  if (q !== want) fail(rule + ': ' + JSON.stringify({ got: q, want }));
};
exact('n39', 'N39', 'the quick brown fox jumps over the lazy', 'L6a a 39-character quote is shown whole');
exact('n40', 'N40', 'the quick brown fox jumps over the lazy.', 'L6b a 40-character quote is shown whole');
exact('n41', 'N41', 'the quick brown fox jumps over the\u2026', 'L6c a 41-character quote is cut at the last word break within 39');
exact('nHard', 'NH', 'y'.repeat(39) + '\u2026', 'L6d a 41-character word is cut hard at 39');
// L7 trailing punctuation of the kept words is dropped before "…".
exact('punct', 'NP', 'alpha beta gamma delta epsilon\u2026', 'L7 trailing punctuation is dropped at the cut');
// L8 a note that is only the robot's own mention is quoted as written.
exact('empty', 'NE', '@Ace', 'L8 an empty quote falls back to the note as written');
// L9 the own-mention match ignores case ("@ace" for Ace).
exact('caseless', 'NC', 'hi', 'L9 the own mention is dropped whatever its case');
// L10 characters are code points: an emoji at 38/39 is kept whole, never split.
exact('emoji', 'NU', 'a'.repeat(38) + '\u{1F600}\u2026', 'L10 the cut counts code points and never splits an emoji');

// 1. A draft with two robots: the chooser shows, the text asks for a choice.
els.msg.value = 'plain two';
nextRoute = { kind: 'choose', list: [ace, bea] };
api.paintRoute();
const choose = state();
if (choose.box || choose.text !== 'Choose who this goes to:' || choose.label || choose.pick || choose.opts !== 4)
  fail('a two-robot draft must show "Choose who this goes to:" and the chooser: ' + JSON.stringify(choose));

// 2. Picked Ace: the chooser carries the label, the text does not repeat it,
// and option and select both carry it as their title.
nextRoute = { kind: 'continue', convo: ace, list: [ace, bea] };
api.paintRoute();
const picked = state();
if (picked.box || picked.pick || picked.text !== '' || picked.shown !== api.convoLabel(ace))
  fail('with the chooser showing Ace, the option is the one label and the text is empty: ' + JSON.stringify(picked));
if (picked.optTitle !== picked.shown || picked.pickTitle !== picked.shown)
  fail('the picked option and the chooser must carry the whole label as title: ' + JSON.stringify(picked));

// 3. An @mention route with no chooser: the text is the same label.
nextRoute = { kind: 'continue', convo: ace };
api.paintRoute();
const named = state();
if (named.box || !named.pick || named.text !== api.convoLabel(ace) || named.text !== picked.shown || named.textTitle !== named.text || !named.label)
  fail('the @mention route must show the same label as the chooser option, with it as title: ' + JSON.stringify({ named, option: picked.shown }));

// 3b. Gauge P2-R8-1: the bare Yes/No sentence and a new conversation are
// not labels: no route-label class, so they wrap in full (CH0 measures it).
els.msg.value = 'Yes';
nextRoute = { kind: 'bare' };
api.paintRoute();
const bare = state();
if (bare.box || bare.label || bare.text !== 'to #route-room as a new note. A bare Yes or No answers an ask only from its stamps.')
  fail('the bare Yes/No sentence must show in full and carry no route-label class: ' + JSON.stringify(bare));
els.msg.value = '@Ace @Bea both';
nextRoute = { kind: 'new' };
api.paintRoute();
const fresh = state();
if (fresh.box || fresh.label || fresh.text !== 'to #route-room, new conversation')
  fail('a new-conversation route carries no route-label class: ' + JSON.stringify(fresh));

// 4. The composer empties (after a send, or in another channel): nothing is
// left: the line hides, its text is reset, the chooser hides with 0 options.
nextRoute = { kind: 'choose', list: [ace, bea] };
api.paintRoute();
els.msg.value = '';
nextRoute = { kind: 'new' };
api.paintRoute();
const idle = state();
if (!idle.box || idle.text !== '' || idle.textTitle !== '' || idle.label || !idle.pick || idle.opts !== 0 || idle.pickTitle !== '')
  fail('an empty composer must hide the line, reset its text and title and empty the chooser: ' + JSON.stringify(idle));

console.log('route preview check passed: ' + JSON.stringify({ labels, choose, picked, named, bare, fresh, idle }));
