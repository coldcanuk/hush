# PLAN — Review Hardening and Collaboration Features

**Status:** DRAFT — scope frozen; Phases 3+ are finalized at the Phase 1
synthesis gate (see `docs/research/RESEARCH_REVIEW_HARDENING.md`).

**Knowledge document:** `docs/research/REVIEW_HUSH_0.0.1.md` (external technical
review of Hush `0.0.1` @ `5f67c65eb`).

**Methodology:** RDAP — Double Diamond (discover → define → develop → deliver),
risk-driven research iterations, small atomic Milestones with a strict
Definition of Done.

**Branch:** `gb/review-hardening` · **Worktree:** `worktrees/review-hardening`

---

## 1. Scope of Work

### Primary goal

Turn the review's findings into verified code, in priority order: close the
missing trust boundary, make the relay core correct and fast enough to use
daily, and then deliver the three collaboration features the review names
(signed identity, durable thread memory, streaming/cancellable/budgeted turns
with a work ledger).

### Non-goals (this build)

- WebSocket transport. The relay keeps its newline JSON protocol; the plan
  makes it honest and authenticated rather than interoperable with stock Nostr
  clients.
- Relay-to-relay federation (the review's honorable mention).
- A SQLite dependency. Durable state must stay C11 + OpenSSL/libc, matching
  the existing `store.ring` / `wake.ledger` design.
- Rust, new languages, or a new web framework. The PWA stays vanilla JS served
  by the binary.
- Rewriting the conversation engine. `hush_intel.c` policy semantics are
  preserved except where the review found them unenforced.
- Multi-human identity/roster semantics beyond what signatures make possible.

### Success criteria (measurable)

1. **Trust boundary.** No `system()` call remains on any request path;
   `/api/project` uses `mkdir()` + `execvp`. The relay binds `127.0.0.1`
   by default; remote binding requires an explicit flag. `Access-Control-Allow-Origin: *`
   is gone. No `nsec` or TURN password is returned to an unauthenticated or
   non-loopback caller. `/api/*` requires a session token minted at first run.
2. **Event authentication.** `hush_event_t` carries `sig`; the line protocol
   verifies BIP-340 signatures and recomputes the event id before store/ack;
   invalid events get `["OK", id, false, "invalid: ..."]` and are not stored.
   BIP-340 official test vectors pass in a unit test.
3. **Relay correctness.** The two table-overflow clobbers and the
   `hush_send_str` partial-write bug are fixed with tests that fail before the
   fix. `cooldown_s`, `max_jobs`, and `robot_hops` are enforced as
   documented, or the documentation is corrected.
4. **Persistence performance.** Store inserts are append-only with a periodic
   snapshot; median insert latency at a 1,000-event ring is < 1 ms with
   persistence on (baseline to be re-measured), and `store.ring` stays valid
   across restart.
5. **Wire/documentation truth.** Filters honor `ids`/`since`/`until` and all
   authors/#h values; `created_at` and tags survive the line parser; README,
   `SECURITY.md`, and `NOSTR.md` describe what the code actually does.
6. **Durable thread memory.** A thread brief survives restart; API providers
   receive a real `messages[]` array; thread context is budget-driven rather
   than a fixed 6 × 384 B window.
7. **Streaming, cancel, budget.** API providers stream partial output to the
   PWA; `POST /api/cancel` stops a running job; token/cost is accounted per
   job/robot/thread and a per-thread budget is enforced by the leash; the
   signal ring drives a visible activity timeline.
8. **No regressions.** `make` is clean under
   `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`; all existing unit
   tests, `check_agent.sh`, and `check_collaboration.py` pass, plus new tests
   for every fix. Every milestone is committed on `gb/review-hardening`; the
   work lands on `main` only through PRs; the worktree is removed afterwards.

### Constraints

- C11 only; strict warning set; legible-C standard (`write-legible-c`) applies
  to every `.c`/`.h` touched.
- No new mandatory runtime dependency. OpenSSL (`-lcrypto`) is already linked
  and is the crypto toolbox.
- The single-threaded `poll(2)` loop stays; streaming must not require a
  thread-per-client or blocking reads.
- Existing data files (`store.ring`, `wake.ledger`, `vibe.json`) must be
  read compatibly or migrated with a documented path.
- Prime Directive: worktree → commits/pushes on `gb/*` → PR → auto-merge →
  delete worktree. Never touch `main` directly.

### Assumptions

- The review's claims are accurate until the Phase 1 verification says
  otherwise; every claim is checked against the code before a fix is written.
- The local machine has OpenSSL 3.x with secp256k1 group support and Python 3
  for the test harnesses.
- GUI-window tests (`check_win.py`, `check_browser_launch.py`) are verified
  manually or skipped with a recorded reason, as in the review.

### Required environment / tools

`gcc`, `make`, `./configure`, `openssl`, `python3`, `gh` (authenticated as
`coldcanuk`), `pass` (tests use `fake-pass.sh`).

### Top risks and mitigations

| # | Risk | Mitigation |
|---|------|------------|
| R1 | The session-token gate breaks the embedded PWA or the integration tests | Phase 1 inventory of every route + every test caller; token served same-origin; compatibility mode only for loopback; update tests in the same milestone |
| R2 | BIP-340 verification is expensive or subtly wrong | Use official test vectors as the gate; verification-only (no signing on the wire path); if OpenSSL BIGNUM proves too slow for the wire, vendor a small audited single-file verifier |
| R3 | The append-only store changes on-disk format and breaks restart memory | Keep a versioned record format; migration path from `store.ring`; restart tests in `check_collaboration.py` cover it |
| R4 | Streaming in a single-threaded poll loop blocks other clients | Non-blocking child stdout pipe + per-job output ring; only `/api/stream` reads it; CLI providers keep buffering until the streaming milestone |
| R5 | Scope creep across 256 goal rounds | Phases land as separate PRs; each phase has its own Definition of Done; the plan is re-frozen at the Phase 1 gate |

---

## 2. Phases (finalized at the Phase 1 synthesis gate)

The phase skeleton below is the working structure; task-level detail is
written into this file once the research notes land.

- **Phase 0 — Environment & isolation.** Worktree, baseline build/test. (done)
- **Phase 1 — Research & discovery.** Four evidence notes, then synthesis and
  the updated plan. Ends with the mandatory synthesis task.
- **Phase 2 — Define / architecture.** Decisions record: token flow, BIP-340
  verify path, store log format, streaming transport, thread-memory model.
- **Phase 3 — Trust boundary.** Review §8 items 1–3, 5, 7 + hardening flags.
- **Phase 4 — Relay correctness.** Overflow clobbers, partial writes, reply
  cap, leash semantics, dead reset path, start-failure diagnostics.
- **Phase 5 — Persistence.** Append-only log + snapshot; measure the write
  path before/after.
- **Phase 6 — Wire fidelity.** Tags, timestamps, filter completeness; docs
  truth (README/SECURITY/NOSTR).
- **Phase 7 — Feature 1: signed identity.** BIP-340 verification, `sig` in
  the event struct, id recompute, NIP-42-style challenge.
- **Phase 8 — Feature 2: thread memory.** Durable thread store, rolling
  brief, `messages[]` for API providers, budget-driven context, durable robot
  context.
- **Phase 9 — Feature 3: streaming/cancel/budget/ledger.** SSE or NDJSON
  endpoint, cancel endpoint, accounting + budget enforcement, activity
  timeline from `hush_cevent`.
- **Final phase — Verification, polish, integration, cleanup.** Full suite,
  docs, PRs merged, worktree removed, "Vibe Code Build complete."
