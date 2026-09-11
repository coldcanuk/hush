# PLAN — Review Hardening and Collaboration Features

**Status: FROZEN after the Phase 1 synthesis gate. Progress: Phases 0–4 done;
Phase 5 next.** Scope, decisions, and success criteria below are authoritative
for the rest of this build.

- **Knowledge document:** [docs/research/REVIEW_HUSH_0.0.1.md](../research/REVIEW_HUSH_0.0.1.md)
- **Phase 1 synthesis:** [docs/research/RESEARCH_REVIEW_HARDENING.md](../research/RESEARCH_REVIEW_HARDENING.md)
- **Landed:** PR #152 (Phases 0–3) merged; `gb/relay-correctness` carries Phase 4.
- **Branch/worktree:** phases use `gb/<phase-slug>` in `worktrees/<phase-slug>`
- **Methodology:** RDAP (Double Diamond, risk-driven research iterations, small
  atomic Milestones with a Definition of Done).

---

## 1. Scope of Work

### Primary goal

Turn the review into verified code, in priority order: close the missing trust
boundary, make the relay core correct and fast enough for daily use, then
deliver the three collaboration features the review names (signed identity,
durable thread memory, streaming/cancellable/budgeted turns with a work ledger).

### Non-goals

- WebSocket transport. The line protocol becomes honest and authenticated, not
  interoperable with stock Nostr clients.
- Relay-to-relay federation.
- A SQLite dependency; durable state stays C11 + OpenSSL + libc.
- Rust, a new web framework, or a rewrite of the conversation engine.

### Success criteria (measurable)

1. **Trust boundary.** No `system()` on any request path; loopback bind by
   default; `Access-Control-Allow-Origin: *` gone; no `nsec`/TURN password to
   an unauthenticated caller; `/api/*` token-gated except `/api/status` and
   `GET /api/complete?t=`.
2. **Event authentication.** `hush_event_t` carries `sig`; BIP-340 verified and
   ids recomputed on the wire path; invalid events answered
   `["OK", id, false, "invalid: ..."]` and not stored; all 19 official BIP-340
   vectors pass.
3. **Relay correctness.** The two table-overflow clobbers and the
   `hush_send_str` partial-write bug are fixed with regression tests;
   `cooldown_s`, `max_jobs`, and `robot_hops` are enforced as documented or
   the documentation is corrected.
4. **Persistence performance.** Append-only records + periodic snapshot; median
   insert latency < 1 ms with persistence on at a 1,000-event ring (baseline
   re-measured), `store.ring` still loads across restart.
5. **Wire/documentation truth.** Filters honor `ids`/`since`/`until` and all
   authors/`#h` values; `created_at` and tags survive the line parser; README,
   `SECURITY.md`, and `NOSTR.md` describe actual behavior.
6. **Durable thread memory.** A thread brief survives restart; API providers
   receive a real `messages[]` array; context selection is budget-driven.
7. **Streaming, cancel, budget.** API providers stream partial output to the
   PWA; `POST /api/cancel` stops a job; usage/cost is accounted per job, robot,
   and thread; the leash enforces a per-thread budget; the signal ring drives a
   visible activity timeline.
8. **No regressions.** Clean build under the strict flag set; all unit tests,
   the non-GUI integration checks, and new tests pass; work lands on `main`
   only through PRs; worktrees are removed after merge.

### Constraints

- C11 only; `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`; the
  write-legible-c standard applies to every changed `.c`/`.h`.
- No new mandatory runtime dependency (OpenSSL `-lcrypto` is the toolbox).
- The single-threaded `poll(2)` loop stays; streaming must not block it.
- Existing data files (`store.ring`, `wake.ledger`, `vibe.json`) load or
  migrate with a documented path.
- Prime Directive: worktree → commits/pushes on `gb/*` → PR → auto-merge →
  delete worktree. Never touch `main` directly.

### Assumptions and environment

- The four Phase 1 notes are accurate for `5f67c65eb`; each task re-checks its
  own citations before editing.
- Tools: gcc, GNU make, `./configure`, OpenSSL 3.0.13 (secp256k1 via
  `EC_GROUP`), python3, curl, `gh` (authenticated as `coldcanuk`).
- GUI checks (`check_win.py`, `check_browser_launch.py`) are run only when a
  display is available and the operator accepts windows opening; otherwise the
  skip is recorded. `check_collaboration_ui.cjs` needs Playwright.

### Top risks

| # | Risk | Mitigation |
|---|------|------------|
| R1 | Token gate breaks the PWA or tests | Route inventory; same-origin cookie bootstrap; harness wrapper; browser flow check |
| R2 | BIP-340 verify wrong or slow | Official vectors first; verification-only; OpenSSL path measured at ~291 µs/op in a probe |
| R3 | Store format change breaks restart memory | Versioned records; read old `store.ring`; restart tests |
| R4 | Streaming blocks the poll loop | Non-blocking child stdout + per-job buffer; poll-set integration milestone |
| R5 | Scope across rounds | One PR per phase; explicit DoD; plan frozen here |

---

## 2. Phases, Milestones, Tasks

### Phase 0 — Environment and isolation  ✅ DONE

- **M0.1 Worktree + baseline.** `gb/review-hardening` in
  `worktrees/review-hardening`; baseline: clean build, 22/22 unit tests,
  `check_agent.sh`, `check_collaboration.py`.
  Verify: `git worktree list`; test logs.
- **M0.2 Knowledge document.** Land `docs/research/REVIEW_HUSH_0.0.1.md`.
  Commit `0bc0f34e9`.

### Phase 1 — Research and discovery  ✅ DONE

- **M1.1 Evidence notes.** Four notes (security surface 588 lines; relay
  correctness; event auth, incl. BIP-340 probe result; thread memory/streaming).
  Verify: each note's claim table is all "verified", "partially verified", or
  "refuted" with `path:line` evidence.
- **M1.2 Synthesis gate.** `RESEARCH_REVIEW_HARDENING.md` + this frozen plan.
  Verify: every Phase ≥ 3 task below cites a note or the review.

### Phase 2 — Define / architecture  ✅ DONE

- **M2.1 Decisions D1–D12** recorded in the synthesis (§3). No separate
  architecture document; each decision names its constraint and rationale.

### Phase 3 — Trust boundary  ✅ DONE (PR #152 merged)

- **M3.1 Shell sink.** ✅ Commit `ac16327f9`: `hush_launch_git_init` uses
  `mkdir` + `execvp("git")`; `hush_launch_validate_project_path` rejects
  relative/root/`..`/control-char paths; regression test proves a quote/semicolon
  path creates no file.
  Verify: `./tests/test_launch && grep -rn 'system(' hush-c/src` (no hits).
- **M3.2 Bind + CORS + Host.** `hush_relay_run(port, bind_addr, open_ui)`;
  `--listen ADDR` (default `127.0.0.1`, `0.0.0.0` for LAN); announce prints the
  real address; `hush_http_host_ok` rejects non-local `Host` in loopback mode;
  both `Access-Control-Allow-Origin` emitters deleted.
  Verify: `ss -ltn` shows `127.0.0.1`; `curl -H 'Host: evil' → 403`;
  `--listen 0.0.0.0` still serves the PWA.
- **M3.3 Session token.** `hush_auth.c/h`: 32-hex token in
  `$HUSH_HOME/session.token` (0600, `O_NOFOLLOW`), constant-time compare;
  `hush_http_guard` gates `/api/*` (exceptions: `/api/status`,
  `GET /api/complete`); loopback responses set `hush_session` cookie
  (`HttpOnly; SameSite=Strict`); carriers: cookie, `X-Hush-Token`,
  `Authorization: Bearer`, `?k=`. Test harness: hermetic `HUSH_HOME`,
  `XDG_RUNTIME_DIR`, `HUSH_PASS_HELPER` seam, token-aware `curl()`.
  Verify: `tests/test_auth`; manual 401/200 matrix; `check_launch.sh`,
  `check_turn.sh`, `check_provider.sh`, `check_agent.sh`,
  `check_collaboration.py`.
- **M3.4 Build hardening + default goal.** `configure` probes
  `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-fPIE`,
  `-Wl,-z,relro`, `-Wl,-z,now`, `-pie`; Makefile defaults add
  `-fstack-protector-strong -fPIE`; `.DEFAULT_GOAL := all` fixes incremental
  builds.
  Verify: `grep HARDEN` in configure output; `make` twice rebuilds
  correctly; `readelf -l hush-relay | grep GNU_RELRO`.
- **M3.5 Docs truth.** `SECURITY.md` rewritten for the token gate, durable
  store, membership semantics, input validation, hardening; README feature
  claims corrected (kinds, filters, private vibe, token); `NOSTR.md` banner.
  Verify: claims match `grep` results from the security note.
- **M3.6 PR 1.** Push, open PR, auto-merge, delete worktree, fast-forward
  `main`. Verify: `gh pr view` shows MERGED; `git worktree list` shows only
  the main checkout.

### Phase 4 — Relay correctness  ✅ DONE (PR 2)

- **M4.1 Overflow clobbers.** `hush_intel_take_hold` returns NULL when the
  table is full and `hush_agent_follow_take` reports FULL; callers deny/jam
  explicitly instead of folding into slot 0.
  Verify: new `test_intel` overflow case + a follow-table test at its boundary.
- **M4.2 Partial writes.** Per-client output buffer with `POLLOUT`, bounded
  queue, and drop-with-log on overflow; `hush_send_str` returns status.
  Verify: slow-reader case in `check_collaboration.py` gets complete frames.
- **M4.3 Reply size.** Raise/align the worker and reader reply caps so a
  4097-byte reply succeeds (or truncates with an explicit notice), removing the
  "not usable" failure.
  Verify: `check_collaboration.py` 4097 case expects a stored reply.
- **M4.4 Leash semantics.** ✅ Commit `6e712527e`. `cooldown_s` throttles
  robot-triggered chains only (human mentions and cues stay exempt so normal
  follow-ups are never blocked); `max_jobs` counts live jobs per channel;
  `robot_hops` is documented as the boolean gate it has always been.
  Verify: `test_intel` cooldown case; `check_agent.sh`; `check_collaboration.py`.
- **M4.5 Dead reset path.** ✅ Commits `372ffee28`. `hush_agent_consider` is
  gone; `hush_intel_consider` resets a completed follow slot for each human note.
- **M4.6 Start-failure diagnostics + inflight.** ✅ Commit `372ffee28`. A turn
  that cannot start posts a diagnostic note; only started jobs count toward a
  wave's `inflight`.
- **M4.7 POST /api/turn.** ✅ Commit `765117259`. Read-only routes require GET,
  so the POST handler is reachable; `check_turn.sh` proves the body is applied.
- **M4.8 Predictable dirs.** ✅ Commit `0ba8ded78`. New
  `hush_dir_ensure_private` (owner match, no symlink) guards TURN state and
  `turnserver.conf` (`O_NOFOLLOW`, 0600); agent and canvas cwd fall back to a
  per-process private directory. `tests/test_dir` covers file and symlink
  rejection. Deferred: the same treatment for `~/.hush`-rooted provider and
  pidfile directories, which are not shared/world-writable paths.
- **M4.9 Oversized line.** ✅ Commit `3fc4b946e`. An over-long line drains the
  peer, sends a `NOTICE`, and closes; raw-socket case in
  `check_collaboration.py`.
- **M4.10 PR 2.** Push, open PR, merge, delete the worktree.

### Phase 5 — Persistence performance  ✅ DONE (PR 3)

- **M5.1 Record format.** ✅ Every insert appends `u32 magic + event` to
  `$HUSH_HOME/store.log`; the reader treats a missing entry, a foreign magic,
  or a torn record as end-of-log. `test_store` covers replay and a torn tail.
- **M5.2 Append writer.** ✅ No snapshot per insert: one append plus a
  `fdatasync` at most once per second, and a snapshot every 256 inserts.
- **M5.3 Compaction.** ✅ `hush_store_compact` writes the existing snapshot
  format through temp + fsync + rename + dir fsync, then truncates the log;
  replay skips ids already in the snapshot, so an interrupted compaction cannot
  duplicate events. Old `store.ring` files load unchanged.
- **M5.4 Measurement.** ✅ `tests/test_store_bench` (also in the unit suite).
  Same harness, same `/tmp` filesystem, 1 KB events:
  **before** median 3.437 ms / p95 5.828 ms at 1,200 events (old full-snapshot
  writer); **after** median 0.022 ms / p95 0.032 ms, max ~6 ms at the
  256-insert snapshot, final compact ~9 ms. 4 KB events: 4.445 ms before,
  0.023 ms after. That is ~150x at 1,200 events, well past the <1 ms target.
  Restart memory is covered by `check_collaboration.py`.
- **M5.5 PR 3.** Push, open PR, merge, delete the worktree.

### Phase 6 — Wire fidelity and docs  ✅ DONE (PR 4)

- **M6.1 Parser preservation.** ✅ The line parser now runs on
  `hush_json_lookup`/`hush_json_decode` instead of `strstr`/`sscanf`; events keep
  `created_at` and tags, and every string is escape-decoded. `test_proto` covers
  a three-tag event with escaped content.
- **M6.2 Filters.** ✅ `kinds`, `ids`, `authors`, `since`, `until`, and the
  `#e`/`#p`/`#h`/`#d` tag filters (four values each) are parsed, and matching is
  generic over the tag key instead of only `#h`. `test_filter` covers AND
  semantics, inclusive ranges, ids/authors, and tag values.
- **M6.3 Kind semantics.** ✅ NIP-09 kind 5 deletions remove same-author
  `e`-tagged targets in one forward pass and are logged (but never stored) so
  replay re-applies them across a reload. `a` tags and reaction rendering stay
  documented omissions.
- **M6.4 NOSTR.md rewrite.** ✅ `NOSTR.md` now documents the real transport,
  frames, parsed fields, filter semantics, kind handling, limits, and the
  deliberately missing pieces.
- **M6.5 JSON-escape emitted frames.** ✅ EVENT/OK/EOSE writers escape every
  string through `hush_json_escape`, and emitted events carry `created_at` and
  `tags`; `test_proto` round-trips a tab/quote/backslash payload.
- **M6.6 PR 4.** Push, open PR, merge, delete the worktree.

### Phase 7 — Feature 1: signed identity (PR 5)

- **M7.1 `sig` + id recompute.** ✅ `hush_event_t` carries `sig`; the wire parser
  reads it and the serializer emits it; `hush_event_compute_id` now preserves
  interior empty tag elements (trailing empties are omitted, the one documented
  limit of the fixed tag layout).
- **M7.2 `hush_schnorr` verify.** ✅ `hush_schnorr.c` implements BIP-340
  verification on the already-linked OpenSSL BIGNUM/EC: x-only lift_x via
  even-y decompression, tagged SHA-256 challenge, `R = sG - eP`, parity and x
  checks, canonical range rejection. The 19 official vectors are vendored at
  `hush-c/tests/vectors/bip340_test_vectors.csv` and all pass in
  `test_schnorr`, including the four variable-length-message cases.
- **M7.3 Ingest gate.** ✅ `hush_event_verify` recomputes the id and verifies the
  signature; the relay answers `OK false` with `invalid: ...` and skips store,
  wake ingest, and fan-out. `test_event` covers accept, id mismatch, bad
  signature, and missing signature; `check_collaboration.py` proves a real
  signed frame is accepted and a tampered one rejected end to end.
- **M7.4 NIP-42.** ⏸ Deferred: no challenge/response, so a captured valid frame
  can be replayed. Documented in `NOSTR.md` and `SECURITY.md`; the remaining
  gap is tracked here for a later phase.

### Phase 8 — Feature 2: thread memory (PR 6)

- **M8.1 Durable transcript.** Per-root JSONL + index under `$HUSH_HOME`;
  survives restart and ring eviction.
- **M8.2 Rolling brief.** Summary event pinned to the root when the window
  exceeds its budget; loaded first on rebuild.
- **M8.3 `messages[]`.** API providers receive system + user/assistant turns;
  CLI providers keep the flattened prompt.
- **M8.4 Budget-driven context + durable robot context files.**
  Verify: restart test shows remembered decisions; provider mock asserts the
  array shape; context stays under the byte budget.

### Phase 9 — Feature 3: streaming, cancel, ledger (PR 7)

- **M9.1 `hush_sse` parser** + unit tests (pure, bounded).
- **M9.2 `stream:true` + `curl -N`** into the job pipe with non-streaming
  fallback.
- **M9.3 Job fds in the poll set** so partial output arrives without the 1 s
  tick; per-job bounded output ring.
- **M9.4 Reply delivery.** `GET /api/reply?root=…` long-poll/NDJSON for
  partial text.
- **M9.5 `POST /api/cancel`** with SIGTERM then SIGKILL to the process group;
  honest "stopped" note. Verify: cancellation test.
- **M9.6 Usage + budget.** Parse provider usage; count tokens/cost per job,
  robot, thread; leash denies over-budget dispatch.
- **M9.7 PWA + ledger.** Stream into the thread pane; activity timeline from
  `hush_cevent`; one run artifact per team run.
  Verify: `check_collaboration.py` streaming/cancel cases; UI check when
  Playwright is available.

### Final phase — Verification, polish, integration, cleanup

- Full clean `./configure && make`; all unit tests; all non-GUI checks plus
  GUI checks when a display is available; README/CHANGELOG updates; final PR
  merged; worktree removed; `main` clean; report **"Vibe Code Build complete."**

---

## 3. Plan audit (pre-execution)

- Every task names its milestone, its exact code/CLI surface, and a
  verification step. ✅
- The research → plan-update gate is the Phase 1 synthesis, which is committed
  before Phase 3 code. ✅
- Worktree lifecycle follows the Prime Directive at every milestone. ✅
- Tasks are single-purpose and small enough to verify independently. ✅
- Commit per milestone on `gb/review-hardening`; PR per phase. ✅
