# Technical Review — Hush `0.0.1` @ `5f67c65eb`

> Landed from the external technical review supplied as the build's knowledge
> document. Nothing here is a claim by the Hush maintainers; it is the input
> for `docs/plan/PLAN_REVIEW_HARDENING.md`.

I built it from source in a scratch copy, ran the whole unit + integration suite, benchmarked the write path, and probed a running relay from a non-loopback interface. Verification log is at the end. Nothing in your tree was modified.

**One-line verdict:** Hush has one of the most thoughtful *conversation engines* I've seen in a multi-agent chat product, sitting on a relay core that is still a prototype — no signatures, no auth, no WebSocket, one thread, 16 clients, and a full-store fsync per message. The intelligence layer is ahead of the plumbing.

---

## 1. Architecture (what actually exists)

Single ~6 MB binary, ~30k lines of strict C11, PWA served from inside it.

| Module | Role |
|---|---|
| `hush_relay.c` | `poll(2)` server; HTTP and newline-Nostr share one port, sniffed by first bytes (`hush_relay.c:491`) |
| `hush_http.c` | PWA + JSON API (30+ routes) |
| `hush_store.c` | 1024-event in-memory ring, rewritten to `store.ring` on every insert |
| `hush_intel.c` | The **leash** — who may reply, when, after how many notes |
| `hush_agent.c` | Job spawner + multi-robot orchestration (leader election, wave planning) |
| `hush_wake.c` | Durable claim ledger with 90 s leases (prevents double-reply) |
| `hush_presence.c` / `hush_cevent.c` | NIP-38-shaped presence + in-process signal ring |
| `hush_provider.c` / `hush_inference.c` | 12 providers, all via `fork/exec` (CLI or `curl`) |
| `demo/index.html` | 6,153 lines of vanilla JS — the actual product surface |

---

## 2. Strong points

1. **The conversation engine is the real IP.** `hush_intel.c` is a genuine policy layer: channel leash (`open/humans/robots/mixed`), reply mode (`off/mention/confirm`), burst holds with recap, mention-only detection, duplicate suppression, a job cap, and a per-root robot-turn cap. Almost nobody builds this; most "AI team" products let every agent answer everything and burn tokens.

2. **Multi-robot orchestration is unusually serious.** A human note is classified as SOLO / EXPLICIT / BROADCAST by extracting per-robot clauses after each `nostr:npub` (`hush_agent.c:3205`). Two robots get a cooperate prompt; 3+ get a leader election (Major first, then leadership-skill rank, then LLM election) followed by a fenced ```plan``` with **wave numbers**, `fifo`/`lifo`, and parallel groups, executed by a per-root follow queue (`hush_agent.c:3939-3999`). Explicit delegation gives each robot only its own clause with a strict-scope rule.

3. **Output hygiene is battle-scarred in a good way.** Replies are scrubbed for self-mentions, echoed asks, leaked npubs, and handoff phrases; the last robot in a wave strips every mention so A→B→A loops cannot form (`hush_agent.c:2820-2834`); the "exactly one joke" and "do not repeat a prior joke" rules exist because someone actually ran this repeatedly.

4. **It builds clean and it's tested.** `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow` with **zero warnings**; **22/22** unit tests pass; `check_agent.sh` (fake-grok mention→reply, argv assertions, context injection, scrubbing) passes; `check_collaboration.py` (room prompts, restart memory, team election/planning/handoff, six API providers, Cline CLI, a multi-megabyte event response under backpressure) passes. That is a real suite.

5. **Bounded everything.** Fixed caps on channels, group, robots-per-channel, jobs (4), holds (8), follow slots (8), tokens, prompts, buffers. No unbounded allocation in the conversation path. Assertions are on, contracts are documented at declaration sites.

6. **Secret handling is genuinely careful in the places it was thought about.** Keys live in `pass`; provider credentials reach `curl` via a 0600 `mkstemp` config or an anonymous `tmpfile()` piped to `curl --config -` — never argv, never logs. Grok runs `--no-memory`, `--disallowed-tools`, `--no-subagents`, `--disable-web-search` from an empty cwd; Codex gets `--sandbox read-only`. Hush never writes `~/.grok` or `~/.codex`.

7. **Durable claim ledger.** Before spawning a model, the relay takes a fsynced lease keyed `SHA-256(robot||":"||root)` with gossip events and expiry handling (`hush_wake.c`). That is a real distributed-systems reflex in a single-process app.

8. **Process discipline.** 2,897 commits, PR-only landings with auto-merge, hooks that block writing `main`, RDAP plan docs, and a gauntlet scoring rubric that explicitly forbids inflating scores ("raising any score without new evidence is cheating"). The repo is also free of committed build artifacts.

9. **The PWA/native-window integration is a nice touch** — a frameless `--app` window with WM minimize/maximize, own user-data-dir, and a Close/Exit distinction that actually reaps children.

---

## 3. Weak points (ranked)

### Critical — the trust boundary does not exist

1. **Zero event authentication.** `hush_event_t` has **no `sig` field at all** (`hush_event.h:20-29`); `hush_event_validate()` says so and is *never called* outside tests; the wire parser `sscanf`s a claimed `pubkey` and stores it (`hush_proto.c:127`, `hush_relay.c:549`); the relay answers `["OK", id, true]`. No schnorr verification, no id recomputation. Any TCP client can publish as the human or as a robot — and `hush_intel.c:603` treats "author is human" as the branch that unlocks robot dispatch.

2. **The HTTP API is unauthenticated and reachable off-host.** The listener binds `INADDR_ANY` (`hush_relay.c:287`) while printing `http://127.0.0.1`. I verified from the machine's LAN address `192.168.1.241`: `POST /api/identity` created an identity, `POST /api/vibe` created a hive, `POST /api/event` posted a note, all `200 OK`. `g_launch->logged_in` is a process flag, not a credential.

3. **One unauthenticated shell sink.** `POST /api/project` takes an arbitrary `path` and passes it to `system("mkdir -p '%s' && git init -q '%s'")` (`hush_launch.c:1881`). A `'` in the path escapes the quoting → command execution as the relay user. Everything else uses `execvp(argv)` without a shell — this is the single outlier.

4. **Info disclosure through the same open door.** Every reply carries `Access-Control-Allow-Origin: *` (`hush_http.c:520, 648`). `/api/session` returns the human **nsec** in cleartext during the onboarding window (`hush_launch.c:1626-1627`) plus the vibe join token; `/api/ice` returns the live TURN password; `/api/provider` can overwrite provider secrets; `/api/provider/scan` is an SSRF (caller-supplied host becomes a `curl` URL); `/api/exit` shuts the relay down.

5. **"Private vibe" and channel membership are cosmetic.** The join token is generated and displayed but never checked on any request path; no relay code compares a pubkey against channel membership. `SECURITY.md:69-72` and the README claim otherwise.

### High — it will not scale past a demo

6. **Every message rewrites and fsyncs the whole store, on the single-threaded loop.** Measured on my scratch build (1 KB notes): **0.28 ms** median with persistence off vs **5.4 ms** with it on at 120 events; and it grows with the ring — **2.1 ms @ 30 events → 18.5 ms @ 260 → 36.4 ms @ 510 → 33 ms @ 900** (969 KB ring file). A human message triggers 2–3 inserts (note + presence line + trail). Worst-case events (4 KB content + 32 tags ≈ 37 KB each) put the ring file in the tens of MB. This is the single biggest "feels bad" bug in daily use.

7. **Conversation memory is a six-note snippet window.** Context = opening note + last 6 work notes, each flattened to 384 bytes, plus the current message (`HUSH_AGENT_THREAD_MAX = 6`, `hush_agent.c:36`). No summarization, no retrieval, no durable per-thread state. Robot context files are explicitly RAM-only and lost on restart (`hush_roster.h:56-58`). If the root falls out of the 1,024-event ring, the thread degrades to "current message only".

8. **API providers get one flattened user message.** `hush_inference.c:150-177` sends `system`/`rules` + a single `user` message, `"stream":false`, no `tools`, no JSON mode, no multi-turn `messages[]`. Multi-turn quality is left on the table for every HTTP provider.

9. **No streaming, no cancel, no budget.** One blob after up to 90 s; the only "stop" is the 90 s timeout or disabling the robot; `POST /api/fixup` blocks the whole relay loop for up to 90 s. No token/cost accounting anywhere.

10. **16 clients, no timeouts, truncated frames.** `HUSH_MAX_CLIENTS = 16`, no idle timeout, no output queue; `hush_send_str` treats a short/EAGAIN write as success (`hush_relay.c:532-543`), so a slow reader gets corrupt frames. That's LAN-reachable slowloris.

11. **It is Nostr-shaped, not Nostr.** No WebSocket (standard clients cannot connect), tags and `created_at` are dropped by the line parser (`created_at` is hardcoded to `1720000000`), filter `ids`/`since`/`until`/extra authors/extra `#h` values are never parsed, `AUTH`/`COUNT` parse as UNKNOWN, kind 5 deletion and kind 7 reactions don't exist at all despite the README listing them, and `NOSTR.md` describes a different relay entirely (NIP-29, Postgres, Docker).

### Medium — correctness and craft

12. **Silent state corruption at capacity.** `hush_intel_take_hold` returns `&g_holds[0]` when all 8 slots are live (`hush_intel.c:435`), folding a 9th conversation into an unrelated in-flight hold; `hush_agent_follow_take` does the same to `g_follow[0]` (`hush_agent.c:3618`).

13. **Dead reset path → stale queues.** `hush_agent_consider` has zero callers, so `hush_agent_reset_follow` never runs; follow slots are root-keyed and reused, which can leave later re-mentions of the same robots quietly undelivered.

14. **Silent no-reply on any start failure** (job table full, ledger full, claim denied): the intro is posted, then nothing. Combined with a wave `inflight++` that increments even when `begin_work` dropped the job, a thread can jam.

15. **Replies > 4096 B become failures** (the pipe is closed, `out` is erased, and the job reports "did not return a usable reply").

16. **`cooldown_s` is stored and shown but never enforced**; `max_jobs` is enforced only for the first resolvable p-tag; `robot_hops` is a boolean, not a counter.

17. **No hardening flags in the build** — only `-Wl,-z,noexecstack`; no `-fstack-protector-strong`, `-D_FORTIFY_SOURCE`, `-fPIE/-pie`, `-z relro,-z now`. (Your local gcc defaults happen to give PIE/RELRO; that is not guaranteed elsewhere.)

18. **`/tmp` cwd and TURN state dirs are accepted if they already exist** with no ownership/symlink check.

19. **God-files and three JSON parsers** (`hush_json_read.c`, the `strstr/sscanf` parser, and `hush_http.c`'s ad-hoc field search that drops backslashes) — inconsistent decoding of the same input.

---

## 4. How conversation management actually works

### 4.1 The life of a message

```
composer submit (index.html:3548)
  → assembleMentionContent: "@Name" → "nostr:npub1…" in content, mention_0..N in body
  → POST /api/event
      author FORCED to g_launch->human.pubkey_hex (hush_http.c:763)
      kind=1, tag h=<channel>, tag e=<reply_to>, tag p=<mention_i> (hush_http.c:750-772)
      hush_event_compute_id
      hush_store_insert            ← rewrites + fsyncs store.ring
      hush_intel_consider          ← the only agent entry point in production
      presence "Conversing"        ← 1–2 more inserts
  → UI polls /api/status, /api/events, /api/session, /api/presence every 1000 ms
```

Note the asymmetry: the **HTTP** path is the only one that reaches robots. An event arriving over the newline-Nostr port is stored, acked and fanned out, but never dispatched (`hush_relay.c:545-554`) — and its tags and timestamp are discarded anyway.

### 4.2 The reply decision (as implemented)

```
for each p-tag in note:
    robot = lookup(Payne → roster)                     hush_intel.c:291
    if robot_reply == off            → deny note "This channel is humans talking."
    if robot not on channel roster   → deny note "Not on this channel."
    if content is mention-only       → deny note "Say the ask."
    if author is not human && robot_hops == 0 → deny "Robots do not chain here."
    if live_jobs >= max_jobs && is_lead_p     → deny "Holding…job cap."
    ALWAYS insert "Mention received." (robot-signed)   hush_intel.c:657
    fold note into hold(channel, root, robot)
    if hold.awaiting && content ∈ {yes,y,confirm,go,do it,1..8} → dispatch now
    else if robot_reply == confirm → post recap (t="hush-confirm"), await cue
    else if nnotes == 1            → dispatch now
    (otherwise: wait for burst_ms, then dispatch from hush_intel_poll)

dispatch → hush_agent_mention:
    skip if already busy on this (robot, root)
    classify SOLO / EXPLICIT / BROADCAST
    3+ robots → elect leader → leader emits ```plan``` → wave queue
    2 robots  → cooperate prompt, first runs now, second queued
    explicit  → each robot gets only its extracted clause
    begin_work: turns_full? → chaperon line, stop
                one intro per (robot, root) via the wake ledger
                claim lease → fork worker → spawn provider
    finish_job: scrub reply → insert note → on_posted → kick next wave
```

**Key facts people get wrong about Hush:**

- **`burst_ms` only matters in `confirm` mode.** In the default `mention` mode the first note releases immediately and clears the hold, so follow-up notes each start a fresh hold. "Burst coalescing" as documented is largely not in effect.
- **"Confirm first" only works inside a thread.** The cue is matched against `(channel, root, robot)`; typing "1" in the channel creates a new root and re-recaps.
- **`kind` barely matters.** The only read is `open && nrobots==0` ⇒ whole hive. Otherwise membership in `robots[]` is the gate.
- **Two humans cannot share one robot.** Authorship is forced to the single local identity, and roster "members" are address-book entries only. In practice Hush is a one-human hive.

### 4.3 State that exists

| State | Where | Lifetime |
|---|---|---|
| 4 job slots (`NOTE/FIXUP/PLAN/ELECT`) | `g_jobs[4]`, `hush_agent.c:167` | process |
| 8 intel holds, keyed `(channel,root,robot)` | `g_holds[8]`, `hush_intel.c:34` | process |
| 8 follow queues, keyed by root, with waves | `g_follow[8]`, `hush_agent.c:203` | process |
| Wake claim ledger, 256 slots, 90 s leases | `wake.ledger`, fsynced | durable |
| Presence lines 30315 + trail 1038 | store ring | 30 s stall / 45 s idle |
| Signal ring (`mention/intro/job_start/…`) | `g_cevent`, 64 events | process, **never consumed by the PWA** |
| Chat history | `store.ring`, 1024 events | durable |

---

## 5. Conversation matrix

Legend: ✅ works · ⚠️ works with a caveat · ⛔ not possible.

| # | Pairing | How it starts | Who answers / routing | Guardrails | Wire shape | Status |
|---|---|---|---|---|---|---|
| 1 | **Human ↔ Human** (channel) | Composer post | Nobody; peers poll every 1 s | none needed | kind 1, `h` tag, no `e` | ✅ the core path |
| 2 | **Human ↔ Human** (1:1) | — | — | — | — | ⛔ **no DM concept exists**; the 1:1 pane is human+robot only |
| 3 | **Human ↔ 1 robot** (channel) | `@robot` pill → `p` tag | intel → agent job → one kind-1 note, `e`=root, `p`=human | reply mode, mention-only, job cap, 90 s, 4 turns/root | kind 1 + `h`,`e`,`p` | ✅ |
| 4 | **Human ↔ 1 robot** (thread pane follow-up) | reply in pane with no new `@` | UI prepends `@Name` when the thread has exactly one robot (`index.html:3599`); **server also inherits the root's first robot p-tag** (`hush_http.c:820-833`) | same leash | kind 1 + `e`=root | ✅ |
| 5 | **Human ↔ 2 robots** | one note, two `p` tags, no clauses | BROADCAST → "cooperate, divide the labor"; first runs now, second queued | turn cap, last-robot strips mentions | two notes, same `e` | ✅ |
| 6 | **Human ↔ 3–8 robots** | one note, 3+ `p` tags | election pass → leader ```plan``` fence → wave queue (fifo/lifo, parallel groups) | jobs max 4 → overflow **dropped**, not queued | plan note + per-robot notes | ⚠️ works, fragile parser + silent drops |
| 7 | **Human ↔ N robots** (explicit) | `@A do X … B do Y` | clause extraction; each robot gets only its clause + strict-scope rule | `ask` is 512 B; "<4 chars" clauses ignored | one note each | ✅ |
| 8 | **Robot ↔ Robot** (scheduled) | non-last robot finishes; queue kicks the next wave | follow queue, not intel | `max_robot_turns` (default 4) counted since last human note; chaperon line when exceeded | notes carry `e`=root | ✅ |
| 9 | **Robot ↔ Robot** (spontaneous) | robot writes `@Peer`, emitting a `p` tag | ⛔ **asymmetric**: `publish_reply` never calls intel, so robot p-tags are inert for dispatch. `robot_hops==0` denies any non-human p-tagged note anyway; the UI checkbox sets `robot_talk` **and** `robot_hops` together (`index.html:5804`) | hop boolean + turn cap | `p` tag present but unused | ⚠️ only the UI benefits (ack pills / thread membership) |
| 10 | **Mixed room** (humans + robots) | channel with `kind` humans/robots/mixed | `kind` only special-cases `open && no robots`; real gate is `robots[]` membership + the leash | per-channel policy radios | kind 1 + `h` | ⚠️ works; `kind` is weaker than the UI implies |
| 11 | **Human ↔ Robot voice** | Call / Voice icon (needs `whisper` on PATH) | Browser WebRTC mesh; SDP/ICE as kind 25000 | — | kind 25000 | ⚠️ **Whisper is detection only**; agents join as signaling tiles, no STT/TTS; with TURN off, ICE uses Google STUN |
| 12 | **External Nostr client ↔ hive** | newline JSON `EVENT` / `REQ` | Nobody (no dispatch). No WebSocket, so standard clients can't connect at all | none — anyone can write any pubkey | tags/timestamp dropped | ⛔ not interoperable, and **not confidential** |
| — | **Agent actions** (files/shell) | via provider CLIs | `grok --always-approve`, `copilot --allow-all`, goose/ollama unrestricted; codex read-only | no approval gate, no sandbox, no per-robot identity | — | ⚠️ the biggest *collaboration* gap after memory |

---

## 6. What's missing to make Hush excellent at human↔AI collaboration

**Tier 1 — without these it stays a single-player toy**

1. **A trust boundary.** Authenticated local sessions + verified event signatures. This is not just security: you cannot invite a colleague, attribute a decision, or let a robot act *as* someone without it. Today "signed in" means "a process flag is set."
2. **Durable, shared understanding.** A thread brief that survives restarts and summarizes beyond the last 6 notes; artifacts (decisions, files, canvases) attached to the thread rather than lost in a ring buffer. Right now a robot that helped you yesterday knows nothing today.
3. **Incrementality and control.** Streaming replies, a Stop button, per-thread budgets, and cancellation. Watching a thinking chip for 90 s with no way to steer is the opposite of collaboration.

**Tier 2 — what makes it feel professional**

4. **Observable work.** A job ledger (tool calls, files touched, cost, outcome) and a per-team run artifact. `hush_cevent` already emits `job_start/job_done/follow/chaperon` and the PWA never reads it — half the plumbing is already there.
5. **Structured tool use with approval gates.** API providers have no `tools`; CLI providers run approval-free. An "approve this action" surface would let robots *do* things safely.
6. **Interop.** WebSocket + a parser that preserves tags and timestamps would let other humans and other AI clients join a hive instead of only the embedded PWA.
7. **Collaboration primitives on messages** — edit, delete (kind 5), react (kind 7), quote, pin a decision, fork a thread, search.
8. **Multi-human presence.** Who is here, who is typing, who owns this thread. Presence exists but is only used to color robot ack pills.
9. **Performance headroom** — incremental persistence, more than 16 sockets, no fsync on the poll loop.

---

## 7. Three features I would add, and why

### Feature 1 — **Authenticated identity and signed events** (the trust boundary)

**What:** (a) bind `127.0.0.1` by default with an explicit `--listen` opt-in; (b) issue a per-browser session token at first run and require it (header or cookie) on every `/api/*` route; (c) replace `Access-Control-Allow-Origin: *` with an Origin/Host allowlist and drop the `nsec` from `/api/session` entirely; (d) add BIP-340 verification (libsecp256k1, or OpenSSL EC verify) on `EVENT`, recompute the id, store `sig` in `hush_event_t`, and answer `["OK", id, false, "invalid: bad signature"]` when it fails; (e) implement a NIP-42-style challenge for the line protocol; (f) fix the `/api/project` `system()` sink (use `mkdir()` + `execvp` for `git init`).

**Why this is a *collaboration* feature, not a chore:** identity is the substrate for everything collaborative — attribution ("who asked for this?"), delegation ("act as me in this channel"), invitations ("join my vibe"), and accountability ("which robot did that?"). Right now anyone on the LAN can post as you, and the relay cannot tell a human from a robot from a stranger, so every multi-human workflow is impossible by construction. It is also the prerequisite for the other two: a streaming endpoint and a memory service both widen the attack surface a lot if they inherit today's API.

### Feature 2 — **Thread memory: a persistent, summarized conversation state**

**What:**
- Persist threads beyond the 1,024-event ring (SQLite, or an append-only log) with an FTS index for search.
- A **rolling thread brief**: when the window would exceed a token budget, summarize the evicted turns and pin the summary to the root as a first-class (encrypted, replaceable) event, so it travels with the thread and survives restarts.
- Feed API providers a **real `messages[]` array** (system / user / assistant, and later tool) instead of one flattened user string; keep flattening only for CLI providers.
- Make robot context files durable (they are RAM-only today) and reference artifacts (canvas files, decisions) by id rather than pasting bytes into every prompt.
- Replace the "last 6 notes, 384 bytes each" window with budget-driven selection: the brief, the last N turns, plus retrieval over the thread for the current ask.

**Why:** this is the single largest quality lever. Today a robot's memory of your project is a 2.3 KB flattened string, reset every turn, and gone if the root ages out of the ring. Collaboration requires shared context that compounds: "we decided X on Tuesday, here's the file, and here's why." Everything about agent usefulness — fewer repeated questions, consistent decisions, less prompt scaffolding — follows from it.

### Feature 3 — **Streaming, cancellable, budgeted turns with a visible work ledger**

**What:**
- `stream: true` on API providers and an SSE/NDJSON endpoint so partial output paints live into the thread pane; drain CLI stdout into chat as it arrives rather than buffering to one blob.
- `POST /api/cancel {root, robot}` → `SIGTERM` to the process group (and a Stop button on the thinking chip); today the only stop is the 90 s timeout.
- Token/cost accounting per job, robot, channel, and thread, plus a **per-thread budget the leash enforces** — a natural extension of the existing `max_jobs` / `cooldown_s` / `max_robot_turns` policy.
- Surface the already-emitted `hush_cevent` signals plus tool activity as an activity timeline on every thread, and write one `run.md`-style artifact per team run (who did what, what changed, what it cost).

**Why:** collaboration is a loop of *act → observe → steer*. Today the observe step is a pulsing dot and the steer step is waiting. Streaming changes perceived latency by an order of magnitude; cancellation makes experimentation cheap; and budgets plus a work ledger are what let a human trust a room full of agents to run unattended. It also converts the existing (and currently unread) signal ring into the product feature it was clearly meant to be.

**Honorable mention** (deliberately not in the top 3 because it depends on Feature 1): relay-to-relay federation so two hives can share a channel. That is the feature that would make "vibe" mean something beyond "my local process."

---

## 8. If I had one week (highest value per line)

1. `system()` → `mkdir()` + `execvp("git", …)`; validate the path.
2. Bind loopback by default; delete `Access-Control-Allow-Origin: *`; stop emitting `nsec`/TURN credentials over HTTP.
3. Require a session token for `/api/identity`, `/api/project`, `/api/canvas`, `/api/provider`, `/api/exit`.
4. Replace full-ring rewrite+fsync with an append-only record + periodic snapshot (this alone turns 33 ms stalls into ~0.3 ms).
5. Fix the two table-overflow clobbers (`hush_intel.c:435`, `hush_agent.c:3618`) and `hush_send_str`'s partial-write handling.
6. Add `since`/`until`/`ids` to the wire parser — or delete the claim from the README.
7. Correct the docs: `NOSTR.md` describes a different relay; `SECURITY.md` claims signatures are validated and that the store is non-durable; the README lists kinds 5 and 7 that don't exist.

---

## 9. Verification log

| Check | Result |
|---|---|
| Scratch copy build (`./configure && make`) | ✅ clean, `-Werror`, 0 warnings |
| Unit tests (`tests/test_*.c`, 22 binaries) | ✅ 22 pass / 0 fail |
| `tests/check_agent.sh` (fake grok, mention dispatch, scrubbing, context) | ✅ pass |
| `tests/check_collaboration.py` (rooms, restart memory, teams/election/handoff, 6 API providers, multi-MB history) | ✅ pass |
| Insert latency, persistence **off** | 0.28 ms median |
| Insert latency, persistence **on** | 5.4 ms median 120 events; 2.1 / 18.5 / 36.4 / 33.3 ms at rings ~30 / 260 / 510 / 900 (969 KB file) |
| LAN exposure probe from `192.168.1.241` | `POST /api/identity`, `/api/vibe`, `/api/event` all `200 OK`, no credentials |
| Repo state after review | unchanged (`git status` matches the pre-existing untracked files) |

*Method note: I did not run the window/browser shell checks (`check_win.py`, `check_browser_launch.py`), since they open GUI windows on your desktop.*

---

If you'd like this review landed in the repo, I can put it at `docs/research/REVIEW_HUSH_0.0.1.md` on a `gb/review-hush` worktree and open a PR — per your Prime Directive I won't write to `main` directly. I can also turn any of the three features into an RDAP plan under `docs/plan/` on the same branch.
