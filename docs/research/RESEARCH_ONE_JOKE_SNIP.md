# RESEARCH — One joke, full-note snip

Worktree: `/opt/repo/hush/worktrees/one-joke-snip`
Branch: `gb/one-joke-snip`
Base: `main` `c22e4d2bb` (PR #46)

Using the four-minds debug protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP / extensions in this session: analyze, apps, buzz_publish, developer,
  extensionmanager, skills, summon, todo. No documentation or observability
  MCP for Grok CLI, joke policy, or the live hive.
- Consulted: live `hush-relay --open` pid 401684 on `127.0.0.1:10555`,
  `GET /api/status`, `GET /api/session`, `GET /api/events` dumped to
  `/tmp/hush-events-jokes.json`, `hush_agent.c` snip / argv / hygiene,
  `UI_SPEC.md` §13, `RESEARCH_THREAD_THINK_HYGIENE.md`,
  `PLAN_THREAD_THINK_HYGIENE.md`, `grok --help` 1.0.4.
- No M# items. Skip further MCP: the failure is a local transcript cut
  plus a generous robot prompt, both readable in this tree.

**Data:** docs MCP is absent; the four live events are the instrument.
**8/10**.
**Sherlock:** "What is the docs MCP conspicuously silent on?" Whether
`grok -p` plus `--max-turns 2` concatenates two generations. We have
the stored note, not Grok's internal turn log.
**Linus:** We do not read Wikipedia before fixing a newline cut that
hides the second joke. **9/10**.
**Brian Cox:** Causal order is already on the wire: ask, two-joke note,
"another", atom joke again. **9/10**.

## What is broken (quoted)

Operator paste of the 1:1 thread:

```
Thread · Happy
you · 20s ago
@Happy tell me a joke
Happy · 15s ago
Why did the scarecrow win an award? Because he was outstanding in his field! 😄

Why don’t scientists trust atoms? They make up everything!
you · 13s ago
@Happy tell me another
Happy · 9s ago
Why don’t scientists trust atoms? Because they make up everything! 😄
```

Operator: "how did it have two jokes ready when I had only asked for 1?
soemthing is not right."

## Phase 1 — Evidence

**E1** Live process and status (2026-08-19 16:31 local).
`hush-relay --open` pid 401684 etime 03:08; `/proc/401684/exe` →
`/home/chuck/.local/bin/hush-relay`; that binary `cmp`s equal to
`hush-c/hush-relay` at `c22e4d2bb`.
`GET /api/status`:
`{"ok":true,"version":"0.0.1","events":4,"clients":1,"port":10555,"whisper":false,"turn_running":false,"vibe_public":false,"thinking":[]}`
No grok child.

**E2** `GET /api/events` 1540 bytes, 0 tabs, `json.loads` strict ok,
4 events. First Happy note `000000006a861207` created 1787171335
(5 s after the ask), pubkey `540b04857fafe039`, content_repr:

```
'Why did the scarecrow win an award? Because he was outstanding in his field! 😄\n\nWhy don’t scientists trust atoms? They make up everything!'
```

One kind-1 note. Two jokes. `len` 138. First `\n` at index 78.

**E3** Session robot Happy:
`prompt: 'You are always extremely happy and like telling jokes.'`
`provider: 'grok-build'`.

**E4** `hush_agent.c:56` `#define HUSH_AGENT_GROK_TURNS "2"`.
`hush_agent_exec_grok` `:772-773` passes `--max-turns` that value.
Raised in `f44592af8` "Fulfill multi-part Grok asks in one note."
because `--max-turns 1` stored only a preamble on a script ask.
`UI_SPEC.md:354-356` locks turns at 2 for that reason.
`check_agent.sh:107` asserts `HUSH_AGENT_GROK_TURNS "2"`.

**E5** `hush_agent.c:608` `hush_agent_snip_line`:

```
for (i = 0; i < cap && src[i] != '\0' && src[i] != '\n'; i++)
    out[i] = src[i];
```

Cap `HUSH_AGENT_SNIP_MAX = 160`. The first Happy note is 138 chars
with the second joke after `\n\n`. The follow-up transcript therefore
keeps only the scarecrow line.

**E6** `HUSH_AGENT_THREAD_HEAD` (`:57-59`):

```
"Thread so far. Do not repeat a prior joke. "
"Fulfill the last human line in this note.\n"
```

The "do not repeat" rule cannot see a joke the snip deleted.

**E7** Second Happy note `000000006a86120d` created 1787171341
(4 s after "tell me another"), content_repr:

```
'Why don’t scientists trust atoms? Because they make up everything! 😄'
```

Same atom joke as the hidden second line of E2.

**E8** `hush_agent_consider` walks `p` tags once;
`hush_agent_robot_busy` refuses a second job on the same root.
Event count is 4: ask, reply, ask, reply. Not a double fork.

**E9** `hush_agent_finish_job` posts `job->out` as one note.
`hush_agent_read_job` concatenates stdout. Two jokes in E2 are one
Grok stdout blob, not two inserts.

**E10** `grok --help` 1.0.4: `-p, --single` "Single-turn prompt.
Prints the response to stdout and exits." `--max-turns <N>`
"Maximum number of agent turns." `--no-memory` "Disable cross-session
memory for this session." Live argv has no `--no-memory`.
This hive is 3 minutes old with 4 events; memory is not required to
explain E2 or E7.

### Smuggled assumptions

| Id | Claim | Status | Falsifier |
| --- | --- | --- | --- |
| A1 | Happy forked twice on the first ask. | FALSIFIED E8, E1 events=4 | event count / pids |
| A2 | The UI split one note into two bubbles. | FALSIFIED E2 content_repr | dump events |
| A3 | `--max-turns 2` is why two jokes appeared. | UNVERIFIED | grok turn log; do not drop to 1 (E4) |
| A4 | Grok memory caused the repeat. | UNVERIFIED, weak (E10 fresh hive) | `--no-memory` belt only |
| A5 | Happy never saw the first reply. | FALSIFIED E6+E5: it saw the scarecrow line only | snip source |

**Linus** strikes "Happy is broken" as `[not evidence]`. The stored
notes are the evidence.

**Brian Cox timeline**

1. 1787171330 human `@Happy tell me a joke`
2. 1787171335 Happy one note, two jokes (`\n\n`)
3. 1787171337 human `@Happy tell me another`
4. 1787171341 Happy repeats the atom joke (the line snip hid)

No time reversal. The repeat cannot cause the first double joke.

## Phase 2 — Hypotheses

**H1 — `snip_line` hides joke 2; follow-up repeats an unseen joke.**
Explains E5, E6, E7. Does not by itself explain why E2 packed two
jokes. Falsifier: flatten whitespace in the snip; the next "another"
must not repeat a joke already in that flattened line.
Data: mechanism is in this file. **8/10**.

**H2 — Happy persona + "like telling jokes" + fulfill-the-ask hygiene
over-answers the first ask with two jokes.**
Explains E2, E3. Does not explain the exact atom-joke repeat without
H1. Falsifier: add "exactly one joke when the last ask is a joke";
a fresh "tell me a joke" stores one joke.
Sherlock: a theory that also blames `--max-turns 2` explains too
much (A3). Keep turns. **7/10**.

**H3 — Grok cross-session memory (no `--no-memory`) caused the repeat.**
Explains E7 only if a prior session taught the atom joke. Does not
explain E2. Weak against E10 (fresh 3-minute hive). Falsifier:
`--no-memory` on argv; repeat still happens until H1 lands.
Linus: belt, not the lever. **4/10**.

**H4 — Two grok children / double dispatch.**
Falsified by E8. Score **1/10**.

**H5 — UI paints one note as two bubbles.**
Falsified by E2. Score **1/10**.

**Linus:** implied fix for H1 is one loop in `snip_line`. Implied
fix for H2 is one hygiene sentence. Do not build a joke classifier.
**Brian Cox:** H1 is the geodesic for the repeat. H2 is the mass for
the first note. They are not competitors; they are two arrows.

## Phase 3 — Bayes (primary lever trio H1 / H2 / H3)

Priors from this stack: transcript bugs that drop a newline are common
once we chose a one-line snip (E5); persona over-answer is the next
most common LLM failure; cross-session memory on a 3-minute hive is
rare (E10).

- P(H1) = 0.45. P(E\|H1) = 0.90. Unnorm = 0.405
- P(H2) = 0.35. P(E\|H2) = 0.70. Unnorm = 0.245
- P(H3) = 0.20. P(E\|H3) = 0.25. Unnorm = 0.050
- Sum = 0.700

```
P(H1|E) = 0.405 / 0.700 = 0.579
P(H2|E) = 0.245 / 0.700 = 0.350
P(H3|E) = 0.050 / 0.700 = 0.071
```

H1 wins the repeat. H2 is the independent first-note lever. H4/H5 are
dead. Do not treat H1 and H2 as mutually exclusive at the gate.

## Phase 4 — Second debate

**Data:** H1 margin over H2 is 0.579 − 0.350 = 0.229. Numbers justify
fixing the snip. H2 is still above 0.30 and matches E2; ship the
hygiene sentence in the same slice. **7/10** on a scoped dual fix
(hard cap: not yet executed).

**Sherlock:** "Data, you claim H1 is the lever, but why does E2
conveniently ignore that Grok emitted two jokes before any snip ran?"
Value of information is a flattened-snip unit check plus a hygiene
string grep. Not a live Grok turn log. **7/10** — do not wait.

**Linus:** Sherlock, stop philosophizing. H1's diff is smaller than
H2's and H3's. Do H1. Add H2's one sentence. Do not drop
`--max-turns` to 1 (E4 already burned us). `--no-memory` is one argv
slot (21 of 28 used). **7/10**.

**Brian Cox:** Linus, you are not compressing the timeline — the
double joke is at t=5s, the repeat at t=11s, snip only acts on the
second job. Arrow of time supports both arrows, not one. **7/10**.

Agreement: H1 + H2. H3 as optional belt. No distillation.

## Phase 5 — Scope

**Problem.** One `@Happy tell me a joke` stored two jokes in one note
because the robot prompt invites jokes and hygiene never says "one".
`tell me another` repeated the atom joke because `hush_agent_snip_line`
cuts at the first newline, so `Do not repeat a prior joke` never saw
joke 2.

**Work.** Flatten whitespace inside the 160-char snip so a prior
multi-line note is visible. Add one hygiene / rules sentence: if the
last human ask is a joke, reply with exactly one joke. Pass
`--no-memory`. Keep `--max-turns 2`. Teach `check_agent.sh` to dump
`grok -p` and assert the follow-up transcript contains the flattened
second line.

**Out of scope.** Dropping turns to 1. Joke classifiers. Streaming.
Raising `HUSH_EVENT_MAX_CONTENT`. Live hive restart (operator).
GitHub unfork.

## Architecture lock

`hush_agent_snip_line` copies up to `HUSH_AGENT_SNIP_MAX` bytes,
collapsing any run of space / tab / CR / LF to one space, trimming
ends. It does not cut on the first newline.

Hygiene and rules gain:

```
If the last human ask is a joke, reply with exactly one joke.
```

`hush_agent_exec_grok` gains `--no-memory` (no value).
`HUSH_AGENT_ARGV_MAX` stays 28.

Fake grok in `check_agent.sh` appends the `-p` argument to
`$HUSH_CONFIG_DIR/grok-p.log`. After the follow-up mention, that log
must contain the flattened prior reply (`Byte me. go: fmt`).

## Four-score on this lock (pre-fix, cap 7)

- Data **7/10** — would rise with a quoted `check_agent` pass.
- Sherlock **7/10** — would rise if a live "another" after restart
  is a new joke.
- Linus **7/10** — would rise if the diff stays inside `snip_line`,
  two `#define`s, and the test script.
- Brian Cox **7/10** — would rise when the second job's `-p` quotes
  both jokes.
