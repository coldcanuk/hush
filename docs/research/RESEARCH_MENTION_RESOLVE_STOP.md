# Research: mention names vs keys, last-robot over-roll

Incident: `@Happy generate a riddle and let @Major solve it.`
Happy generated the riddle (correct split). Major answered "A river."
(correct split). Two remaining failures:

1. Visible mentions were `@npub1t337pnf` instead of `@Major`.
2. Major then appended `your turn, @npub1t337pnf take it from here`
   after his assignment was already done.

Using the four-minds debug protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- GitHub MCP `list_pull_requests`: attempted, gateway failed. Not used.
- Gmail / Calendar / Drive / Outlook / Tasks: not in domain. Skipped.
- No Nostr/NIP docs MCP. NIP-27 display contract confirmed via
  public spec: clients replace `nostr:npub1…` with `@name`
  (`[web:2]` nip-27 example: stored `hello nostr:nprofile1…`, Carol
  renders `@mattn`).
- Local evidence: `hush-c/src/hush_agent.c`, `hush-c/demo/index.html`,
  `docs/ROBOT_TO_ROBOT.md`, `UI_SPEC.md`, `check_agent.sh`.

Data: NIP-27 and UI_SPEC agree the wire is `nostr:npub`, the pane is
`@Name`. Sherlock: the spec is silent on truncated npubs; that silence
is the bug. Linus: we are not reading Wikipedia; we have the renderer
and the snip. Brian Cox: the arrow is snip → LLM copy → exact lookup
miss → 12-char fallback, then PEER_STANDARD → last robot imitates a
handoff. No objection on skipping collab MCPs.

## Smuggled assumptions

| Claim | Tag | Falsifier |
|---|---|---|
| Grok session memory caused the key | assumption | `--no-memory` is on the argv |
| Follow-queue race caused Major's extra text | assumption | extra text is inside Major's one note |
| Happy wrote a full Major npub | assumption | 12-char fallback only fires on lookup miss |
| UI already resolves like `who()` | assumption | `renderPreservingMentions` uses `===` |

## Evidence

**E1.** User quote (Happy body):
`but never walks? @npub1t337pnf your turn, Do.`
Wanted: `[@]Major`. Trailing phrase was judged good.

**E2.** User quote (Major body):
`A river. your turn, @npub1t337pnf take it from here.`
Major was only instructed to answer.

**E3.** Renderer exact-match + 12-char fallback
(`hush-c/demo/index.html`):

```javascript
const hit = mentionRoster().find((h) => h.npub === npub);
span.textContent = "@" + (hit ? hit.name : npub.slice(0, 12));
```

`npub.slice(0, 12)` of a token starting `npub1t337pnf…` is exactly
`npub1t337pnf`. That is E1's visible token.

**E4.** `who()` / ack chips use `sameKey` (prefix). Author line shows
`Major`. `mentionedRobots` p-tag path also uses `sameKey`. Body pills
do not. Asymmetric matching.

**E5.** `HUSH_AGENT_SNIP_MAX = 160`. `hush_agent_snip_line` hard-caps
there, mid-token:

```c
if (cap > (size_t)HUSH_AGENT_SNIP_MAX)
    cap = (size_t)HUSH_AGENT_SNIP_MAX;
for (i = 0; src[i] != '\0' && o < cap &&
     i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
```

**E6.** Measured (Python, 63-char npubs, `nostr:`+npub = 69):

- Ask `@Happy generate a riddle and let @Major solve it.` → 175 chars.
  Snip 160 cuts the second npub. `major_full_in_snip False`.
  Captured prefix starts `npub1t337pnf…`. UI fallback
  `@npub1t337pnf` — exact match to E1.
- Existing check_agent ordered ask
  `nostr:H tell a joke. nostr:M was it funny?` is ~167 chars, same cut.
- Happy's riddle note ~237 chars; Major's thread snip of it cuts the
  second token to `nostr:n`.

**E7.** Two-robot and explicit jobs skip the peer-name list
(`hush_agent_fill_job`):

```c
if (!in->scoped && job->n_co_robots > 0 &&
    !(in->mode == HUSH_AGENT_MODE_BROADCAST && job->n_co_robots == 1) &&
```

The user's case is exactly that skip: two robots, explicit *or*
cooperate. Prompt never says `@Major`. When the list *is* injected it
teaches keys: `"%s%s (nostr:%s)"`.

**E8.** `HUSH_AGENT_PEER_STANDARD` (always in hygiene + rules):

```
keep nostr:npub mentions in the same sentence order they were given.
… To call a peer, write their name plus a phrase of intent
(e.g. "your turn, Major").
```

That is why Happy's trailing phrase was good, and why Major copied it.

**E9.** Grok argv includes `--no-memory` (`HUSH_AGENT_GROK_NOMEM`).
Not a memory incident.

**E10.** `hush_agent_on_posted` → `hush_agent_follow_kick` is
sequential. Major's extra words are in the same stored note as
"A river.", not a second dispatch. Not a timing incident.

**E11.** `finish_job` only rewrites `@npub1` → `nostr:npub1`. It does
not expand truncated npubs and does not rewrite `@Major` →
`nostr:<full>`. `strcat` into a same-sized temp is unbounded.

**E12.** `hush_agent_lookup_robot` / `hush_agent_key_matches` are
exact `strcmp` on npub or hex. A truncated token does not resolve.
Follow still works because `hush_agent_collect_hexes` also walks
p-tags (the human `mention_N` tags). Dispatch survives; display does
not.

**E13.** UI_SPEC §13: composer never displays `nostr:npub1…`; stored
content uses `nostr:` at those offsets; render turns those tokens into
in-sentence `@Name` pills.

## Hypotheses

| Id | Claim | Explains | Does not | Falsifier |
|---|---|---|---|---|
| H1 | 160-char snip cuts the 2nd `nostr:npub`; LLM copies the stump; E3 paints `@`+12 | E1, E3, E5, E6, E12 | Major's extra sentence | Post two-mention note; `grok-p.log` `P:` lacks full 2nd npub |
| H2 | 2-robot/explicit jobs never inject `@Name`; PEER_STANDARD teaches `nostr:npub` | E1, E7, E8 | UI fallback shape | Prompt dump has no `@Major`, has `nostr:` |
| H3 | Body pills use `===`, acks/`who()` use `sameKey` | E1 vs author `Major`, E3, E4 | Over-roll | Change only renderer; truncated token still pills `@Major` |
| H4 | PEER_STANDARD always orders a handoff; last robot is not told to stop | E2, E8, E10 | `@npub` glyph | Last-job prompt contains a stop rule; Major's note is only the answer |
| H5 | Grok memory | — | E9 | argv without `--no-memory` |
| H6 | Race | — | E10 | two Major notes |

Data: H5/H6 have never been observed here; strike. Sherlock: H1+H2+H3
together are not "a theory of everything" — they are three layers of
one mention path. Linus: implied fix is one module (`hush_agent.c`)
plus the renderer already sitting next to `sameKey`. Brian Cox: no
time reversal; snip happens before spawn; fallback happens at paint.

## Bayesian (top three for symptom 1, H4 separate for symptom 2)

Priors from this codebase: snip-too-short and prompt-teaches-keys are
the common class (one-joke-snip already existed; PEER_STANDARD was
added to stop *bare* mentions, not keys). UI `===` vs `sameKey` is a
known seam (acks were patched, pills were not).

```
H1 prior 0.40  P(E|H1) 0.90  unnorm 0.360
H2 prior 0.35  P(E|H2) 0.80  unnorm 0.280
H3 prior 0.25  P(E|H3) 0.70  unnorm 0.175
sum 0.815
P(H1|E)=0.442  P(H2|E)=0.344  P(H3|E)=0.215
```

H4 for symptom 2: prior 0.70 given E8 always-on "your turn, Major"
and no last-robot flag. Complementary, not competing.

Data: **7/10** the numbers justify a scoped combined fix (margin on
H1 is not huge; H2/H3 are cheap and on the same path). Sherlock: the
highest-VoI check is `P:` containing both full npubs — we will add
that test, not wait. Linus: UI `sameKey` is the smallest display
diff; snip+rewrite is what makes the wire valid NIP-27. Do both.
Brian Cox: static universe for this bug (no rollout/cache); **8/10**
on the arrow.

## Unanimous agreement (pre-fix)

- Data: yes. **6/10** until tests (cap).
- Sherlock: yes. **6/10**. Would raise on `P:` both-npubs + stored
  `@Major` → `nostr:<full>`.
- Linus: yes. **6/10**. No new module. Helpers only.
- Brian Cox: yes. **6/10**. Causal order matches.

Pass. Implement.

## Architecture lock

1. Wire stays NIP-27 `nostr:<full npub>`. Pane shows `@Name`.
2. Robots are told to write `@Name`, never keys. Server rewrites
   `@Name` and truncated `npub1…` to `nostr:<full>` before insert.
3. Snip must not cut a `nostr:npub1` / `npub1` token. Soft cap may
   rise so two mentions + a short clause fit.
4. Last robot on a note: do the assignment and stop. Non-last may
   still write `your turn, @Name` (E1 praised that).
5. Renderer mention lookup uses the same `sameKey` as `who()` / acks.
6. Not in scope: hop policy, leader election, intro table, memory
   flags, new persistence.
