# PLAN — WS5 M2: thread memory (complete leave→return journey)

Base: `origin/main` tip `33331a65` (#198). M1 (`f459773e`, PR #180) owns the
durable rolling brief on the existing `hush_thread` path. This plan extends
that plumbing into a demoable journey. No parallel scaffolding.

Status: CoS GO is live. Cloud-VM only. Draft PR to main, no merge.

## 1. OBSERVED gap (verified in this tree)

- `hush_thread.c` persists every kind-1 note to
  `$HUSH_HOME/threads/<root>.log` plus a rolling `<root>.brief`. The agent
  prompt backfills from it (M1). OBSERVED in source.
- The thread pane (`hush-c/demo/index.html` `paintThreadStream`) renders
  only from in-memory `lastEvents` (`GET /api/events`, ring cap 1024) and
  returns silently when the root is absent. OBSERVED.
- There is NO `GET /api/thread` route (grep: no hits). A thread whose root
  left the ring (eviction, restart with a full ring, prune) cannot be
  reopened, and the pane never shows the durable brief/turns. The UI has no
  memory affordance at all. OBSERVED.
- `openThread` lives only in page JS state; a page reload forgets which
  thread was open. OBSERVED.

## 2. Outcome

Open a thread, leave (close pane / reload page / restart relay), return, and
see real restored context with honest labels. No localStorage content
phantoms: the browser may remember only the root id; every rendered turn and
every label must come from a relay response.

## 3. Milestones (commit per M, small atomic)

- M1: server `GET /api/thread?root=<64hex>` serving `{ok,root,brief,count,
  truncated,turns[]}` from the M1 transcript+brief files. Pure formatter in
  `hush_thread.c` (`hush_thread_format_json`, unit-tested) + thin handler in
  `api_status.c` + route in `hush_http.c`. write-legible-c §14 on all C.
- M2: pane merges durable turns under an honest `Thread memory · Saved on
  this relay · N turns` line (brief excerpt included). Evicted-root threads
  open from durable data with `Saved thread` chrome. Field-office chrome
  untouched (no new layers; Escape stack unchanged).
- M3: remember only the open root id (`localStorage.hush-thread-open`); on
  boot, revalidate via `/api/thread` and reopen when the relay confirms
  turns. Stale/unknown ids clear silently — never paint remembered content.
- M4: `UI_SPEC.md` delta + proof notes (curl transcript + restart demo).
  Draft PR, CI `build-test`.

## 4. Out of scope

Honesty-label Armory polish, PE-3/PE-4, federation/TLS/multi-human,
rewrites, merge.
