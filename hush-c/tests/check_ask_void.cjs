// Gauge M1-F19 unit pin (modelled on Gauge's r3 void_scope_probe.cjs).
// The serial hold that gives one robot turn per conversation applies only
// to owner notes (agent_dispatch.c:548-550, :592-597). A guest note naming
// two robots (hush_intel.c:185-189, agent_dispatch.c:1370-1371) or a
// parallel team wave (agent_dispatch.c:1657-1668) can leave two asks
// waiting in one conversation, and the relay voids all of them by root
// (agent_approve.c:104-118, hush_agent_ask_void :165-176). The CH0 leg
// cannot reach that state through owner notes, so this pin runs the real
// readAskState and answer bar (loopAnswerBar) from demo/index.html on it.
// The functions are cut out of the page source, never copied here, so a
// change in the page is tested.
'use strict';
const fs = require('fs');

const fail = (msg) => { console.error('ask void check failed: ' + msg); process.exit(1); };
const html = fs.readFileSync(process.argv[2] || 'demo/index.html', 'utf8');

// One top-level page function, from "    function NAME(" to its closing
// "\n    }\n" at the same indent. Each must appear exactly once.
const grab = (name) => {
  const head = '\n    function ' + name + '(';
  const i = html.indexOf(head);
  if (i < 0 || html.indexOf(head, i + 1) >= 0) fail('demo/index.html must define ' + name + ' exactly once');
  const j = html.indexOf('\n    }\n', i);
  if (j < 0) fail('no end found for ' + name);
  return html.slice(i, j + 7);
};
const names = ['sameKey', 'rosterHit', 'isMineNote', 'isYesNoAsk', 'yesNoWord', 'readAskState', 'releaseAsk'];
const src = names.map(grab).join('');
// The page globals these functions read: the owner session and the roster.
const session = { pubkey: 'owner'.repeat(8), npub: '' };
const roster = [{ kind: 'robot', name: 'Ace', pubkey: 'ace0'.repeat(16) },
  { kind: 'robot', name: 'Bea', pubkey: 'bea0'.repeat(16) }];
const mentionRoster = () => roster;
const readAskState = new Function('session', 'mentionRoster', src + '\nreturn readAskState;')(session, mentionRoster);

const ask = (id, root, who) => ({ id, reply_to: root, pubkey: 'relay0'.repeat(10), ask: true,
  content: 'Approval needed: ' + who + ' wants to take a turn. Reply Yes or No.' });
const notes = [
  { id: 'X', pubkey: session.pubkey, content: '@Ace @Bea task' },
  { id: 'Y', pubkey: session.pubkey, content: '@Bea other task' },
  ask('A1', 'X', 'Ace'), ask('B1', 'Y', 'Bea'), ask('A2', 'X', 'Bea'),
];
const before = readAskState(notes);
if (before.voided.A1 || before.voided.A2 || before.voided.B1 || before.oldest.X !== 'A1' || before.oldest.Y !== 'B1')
  fail('setup: two asks must wait in X and one in Y before the plain note: ' + JSON.stringify(before));

const after = readAskState(notes.concat([{ id: 'N', pubkey: session.pubkey, reply_to: 'X', content: 'plain follow-up' }]));
const got = { voided: Object.keys(after.voided).sort(), oldest: after.oldest };
if (!after.voided.A1 || !after.voided.A2 || after.oldest.X)
  fail('a plain owner note in X must close every ask waiting in X (A1 and A2): ' + JSON.stringify(got));
if (after.voided.B1 || after.oldest.Y !== 'B1')
  fail('a plain owner note in X must leave the ask waiting in Y open: ' + JSON.stringify(got));
// The answer bar: the real sentStamp and loopAnswerBar cut from the page,
// run against a DOM stub that only records what they build. They read the
// page globals askState (the readAskState result) and answering (asks
// posted from this window, none here).
const stubEl = (tag) => ({ tag, className: '', textContent: '', type: '', title: '', disabled: false, children: [],
  appendChild(c) { this.children.push(c); return c; }, setAttribute() {}, addEventListener() {} });
const document = { createElement: stubEl };
const loopAnswerBar = new Function('document', 'askState', 'answering',
  grab('sentStamp') + grab('loopAnswerBar') + '\nreturn loopAnswerBar;')(document, after, {});
const bar = (id) => {
  const b = loopAnswerBar(notes.find((e) => e.id === id));
  return { live: b.children.filter((c) => c.tag === 'button' && !c.disabled).map((c) => c.textContent),
    stamps: b.children.filter((c) => c.tag === 'button').length,
    sent: b.children.filter((c) => c.tag !== 'button').map((c) => c.textContent).join('|') };
};
const bars = { A1: bar('A1'), A2: bar('A2'), B1: bar('B1') };
const CLOSED = 'Closed by a newer note';
['A1', 'A2'].forEach((id) => {
  if (bars[id].sent !== CLOSED || bars[id].stamps !== 0)
    fail('the bar of ' + id + ' (voided in X) must show "' + CLOSED + '" and no Yes/No stamps: ' + JSON.stringify(bars));
});
if (bars.B1.sent || bars.B1.live.join(',') !== 'Yes,No')
  fail('the bar of B1 (waiting in Y) must show live Yes and No: ' + JSON.stringify(bars));
console.log('ask void check passed: ' + JSON.stringify(Object.assign(got, { bars })));
