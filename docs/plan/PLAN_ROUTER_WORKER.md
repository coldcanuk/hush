# PLAN: Hive-wide Router and silent Workers

Branch: `gb/router-worker`
Base: `main` @ `b5a3383`

Status: the graph, the silent worker, and `/api/router` are in `hush-c`.
`/api/event` is unchanged, so an `@` mention or `nostr:` token still opens the
old thread. A restart reloads `$HUSH_HOME/router/`. The poll loop drains one
queued worker per turn and does not start a thread. `agent_prompt.c` still
injects every equipped skill into a legacy job; `hush_router_select_skills`
is the Phase 4 split and is what a Worker prompt must call.

## 0. What is true on `main` today

Checked against this head, not against a wish:

- One binary. The server is a single-threaded `poll(2)` loop (`hush_relay.c`).
- A human talks in a channel. `@`-mentioning a robot opens a thread. That
  thread is a public surface. Milestones live under `$HUSH_HOME/threads`
  (`hush_thread.c`, `agent_thread.c`).
- Jobs run from `agent_dispatch.c`, `agent_loop.c`, and `hush_agent.c`. A job
  can post back into the thread the human is watching.
- Major is the chief of staff. A `team` fence still waits for the owner's Yes.
- Equipped skills are injected on every job by `agent_prompt.c`. The Armory
  labels `ALWAYS-ON` and `ON-CALL` are browser-only. The relay does not
  honor them.
- Named loadouts already exist (`hush_favorite.c`, `POST /api/loadout`).
- Channel policy (who may speak, when a robot may spend, burst and cooldown)
  is the leash. It stays the leash.

## 1. Goal

One continuous agent for the human. The human talks only to the Router, in
the channel they already have open. Workers do the jobs and stay silent.
The channel never grows a public robot thread for the new path.

Constraints that do not move:

- Stay a self-hosted single-binary C11 program. Strict build stays
  `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`.
- Field-office desk, skill inventory, dolls, loadouts, and human gates stay.
- Ship after every phase. Each phase is reversible.
- Phases 1–3 add no thread and no process. Workers are non-blocking jobs
  whose status is queued and drained by the existing `poll(2)` loop.
- No second Router and no Watchdog before Phase 5.

## 2. Mapping

| Concept | On the relay | Does not |
|---|---|---|
| Router | One hive-wide role. Major, unless a dedicated persona is configured later. | Execute heavy tools. Post as a specialist. |
| Worker | The existing job, made silent. Own job directory. | Post to a channel. See the full graph. |
| Trigger | Channel message, first. | Fast-path non-chat events before Phase 5. |
| State | JSON files under `$HUSH_HOME/router/`. | A new database. |
| Specialist | Today's robot, read as a loadout. | A chat peer you open a thread with. |
| Watchdog | Deferred. | Anything in Phases 1–4. |

The Router owns human messages and events, holds a bounded subgraph, and
posts status. Workers report only to the Router.

## 3. Files

One JSON array per node kind. `hush_json_read.c` already reads this shape.
Writes are atomic (temp file in the same directory, then rename).

```
$HUSH_HOME/router/
  workitems.json
  workers.json
  decisions.json
  artifacts.json
  edges.json
  router_state.json
```

Work item: `id`, `title`, `description`, `status`
(`pending` / `running` / `blocked` / `done` / `failed`), `channel_id`,
`created`, `updated`, `assigned_worker_id`, `priority`, optional `parent_id`.

Worker: `id`, `workitem_id`, `specialist_id`, `status`, `started`,
`last_heartbeat`, `job_dir`, `provider_id`. Heartbeats start in Phase 3.

Decision: the human's channel reply, the work item it was matched to, and
the text forwarded to that worker.

Artifact: something a worker produced (path or short record), linked from
the worker, never posted as a thread.

Edge: `{from, type, to}`. Types used: `assigned`, `depends_on`,
`blocked_by`, `produced`, `belongs_to`, `needs`.

`router_state.json` stores the last subgraph the Router actually loaded,
not a copy of the whole hive.

The Router prompt is built from the subgraph only: active work items, recent
decisions, linked artifacts. Hard cap in Phase 3: 24 nodes. Workers never
receive the graph.

`$HUSH_HOME/threads` and milestones stay where they are. A one-time
`hush-relay --import-threads` copies them into work items. After import the
old files are read-only. The tool does not run itself.

## 4. Human surface

The human posts an ordinary channel message. It goes to the Router.

The Router answers in that same channel, at once: a direct answer, or one
status line (`Starting Work Item #47: rate-limit test.`). Later lines stay
in that channel and carry the id: `[WI-47] Working…`, `[WI-47] Blocked —
reply with 60s or 300s`.

A decision is an ordinary reply. The Router matches it to the open work item
and forwards it. The human does not pick a thread.

Above the dispatch log, a strip: title, status, age, specialist. Opening a
row shows a read-only drawer (last five status lines and artifacts). The
drawer has no composer.

The channel header carries one context line: `3 active · 1 blocked`.

## 5. Desk

- The Router is the only desk officer in the channel.
- Inventory chips show Idle, Assigned, or Blocked. Opening a robot opens the
  doll or the current assignment. It does not start a conversation.
- The doll (`i`) remains the skill editor. Phase 4 is when `ALWAYS-ON` is
  injected into the Router and `ON-CALL` is injected only into the assigned
  Worker. Until that phase the labels stay what they are today.
- The character sheet (`c`) becomes a capacity board: active workers, Router
  context size, equipped specialists.
- A named loadout can say "prefer this specialist." That hint is data the
  Router may read. It is not a command that skips the leash.
- The dispatch log is the work-item event feed. The volume dial still owns it.
- Old `@`-mention threads keep working until Phase 2's exit criteria are met.
  A dual-mode flag is the switch. New-path work does not open a public robot
  thread.

## 6. Phases

### Phase 1 — Router owns the ask

The Router persona receives channel messages. "This needs work" starts an
isolated job that does not post. The Router posts one status line. Old
threads still work.

Exit: a request produces a status line and no public robot thread.

### Phase 2 — Work items, async status

Work item records exist. Jobs report only to the Router. The Router posts
updates. The channel accepts new messages while a job is in flight. The
strip is on the desk. A reply updates the work item it was matched to, not
whichever job finished last.

Exit: two concurrent work items; a decision hits the right worker; the
channel does not block; old threads still optional.

### Phase 3 — Graph, restart, chips

The six files in §3 are the source of truth. Restart reloads active work
items and edges. The Router prompt uses the subgraph, capped at 24 nodes.
`--import-threads` exists and is not silent. Inventory chips follow
assignment.

Exit: kill and restart; active work and edges return; the prompt is the
subgraph and nothing else.

### Phase 4 — Skill tiers are real

`agent_prompt.c` stops injecting every equipped body into every job.
`ALWAYS-ON` goes to the Router. `ON-CALL` goes only to the Worker that is
running. Inventory, doll, and character sheet match that. Presets and the
prefer-specialist hint are stored with the existing loadout mechanism.

Exit: an always-on skill is in the Router context and absent from the Worker
prompt; an on-call skill is the reverse; chips move while a job runs.

### Phase 5 — Later

Fast-path events that are not chat may create work items. A lightweight
watchdog notices a worker with no heartbeat. A second Router is allowed
only after one context is shown to be too small. Not in this plan's build.

## 7. Migration

Robots and loadouts become specialist profiles with no rewrite. Dual-mode
holds through Phase 2. Import is explicit and one-time. Channel policy still
bounds the Router: a leash that would have stopped a robot stops the Router
the same way.

## 8. Risks

| Risk | Mitigation |
|---|---|
| The `poll(2)` loop stalls | Workers are jobs. Status is queued. No new thread until Phase 5. |
| Context grows without bound | Subgraph load. 24-node cap in Phase 3. |
| The desk regresses | Dual-mode through Phase 2. Status lines are visually distinct. Inventory stays the inventory. |
| Restart loses the desk | The graph files are the source of truth. Atomic rename. Heartbeats in Phase 3. |
| Scope creeps into a second product | Exit criteria above are the gate. No multi-Router and no event bus before Phase 5. |

## 9. How to know a phase is done

- Phase 1: request, status line, no public thread.
- Phase 2: two work items at once; the reply hits the blocked one; typing
  still works.
- Phase 3: process dies; active items and edges come back; prompt bytes are
  the subgraph.
- Phase 4: always-on text is in the Router prompt only; on-call text is in
  the Worker prompt only; chips update without a reload.
- Done, overall: the human has one agent, concurrent work is visible without
  thread management, and the skills already equipped still exist.

## 10. What this branch actually changes

- New module `hush_router` plus `GET`/`POST /api/router`.
- `hush_agent_poll` drains one queued worker when `$HUSH_HOME/router` exists.
- No change to `agent_prompt.c`. Legacy jobs still receive every equipped skill.
- No change to `/api/event`. A mention still opens a public thread.
