# RESEARCH: conversation intelligence + channel policy leash

Worktree: `/opt/repo/hush/worktrees/conv-intel-policy`
Branch: `gb/conv-intel-policy`
Base: `main` `e8afb87f8` (PR #41)

Using the four-minds protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP servers: analyze (C/headers unsupported), apps, buzz_publish,
  developer, extensionmanager, skills, summon, todo.
- No documentation or observability MCP for the hive UI or Grok Build.
- Consulted: `PRIME_DIRECTIVE.md`, `UI_SPEC.md` §13–§16, `hush_agent.c`,
  `hush_launch.h`, `hush_http.c`, `demo/index.html` Manage Channel,
  `check_agent.sh`, `test_launch.c`.
- Operator request is a feature, not a live incident. Four minds still
  rank the failure modes before any code.

Data: current Hush has threads, mentions, and a roster-only Manage
Channel drawer. There is no burst window, no confirm-understanding
note, and no channel policy struct. **8/10**.
Sherlock: "Data, why not survey Slack/Discord debounce papers?"
Because the product already chose mention-gated dispatch. The missing
leash is local, not a literature gap.
Linus: do not invent a conversation-OS. One bounded C module plus
pills in an existing drawer.
Brian Cox: tokens burn one way — mention → `hush_agent_consider` →
`grok -p`. Cut that arrow unless a human asked and a policy allows it.

## What is asked (quoted)

1. "in a threaded chat, a human can send multiple messages at a time."
2. "There can also be more than one human and the humans can be
   conversing with each other and not wanting a reply from the agents."
3. "Sometimes the human is sending multiple messages to the agents but
   we the robots don't know which one which means we need to ask for
   clarification."
4. "If the human is sending multiple messages but each messages is
   really part of one big message, we should put them together,
   summarize them and ask the human to confirm if we understood them
   correctly."
5. Conversation types: 1:1 H:R, 1:1 H:H, 1:1 R:R, 1:n mixed. Channels
   of just robots (configurable) or just humans.
6. "We need to allow the humans to apply a channel policy. This way
   the robots will have a leash… The policy manager will be available
   under `Manage Channel`."
7. "Display the different types of conversations possible… add a
   column for recommendations… List all the scenarios you can come up
   with… figure out how best to manage the conversations knowing this
   list."

## Phase 1 — Evidence

**E1** `hush_agent_consider` (`hush-c/src/hush_agent.c:195-208`) walks
every `p` tag on a kind-1 note and calls `hush_agent_handle_mention`
immediately. There is no wait, no batch, no "was this for me?".

**E2** `hush_agent_handle_mention` (`:950-979`) returns only when the
mention is the signed-in human, an unknown key, or the same robot is
already busy on the same root. Otherwise it starts Grok (if ready) or
posts the on-deck note. Mention = job. No mention = silence.

**E3** `hush_agent_robot_busy` (`:926-947`) is per (robot, root). A
second mention of Happy on a *different* root while Happy is thinking
starts a second job (table cap `HUSH_AGENT_JOBS_MAX = 4`). Four chatty
roots can fill the table.

**E4** `hush_http_serve_post` (`hush-c/src/hush_http.c:508-549`) stores
the note then calls `hush_agent_consider`. No channel roster check. No
policy check. A robot mentioned in a humans-only channel still fires.

**E5** `hush_launch_channel_t` (`hush-c/include/hush_launch.h:34-43`)
holds name, slug, id, group_id, humans[8], robots[8]. No policy
fields. Empty lists mean "whole hive" (`hush_launch_set_channel_roster`
comment `:148`).

**E6** Manage Channel (`demo/index.html:789-808`, save `:2897-2911`)
posts `{action:"manage", slug, human_0…, robot_0…}` only. UI_SPEC §14
is membership pills. There is no leash UI.

**E7** Thread pane (`UI_SPEC.md` §13) already supports 1:1 and 1:n
*human + robots*. Title is robot names. Help is `1:1 with Happy` or
`1:n · you + Happy, Payne`. Human-human and robot-robot rooms are not
typed. Channel stream lists roots; replies hide behind Thread.

**E8** Thread composer (`UI_SPEC` §13) re-mentions every robot already
in the member set on every send. A chatty human who hits Send four
times in a 1:n thread can start four Grok jobs (busy-guard only
blocks the *same* robot on the *same* root while a job is live).

**E9** Job hygiene is already tight: `--max-turns 2`, `--reasoning-effort
low`, empty cwd, tool denylist, 90s timeout, `HUSH_AGENT_JOBS_MAX 4`.
The leak is *when* a job starts, not how long one job runs.

**E10** Persist path (`hush_launch_put_channel_lists`) writes
`channel_nhumans` / `channel_nrobots` and indexed keys into
`vibe.json`. Additive policy keys fit the same pattern.

**E11** Starter channels are `general`, `welcome`, `agents`
(`hush_launch.c` push of `HUSH_LAUNCH_CHAN_*`). `#agents` is not
robot-only; roster empty = whole hive, so humans and robots both
speak and any mention fires.

## Conversation types (as Hush can actually host)

Hush is one vibe, many channels, optional thread panes. A "type" is
the *live mix on one channel or one thread*, not a new protocol.

| Type | Who is in the room | What Hush is today | What it should become |
|---|---|---|---|
| H1 | 1 human : 1 robot | Mention starts a thread + one Grok job | Default. Burst-hold, then one job. |
| Hn | 1 human : n robots | Thread composer re-mentions every robot | Mention-gated. Only named robots reply. |
| HH | 1 human : 1 human | Same as open channel; mention of a robot still fires | Humans talk; robots silent unless `@` *and* policy allows. |
| HnH | n humans, 0 robots | Possible via Manage Channel empty-robots? Empty = whole hive | Explicit humans-only roster + `robot_reply=off`. |
| R1 | 1 robot : 1 robot | No robot-originated notes except replies / on-deck | Allowed only on a robot channel with `robot_talk` on. |
| Rn | n robots, 0 humans | Same | Configurable robot room. Default `robot_talk=off`. |
| MIX | n humans + n robots | Open channel + any `@` fires | Policy: mention-only (default), off, or confirm-first. |
| SELF | 1 human, 0 robots | Notes store; no jobs | Stay quiet. No Payne auto-brief. |
| BUSY | any + a live job | Same-root same-robot mention is dropped | Keep. Show thinking. Queue at most one follow-up. |

## Scenario catalog + how to deal

Format requested: `situation, {solution1, solution2, …}`.
**Bold** = chosen default for this slice.

1. Chatty human sent 4 messages in a row (same channel, no `@`)
   {ignore all — they are talking to the room, **hold 2s then stay silent because no mention**, auto-summarize anyway}
2. Chatty human sent 4 messages, last one `@Happy`
   {four jobs, job on every note, **hold `burst_ms`, fold the burst into one prompt, one job**, ask which note to answer}
3. Chatty human sent 4 messages, each `@Happy` (four Sends)
   {four jobs, **hold + coalesce into one confirm note then one job**, answer only the last}
4. Four messages that are clearly one thought split by Enter
   {treat as four asks, **coalesce + one-line recap + Confirm / Correct**, answer last line only}
5. Four messages that are four different asks
   {answer all (token fire), answer last, **recap as a numbered list and ask which to do first**}
6. Human-human talk, no mention
   {Payne jumps in, **robots stay dark**, optional "I can help" once per hour}
7. Human-human talk that `@`s a robot mid-thread
   {ignore because humans were talking, **one job, scoped to the mention + burst window**, reply in that thread only}
8. Ambiguous `@` — two robots mentioned, ask is singular ("fix this")
   {both reply, first roster robot replies, **one confirm: "Happy or Payne?"**}
9. Ambiguous ask — one robot, two possible readings
   {guess, **one clarifying question, no tools / no extra turns until the human answers**}
10. Human talks over a thinking robot (sends again while `.think` is live)
    {second job, drop the second, **queue one follow-up; chip stays; Send in the pane stays disabled for that robot**}
11. Two humans talk over each other in a mixed channel
    {robot answers both, **stay silent unless `@`**, if `@` wait for the burst to settle then recap}
12. Two robots talk over each other (both mentioned, both Grok-ready)
    {let both run, **mention-gated; `max_jobs` + `cooldown_s`; second robot on-deck "standing by"**}
13. Robot replies to a robot (R:R loop)
    {allow, **default `robot_talk=off`; if on, cap `robot_hops` (1) so a robot note cannot re-mention and restart Grok**}
14. Robot-only channel, humans browsing
    {robots free-fire, **policy `kind=robots`, `robot_talk` explicit, humans read-only unless added**}
15. Humans-only channel, someone `@`s a robot who is not on the roster
    {current: job still starts (E4), **deny; on-deck "not on this channel"**}
16. Open channel (empty roster = whole hive), stray `@Payne`
    {always fire, **honor `robot_reply` (mention / off / confirm)**}
17. Thread 1:1, human sends three fragments then "go"
    {three jobs, **coalesce until quiet `burst_ms` or the word is a go-cue, then one job**}
18. Thread 1:n, human addresses one robot by name in prose without a new pill
    {every member robot runs (E8), **do not auto-remention the whole set on follow-up; only new pills + explicit `@`**}
19. Human pastes a wall of text + a one-line ask
    {whole wall as the prompt (today), **summarize to `HUSH_INTEL_SNIP_MAX` in the recap; full text still in the job note**}
20. Human sends "ignore that" / "wait" / "stop" during a burst
    {job already spawned, **cancel queued job; if Grok is live, leave it (no SIGKILL this slice) and post "holding"**}
21. Empty note or mention-only note (`@Happy` and nothing else)
    {Grok invents a task, **on-deck "Say the ask." No job.**}
22. Duplicate send (same content, same author, <1s)
    {two jobs, **drop the duplicate**}
23. Stale thread — last note hours ago, new `@`
    {include old jokes as "thread so far", **transcript already last-N (E existing); no change**}
24. Policy `robot_reply=off`, human `@`s anyway
    {fire (today), **on-deck "This channel is humans talking. Change policy in Manage Channel."**}
25. Policy `robot_reply=confirm`, human `@`s
    {fire immediately (today), **Payne/Happy posts a recap note with Confirm / Edit; job starts only on Confirm**}
26. Robot-only channel with `robot_talk=on` and no hop cap
    {infinite token loop, **`robot_hops=1`, `cooldown_s`, `max_jobs`**}
27. Channel with 0 humans and 0 robots stored (open hive) treated as "just robots"
    {wrong, **kind is explicit: `open` / `humans` / `robots` / `mixed`. Empty roster + `open` stays today's whole-hive.**}
28. Multiple humans `@` the same robot in one burst
    {one job that only sees the last human, **one job; prompt lists each human line; address the last speaker**}
29. Human edits by sending a correction as a new note ("\*typo: deploy")
    {two jobs, **burst coalesce treats `*` / "correction:" / "I meant" as a fold, not a new ask**}
30. Voice / whisper barge-in while a text job is live
    {out of scope this slice, **no change; voice stays mute-local**}
31. Operator opens Manage Channel mid-burst and flips `robot_reply` to off
    {in-flight Grok still finishes, **new considers honor the new policy; in-flight stays**}
32. `#agents` used as a robot workshop
    {open hive, **default starter policy stays `open` + `mention`; operator may set `kind=robots`**}

## How Hush should manage conversations (decision)

One rule, greppable:

> A robot spends tokens only when (a) it is on the channel roster or
> the channel is `open`, (b) `robot_reply` is not `off`, (c) a human
> mentioned it, (d) the note is not mention-only, (e) the burst window
> has closed or the human confirmed, (f) hops / cooldown / max_jobs
> allow it.

Everything else is silence or a one-line on-deck note. No background
summarizer. No robot-initiated conversation unless `kind=robots` and
`robot_talk=on` (still hop-capped; this slice does not spawn
robot-originated roots).

### Burst + confirm (the chatty-human path)

1. On a mention that would start a job, do **not** exec Grok yet.
2. Park a `hush_intel_hold_t` keyed by `(channel, root, robot)`.
3. Each new note from the same human on that key folds into the hold
   (cap `HUSH_INTEL_BURST_MAX = 8` notes).
4. When quiet for `burst_ms` (default 2000):
   - 1 note, one clear ask → start the job (no confirm tax).
   - 2+ notes, all look like one thought (no `?` after the first, or
     explicit fold cues) → post a recap: "I heard: … Confirm / Correct."
   - 2+ notes that look like separate asks (multiple `?`, or "also")
     → post a numbered recap and ask which to do first.
5. Confirm is a human note whose content matches a small cue list
   (`yes`, `y`, `confirm`, `go`, `do it`, `1`, `2`, …) **or** a
   thread reply with no new ask. That note starts the one job.
6. Correct / "no" / a new ask replaces the hold and restarts the
   window.

Confirm notes are kind 1 from the *robot*, `e` = root, short, no Grok
child. The UI paints them as ordinary thread notes. Optional
`.intel-confirm` class if cheap.

### Policy (the leash)

Stored on `hush_launch_channel_t`, edited in Manage Channel, persisted
in `vibe.json`, echoed on session `channels[]`.

| Field | Values | Default |
|---|---|---|
| `kind` | `open` `humans` `robots` `mixed` | `open` |
| `robot_reply` | `off` `mention` `confirm` | `mention` |
| `robot_talk` | `0` `1` | `0` |
| `burst_ms` | 500 / 2000 / 5000 | 2000 |
| `max_jobs` | 1..4 | 2 |
| `cooldown_s` | 0 / 10 / 30 | 10 |
| `robot_hops` | 0 / 1 | 0 |

`kind=humans` forces `robot_reply=off` unless the operator sets
otherwise (save still accepts an explicit override so a humans room
can summon a robot). `kind=robots` requires at least one robot pill
and `robot_talk` visible.

Payne copy in the drawer:

> "Leash the robots. They speak when mentioned, or not at all."

### Manage Channel UI

Keep the human/robot pill lists. Add a **Policy** block above Save:

- Kind: four radios (`open` / `humans` / `robots` / `mixed`).
- Robots may reply: `Off` / `When mentioned` / `Confirm first`.
- Robots may talk to robots: checkbox, shown when kind is `robots`
  or `mixed`.
- Burst wait: `0.5s` / `2s` / `5s`.
- Max live jobs on this channel: `1` / `2` / `4`.
- Cooldown after a job: `off` / `10s` / `30s`.

Fitts ≥44px. Same `+`/`−` language is *not* used for radios (Hick:
these are exclusive). Save posts `kind`, `robot_reply`, `robot_talk`,
`burst_ms`, `max_jobs`, `cooldown_s` with the existing `human_N` /
`robot_N` fields.

### Module boundary

New leaf module `hush_intel` (not more branches inside
`hush_agent_handle_mention`):

- `hush_intel_consider(store, launch, ev)` — called from HTTP instead
  of `hush_agent_consider` directly.
- Classifies, holds, recaps, or forwards to `hush_agent_consider`.
- `hush_intel_poll(store, launch)` — from the relay pump next to
  `hush_agent_poll`. Flushes expired holds.
- Pure helpers for burst fold, cue match, hop count.

`hush_agent_consider` stays "mention → job". Intelligence sits in
front of it. Policy lives on the channel, not on the job.

## Four-minds verdict

| Voice | Claim | Confidence |
|---|---|---|
| Data | Fire-on-mention + empty-roster-open + no policy fields is the whole gap. | 9/10 |
| Sherlock | Chatty-human pain is coalescing *before* exec, not a smarter Grok prompt. | 8/10 |
| Linus | One `hush_intel` + six named policy ints. No NLP classifier. Cues are string prefixes. | 9/10 |
| Brian | Tokens die at `hush_agent_start_grok`. Every hold/deny is a saved child. | 9/10 |

Unanimous: implement the leash + burst/confirm. Do not build a
conversation classifier.

## Bayes (token-burn hypotheses)

| H | Prior | After E1–E11 | Posterior |
|---|---|---|---|
| H1 mention always starts a job | 0.40 | E1 E2 E8 | **0.55** |
| H2 chatty multi-send has no hold | 0.25 | E1 E8 | **0.25** |
| H3 humans-only cannot silence robots | 0.15 | E4 E5 E6 | **0.12** |
| H4 robot-robot loops unconstrained | 0.10 | E3 E9 | **0.05** |
| H5 UI already has a policy and we missed it | 0.10 | E5 E6 | **0.03** |

## Scope locked

**Primary goal**

1. Scenario catalog published (this file) and used as the product
   contract.
2. Manage Channel grows a Policy block that persists and leashes
   robots.
3. Chatty / ambiguous human bursts coalesce; robots confirm before
   spending a Grok child when the burst is multi-note or policy is
   `confirm`.
4. Humans-only rooms stay quiet. Robot-only rooms are explicit.

**Non-goals**

- NLP / embeddings / LLM-as-judge for "one thought vs many".
- Cancelling a live `grok` child this slice.
- Voice barge-in policy.
- Nested NIP-10, Codex/Goose live CLIs, new channel kinds on the
  wire (still kind 1 + `#h` slug).
- Auto robot-originated roots even when `robot_talk=on`.
- Changing starter channel names.

**Success criteria**

- `channels[]` in session JSON include the six policy fields.
- `vibe.json` restores them.
- Manage Channel can set them; `check_launch.sh` greps the new ids.
- `hush_intel` unit tests: silent on no mention; hold then one
  consider after burst; confirm-first does not start Grok until cue;
  `robot_reply=off` denies; hop 0 drops a robot-authored mention.
- `./configure && make && make test` green.
- PR merged; worktree removed.

**Constraints**

- C11, write-legible-c §14, `fn ≤ 40`, depth ≤ 2, ≤ 4 params.
- Prime Directive worktree. Embed UI after HTML:
  `./scripts/embed-ui.sh hush-c/demo`.
- Named constants for every burst / hop / cue bound.
- Hick: Policy is one block, ≤5 primary choices visible (kind +
  reply cover the common case; advanced in a `<details>`).

**Assumptions**

- Cue lists in English are enough for this hive (Payne voice).
- 2s default burst will not feel broken on a local relay.
- Empty roster remains "whole hive" when `kind=open`.

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. Confirm notes look like more chat and re-enter consider → mark
   recap notes with a `t` tag `hush-confirm` and ignore them.
2. `hush_launch.c` is already large → policy get/set as tiny leaves;
   do not grow `set_channel_roster`.
3. Session JSON overflow → six short fields; keep
   `HUSH_LAUNCH_JSON_MAX`.
4. Burst timer vs single-thread poll → compare `time(NULL)`; 1s
   granularity is acceptable (store `burst_ms` but flush when
   `now - last >= (burst_ms+999)/1000` seconds).
5. Thread composer re-mention (E8) fights mention-gated policy →
   stop auto-attaching every member robot on follow-up.

## Updated plan

See [`../plan/PLAN_CONV_INTEL_POLICY.md`](../plan/PLAN_CONV_INTEL_POLICY.md).
