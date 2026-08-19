# RESEARCH — Silent 1:1 follow-up + missing think chip

Worktree: `/opt/repo/hush/worktrees/thread-1to1-follow`
Branch: `gb/thread-1to1-follow`
Base: `main` `0335fa2d2` (PR #43)

Using the four-minds debug protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP / extensions surveyed: analyze, apps, buzz_publish, developer,
  extensionmanager, skills, summon, todo. Extra catalog (summarize,
  tom, code_execution, chatrecall, computercontroller, autovisualiser,
  memory, tutorial) not relevant to a local hive UI + C relay.
- No documentation or observability MCP for Hush or Grok Build.
- Consulted: live `GET /api/events` + `/api/status` + `/api/session`
  on `hush-relay --open` pid 368976 port 10555; `hush_intel.c`,
  `hush_agent.c`, `hush_http.c`, `demo/index.html`, `UI_SPEC.md` §13
  and §20, `PLAN_CONV_INTEL_POLICY.md` M4.2, `test_intel.c`,
  `check_launch.sh`, `check_agent.sh`.
- M1: no MCP produced a fact. Evidence below is file + live HTTP only.

Data: the hive is up; three notes exist; thinking is empty. **8/10**.
Sherlock: "Data, why not tail grok logs?" Because stdout of
`hush-relay` is `/dev/null` and thinking is already `[]`. A missing
job cannot hide in a log we cannot read.
Linus: we do not read Wikipedia before fixing a missing `p` tag.
Brian Cox: first joke arrived, then "tell me another" stored, then
silence. Time order forbids "Grok hung on the first job."

## What is broken (quoted)

Operator paste:

```
Thread · Happy
1:1 with Happy
you · 1m ago
@Happy tell me a joke
Happy · 55s ago
Why did the scarecrow win an award? Because he was outstanding in his field!
you · 49s ago
tell me another
```

1. "there is nothing here to give me visual feedback that the AI is
   process/thinking"
2. "I never got my next reply."

Tried / believed (tagged):
- First turn works. `assumption` until live events quoted; then
  `verified`.
- Second turn is silent. `verified` by live store.
- Thinking UI is absent. `verified` by empty `thinking` plus no chip
  without a job.

## Phase 1 — Evidence

**E1** Live `GET /api/events` (verbatim, 2026-08-19 ~09:16):

```
{"id":"000000006a85abd200000000000000010000000000000001000000009e3779b8",
 "content":"nostr:npub1kz2nalxtd4kh5g83ll94zugya3zl5uxmd2e8fwd39se849yvrxzszp2z2e tell me a joke",
 "reply_to":""}
{"id":"000000006a85abdd00000000000000010000000051ed270a0000000000000007",
 "content":"Why did the scarecrow win an award? Because he was outstanding in his field! \ud83d\ude04",
 "reply_to":"000000006a85abd200000000000000010000000000000001000000009e3779b8"}
{"id":"000000006a85abe300000000000000020000000000000002000000009e3779bb",
 "content":"tell me another",
 "reply_to":"000000006a85abd200000000000000010000000000000001000000009e3779b8"}
```

Three notes. Follow-up content is exactly `tell me another`. No
`nostr:npub1…` on the third note.

**E2** Live `GET /api/status`: `"thinking": []`, `"events": 3`,
`"ok": true`. No in-flight job.

**E3** Live `GET /api/session` channel `my-new-channel`:
`"robots":["happy"]`, `"robot_reply":"mention"`, Happy provider
`grok-build`.

**E4** `hush_intel_consider` (`hush-c/src/hush_intel.c:128-131`):

```
if (strcmp(ev->tags[i][0], HUSH_INTEL_TAG_P) != 0)
    continue;
hush_intel_handle_robot(store, launch, ch, ev, ev->tags[i][1]);
```

No `p` tag → no hold, no recap, no `hush_agent_consider`.

**E5** `hush_agent_consider` (`hush-c/src/hush_agent.c:204-207`):
same `p`-tag loop. No `p` → no Grok job.

**E6** Thread submit (`hush-c/demo/index.html:2254-2262`):

```
const body = { content: text, kind: 1, channel: channel, reply_to: openThread };
extra.filter((p) => p.kind !== "human").slice(0, 8).forEach((p, i) => {
  body["mention_" + i] = p.npub;
});
```

`extra` is this send's pills only. A bare "tell me another" posts
zero `mention_N`.

**E7** `UI_SPEC.md:321-323`:

```
Submit posts reply_to=<root id> and mention_0…N only for **new** pills
typed in this send. Do not re-mention every robot already in the
member set.
```

**E8** `PLAN_CONV_INTEL_POLICY.md:135-141` Milestone 4.2:
"thread follow-up is mention-gated". Risk 5: "Thread composer
re-mention → stop auto-pills on follow-up."

**E9** `UI_SPEC.md:466-470` token-spend rule: a robot spends tokens
only when "A human mentioned it (`p` tag)."

**E10** `test_intel.c:95`: `"no mention stays silent"`. Channel-level
silence without a `p` tag is a locked test.

**E11** Thinking paint (`index.html:2610-2611`, `2751-2772`,
`2803-2808`): `thinkingFor` filters `status.thinking` by
`t.parent === id`. Empty array → `#thread-think` stays blank.
`paintThink` returns immediately when `jobs.length` is 0.

**E12** `hush_agent_status_append` emits
`{"name":"…","parent":"<root id>"}` only for `g_jobs[i].busy`.
No busy job → `"thinking":[]`.

**E13** `check_launch.sh:90-94` forbids
`bots.filter((b) => b.kind !== "human")` (the old "remention every
member" path) and requires `extra.filter((p) => p.kind !== "human")`.

**E14** `check_agent.sh:97-99` follow-up still sends
`"mention_0":"${npub}"`. The agent smoke test already treats a
follow-up as mention-bearing.

**E15** Research catalog type H1
(`RESEARCH_CONV_INTEL_POLICY.md:112`): "1 human : 1 robot …
Mention starts a thread + one Grok job". Scenario 18 (1:n) is the
one that banned auto-remention of the whole set.

### Smuggled assumptions

| Claim | Tag | Falsifier |
|---|---|---|
| Grok hung on the second joke | assumption | E1+E2: no second job exists |
| Thinking CSS is broken | assumption | E11+E12: chip only paints a live job |
| Policy is `off` | assumption | E3: `robot_reply` is `mention` |
| Burst hold is still waiting | assumption | E4: no `p` tag, so no hold; also no recap note in E1 |
| User typed `@Happy` on the second send | assumption | E1 content is `tell me another` |

Linus strikes "I never got my next reply" as `[not evidence]` — that
is the symptom. E1 is the clue.

### Timeline

1. Human posts root with `nostr:npub1kz2n…` (mention + ask).
2. Intel sees `p` (or content-derived mention via the first send's
   `mention_0`). Grok job. Think chip would have been live.
3. Happy note stored (scarecrow). Job ends. `thinking` empty.
4. Human, still in `#thread-pane` titled `1:1 with Happy`, sends
   `tell me another` with no pill.
5. HTTP stores kind 1 + `e`=root, zero `p` tags.
6. Intel / agent no-op. Status stays `{thinking:[]}`.
7. UI has nothing to paint. Send stays enabled. Silence.

Brian Cox: step 6 cannot be caused by step 2. The first job already
reaped.

## Phase 2 — Hypotheses

**H1** Thread follow-up omitted the `p` tag (mention-gated composer).
Intel never started a job. Think chip had nothing to show.
Explains E1, E2, E4, E5, E6, E11. Does not by itself explain a
missed chip *during* the first joke (operator may have blinked;
that job did finish).
Falsifier already run: live events lack `nostr:` / would lack
`mention_0`.
Data **8/10**. Linus **9/10** on "this is the cheap one."

**H2** Job started; thinking JSON or CSS failed.
Does not explain E2 empty thinking *and* E1 with no fourth note.
Sherlock: a theory that needs two independent bugs. **2/10**.

**H3** Grok started and died without inserting a note.
Would require a busy-then-reaped job. E2 is idle now; E1 has no
on-deck / error line. Possible only if the job never existed.
**2/10**.

**H4** Channel policy blocked the second ask.
E3 is `mention`, not `off`. Deny lines from intel are stored notes
(E4 policy_blocks). E1 has none. **1/10**.

**H5** 1:1 should inherit the sole member robot; PR #42 over-corrected
when it stopped *all* auto-pills to fix 1:n fan-out (E8, E15).
This is H1's *why*, not a rival mechanism.
Linus: implied fix is one `if (bots.length === 1)` in the thread
submit. Do not touch C. Do not remention 1:n.

## Phase 3 — Bayes (top three)

Priors from this codebase: mention-gating was an explicit M4.2
commit (E8). UI bugs that match a locked rule outrank hung
subprocesses when the store is complete.

| H | Prior | P(E\|H) | Unnorm | Posterior |
|---|---:|---:|---:|---:|
| H1 omit `p` / inherit missing | 0.55 | 0.95 | 0.523 | **0.847** |
| H5 same mechanism, named as over-correction | folded into H1 | — | — | — |
| H2 think-paint broken | 0.25 | 0.10 | 0.025 | **0.040** |
| H3 job died silent | 0.20 | 0.35 | 0.070 | **0.113** |

```
P(H1|E) = 0.523 / (0.523+0.025+0.070) = 0.847
P(H3|E) = 0.070 / 0.618 = 0.113
P(H2|E) = 0.025 / 0.618 = 0.040
```

Likelihoods cite E1 (no mention text), E2 (empty thinking), E6
(composer), E4/E5 (`p`-only dispatch).

## Phase 4 — Second debate

Data: H1 posterior 0.847, margin 0.734 over H3. Numbers justify a
scoped UI fix. **8/10** on diagnosis; fix unexecuted so path **6/10**.
Sherlock: "What single piece of evidence would move these most?"
A live POST of the same follow-up *with* `mention_0` that then
fills `thinking` and stores a fourth note. Worth doing as verify,
not as a delay. **8/10** proceed.
Linus: H1's diff is smaller than H2's (no CSS archaeology) and
smaller than any C change. Keep `test_intel` "no mention stays
silent" on the *channel*. **9/10** smallest-diff.
Brian Cox: arrow of time matches H1. First job completed; second
arrow never left the composer. Optimistic think chip is mass we
can add only after inherit exists, else it would lie. **8/10**.

Agreement: H1. No distillation loop.

## Phase 5 — Problem and scope

**Problem.** In a 1:1 thread the human reasonably talks to the one
robot already in the pane. PR #42 stopped the thread composer from
attaching member robots so a 1:n follow-up would not fan out. That
rule also stripped the sole robot from a 1:1 follow-up. Intel and
agent only wake on a `p` tag, so "tell me another" stored and died.
The think chip is honest: there was no job.

**Scope of work.** Thread submit: when this send has no robot pills
and `threadMembers(root, kids)` is exactly one robot, attach that
robot as `mention_0` (and prefix `nostr:<npub>` so the bubble still
reads `@Happy`). Optional: optimistic `#thread-think` chip keyed to
that send until live `status.thinking` or a new reply arrives.

**Will not.** Re-mention every member on 1:n. Change
`hush_intel_consider` to inherit without a `p` tag. Change channel
(non-thread) composer. Shrink other drawers. Touch Deepseek. New
policy values. Foreign-home writes.

## Locked architecture

- Wire: same as the first 1:1 ask. `POST /api/event` with
  `reply_to=<root>` + `mention_0=<happy npub>`.
- Gate: `threadMembers(...).length === 1`. 0 robots → stay silent
  (SELF / HH). 2+ → stay mention-gated (Hn).
- Think chip: still driven by `status.thinking`. Optimistic local
  row allowed only after this send attached a mention.
- C: no change. `test_intel` channel silence stays.
- Tests: `check_launch.sh` keeps the 1:n ban; adds a grep that the
  1:1 inherit exists.

## Cross-examination (required)

Data: E1 follow-up has no mention text, so intel cannot have run.
Sherlock: "Data, you claim X, but why does E7 (UI_SPEC) say do not
re-mention members — is this a spec violation if we inherit?"
Linus: Sherlock, stop. E15 type H1 is "mention starts a thread";
the human already mentioned Happy on the root. Inherit is the
thread continuing, not a 1:n fan-out. Spec sentence in §13 needs
a 1:1 exception, not a rewrite of §20.
Brian Cox: Linus, you are not reversing time. The root mention is
in the past; inherit copies that fact onto the new note so the
causal arrow can fire again.
Sherlock: No objection on this point once §13 is amended.
