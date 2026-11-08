# Triage — the 21 open issues against Router/Worker

Date: 2026-11-08. Triage of every open issue in `coldcanuk/hush` against the
Router/Worker methodology that landed in `84463a97` (V2 M1, channels-only chat) and
the plan in `docs/plan/PLAN_ROUTER_WORKER.md`.

The question each issue answers: **does the Router/Worker change make this moot,
change what it means, or leave it untouched?**

Verdicts: **MOOT** (the change removed the thing it fixes), **REFRAME** (still real,
but the fix belongs in the new model), **SURVIVES** (unaffected), **BLOCKED**
(cannot start until another stack lands).

## Summary

| Verdict | Count | Issues |
|---|---|---|
| MOOT | 1 | #331 (partly) |
| REFRAME | 3 | #374, #360, #337 |
| SURVIVES | 16 | #372, #371, #369, #367, #362, #359, #358, #354, #353, #352, #351, #350, #349, #348, #347, #346, #209 |
| BLOCKED | 3 | #362, #360, #358 (all wait on the #336 stack) |

Net: **one issue is partly moot, three need rewriting, sixteen stand as written.**
The packaging batch (#346–#354) is untouched and is the largest single block of work.

## MOOT

### #331 — Follow-ups: Gauge P3s from #323 (Cline person gate)

**Verdict: partly moot.** P3-4 is explicitly about the old surface:

> `agent_approve.c:149-153`: "owner_asked treats a robot note in an owner-opened
> **thread** as the owner's ask"

V2 M1 removed the thread UI, so the robot-note-in-a-thread path it describes no
longer exists as a human surface. P3-1, P3-2, P3-3, P3-5 are about `agent_process.c`,
`agent_team.c`, `agent_approve.c` and `test_cline_gate.c` and are **unaffected**.

Action: close P3-4 with a note pointing at `84463a97`; keep the rest open.

## REFRAME

### #374 — Router/Workers plan: schema-evolution question

**Verdict: reframe — and it is the most important issue on the board.**

It asks a direct question the plan does not answer:

> the six files under `$HUSH_HOME/router/` are versionless JSON with atomic rename,
> and the plan states no evolution story. `workers.json` already gains fields
> mid-plan (`last_heartbeat` in Phase 3, `provider_id`). When two branches extend the
> store concurrently — or a field changes type under existing files — what's the
> approach: tolerant readers with defaults, a `schema_version` per file, or an
> explicit one-time migrator?

This is not a bug; it is a **missing decision in the methodology**. Every other
router issue depends on the answer, because they all write to those six files.

There is a branch `gb/schema-evolution` with a design in
`workdirs/superagent-evolution/design.md` (206 lines) but **no code** — the diff is
docs only. So the decision is drafted and unimplemented.

Action: decide the evolution story, implement it in `hush_router.c`, close #374.
Until then the router store is a one-way door.

### #360 — agent workers and providers inherit relay sockets (no CLOEXEC)

**Verdict: reframe.** The mechanism is unchanged, but the Router/Worker model makes
it worse, not better: workers are now the *only* thing that runs jobs, so every
worker inherits the relay's sockets and a `POST /api/event` connection stays open
until the provider exits. Under the old model a leaked socket died with the thread;
under the new model it dies with the work item, which may be long-lived.

Action: keep P2, but restate it in worker terms and raise it in the queue — a
long-lived worker holding a relay socket is a resource leak with a longer tail.

### #337 — Persist approval-held turns across relay restart

**Verdict: reframe.** The plan defers the Watchdog to Phase 5 and says nothing about
what happens to an approval-held turn when the relay restarts. Under Router/Worker a
held approval is a `blocked` work item, and `workitems.json` already has a `blocked`
status — so the fix is to persist the held turn *as a work item*, not to add a new
mechanism.

Action: rewrite the acceptance criteria in work-item terms.

## SURVIVES

Sixteen issues are untouched by the change. They fall into four groups.

**Packaging batch (#346, #347, #348, #349, #350, #351, #352, #353, #354, #369)** —
ten issues, one batch. #353 is **P1 security** (deb/rpm ship `hush-turn` with a
public replace-me TURN credential). #369 says *"fold into the packaging batch
(#346-354). Not a fix PR on its own."* This is the largest coherent block of work on
the board and it has nothing to do with Router/Worker.

**Sandbox and jail (#372, #367)** — Landlock/bwrap behaviour in `hush_sandbox.c`.
Owned by Floor. Unaffected.

**Test flakes (#359, #358)** — `check_loop.sh` s14 and the `id1-chrome` races.
Unaffected.

**Protocol and platform (#362, #209)** — JOIN token decode, and `--quit` process
identity on BSD. Unaffected.

## BLOCKED

Three issues say so themselves and must not be started:

| Issue | Blocker |
|---|---|
| #362 | *"no fix PR until the #336 stack lands"* |
| #360 | *"no fix PR until the #336 stack lands"* |
| #358 | *"no fix PR until the #336 stack lands"* |

The #336 stack is the gate. Any plan that assigns these before #336 lands will
produce work that cannot merge.

## What this means for the bots

The board is not one queue. It is four:

1. **The methodology decision (#374)** — blocks every router issue. One decision, no
   code until it is made.
2. **The packaging batch (#346–#354, #369)** — ten issues, one owner, no dependency
   on the router. This is where a worker can produce merged code today.
3. **The #336-gated three (#362, #360, #358)** — do not start.
4. **The rest (#372, #367, #359, #209, #331)** — independent, small, assignable.

A wake that assigns "the next issue" is therefore wrong. It must assign from a
specific queue, and it must know that queue 3 is closed.
