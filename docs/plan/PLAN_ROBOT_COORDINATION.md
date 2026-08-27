# PLAN: Robot self-organization (leader election, division of labor, strict scoping)

Branch: `gb/robot-coordination`
Worktree: `worktrees/robot-coordination`
Base: `main` @ `c31bdb7a1`

## 1. Scope

### Primary goal

When a human tags N robots in one note, the relay must produce the *right*
coordination behavior instead of every robot running the same full ask:

- **Explicit delegation** (human names each robot *and* its job): each robot
  does **only** its own named sub-task.
- **Undirected broadcast** (human tags 2+ robots, no per-robot job): robots
  self-organize.
  - **1 robot** → solo.
  - **2 robots** → cooperate and divide labor between themselves (no leader).
  - **3+ robots** → elect a leader; the leader organizes the others and emits a
    division-of-labor plan.

### Non-goals

- No new persistence model, no UI redesign. Coordination is prompt + in-process
  dispatch only.
- No cross-channel coordination. Scope is one note → one thread.
- No full NLP dependency-graph scheduling. The leader picks serial-vs-parallel
  and an ordering policy; the relay executes it.

### Success criteria (measurable)

1. `@A @B` (broadcast, 2 robots) → both reply with **non-overlapping** parts.
2. `@A @B @C` (broadcast, 3 robots) → exactly one leader plan note is posted,
   then each of the other robots runs its **own** sub-task from that plan.
3. `@A tell a joke. @B was it funny?` (explicit) → A only tells a joke; B only
   answers "was it funny?"; neither does the other's part.
4. With `Major` present in a 3+ broadcast, Major is the leader.
5. `./configure && make && make test` green (all existing + new tests).

### Constraints

- C11, strict build: `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`.
- No Rust/Cargo. grok is invoked as a subprocess (`grok -p <note> ...`).
- Up to `HUSH_AGENT_JOBS_MAX` (4) concurrent jobs; follow queue up to
  `HUSH_AGENT_FOLLOW_ROBOTS` (8); co-robots per note capped at 4.
- Existing turn/hop caps (`max_robot_turns`, `robot_hops`) must still bound
  runaway conversation.

### Assumptions

- A robot is only "in the group" if it is tagged in the triggering note
  (p-tag set already computed as `co_npubs` + self). Q7 = (a).
- Leader election is **deterministic-first** (Major, else skill rank) with an
  LLM elect fallback. Q4.
- Explicit-vs-broadcast is **deterministic heuristic first, LLM fallback**. Q2.

### Top risks & mitigations

| Risk | Mitigation |
|------|-----------|
| LLM over-runs the scoping again (does the peer's job) | Each robot's `ask` is *replaced* by its own sub-task; prompt says "do only this assignment"; strict-scope rule added |
| Leader plan unparseable | Use a line-oriented fenced block (not free JSON); on parse failure fall back to sequential full-ask (current behavior) |
| Runaway planning / re-plan loops | Plan produced once per thread; existing turn/hop caps; plan tasks capped at co-robot count |
| Detection heuristic misclassifies | Deterministic fast path only for clear cases; ambiguous → LLM classifies |
| Parallel dispatch exceeds job slots | Cap concurrent starts at `HUSH_AGENT_JOBS_MAX`; overflow waits (FIFO) |

## 2. Design decisions (Phase 2)

### 2.1 Coordination modes

Computed from the robot hex list length `n` (self + co-robots):

- `n == 1` → **solo** (current behavior, no change).
- `n == 2` → **cooperate** (no leader).
- `n >= 3` → **orchestrate** (leader).

### 2.2 Explicit vs broadcast detection

Deterministic heuristic (C):

- Scan the human note's content for `nostr:<npub>` tokens.
- Count `assignments` = tokens that are followed (after optional whitespace)
  by a substantive clause before the next token or end-of-string.
- `assignments >= n` and `n >= 2` → **explicit**.
- `assignments <= 1` → **broadcast**.
- otherwise → **ambiguous** → LLM fallback (the would-be leader classifies via
  a one-line instruction).

`n == 1` always solo, so detection only runs for `n >= 2`.

### 2.3 Leader election (3+)

1. If `Major` (Payne) is in the group → leader = Major.
2. Else rank robots by count of equipped **leadership skills**; robots with
   ≥1 form the candidate pool; the leader-selection LLM pass elects among them.
3. No leadership-skilled robot → all robots are candidates; LLM elects
   (tiebreak = first mentioned).

Leadership skill set (const list, tunable):

```
system:hive-patterns, system:conflict-break, system:canvas-coach,
system:summary-handoff, system:job-cap
```

### 2.4 Leader planning pass (3+)

The leader runs a dedicated grok pass (new job kind) whose system prompt
instructs it to emit **one** fenced, line-oriented plan:

````
```plan
order: fifo
parallel: no
Happy: generate a riddle
Major: answer the riddle
```
````

- `order`: `fifo` | `lifo` (`filo`→`lifo`, `lilo`→`fifo`).
- `parallel`: `yes` | `no`.
- Task lines: `Name: task text` (one per non-leader robot).
- The plan note is posted visibly (renders as a code block via `splitFences`).
- The backend parses the fenced block into a task array.

Parse failure → fall back to current sequential full-ask dispatch.

### 2.5 Dispatch (per-robot sub-task + ordering)

- Extend the follow queue to carry **per-robot `ask`** + `order` + `parallel`
  (a small `hush_agent_plan_t`).
- serial + `fifo`: start tasks in listed order, each next after prior finishes.
- serial + `lifo`: reverse order.
- parallel: start tasks immediately, capped at `HUSH_AGENT_JOBS_MAX`; overflow
  queued FIFO.
- Each dispatched robot's `in.ask` is its sub-task, not the original human ask.

### 2.6 Strict scoping

- New prompt constant `HUSH_AGENT_STRICT_SCOPE`: "Do ONLY the assignment given
  to you. Do not perform, answer, or complete any part assigned to another
  robot."
- Injected for explicit-delegation robots and for every post-plan robot.

### 2.7 Two-robot cooperation (no leader)

- Both robots receive the full ask + `HUSH_AGENT_COOPERATE`: "You are a pair.
  Divide the labor between you two; each does a distinct, non-overlapping part.
  Do not duplicate your partner's part."
- The follow queue runs them sequentially; robot 2 sees robot 1's note in the
  thread transcript and does the complementary part.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (done in this worktree)

- [x] Worktree `gb/robot-coordination` from clean main.

### Phase 1 — Research

- [x] Read `hush_agent.c` dispatch/follow/job path, grok spawn, prompt macros.
- [x] Read skill catalog + roles (`hush_skill.h`, `hush_roster.h`).
- [x] Confirm `hush_json.h` is escape-only → plan uses a line format, not JSON.
- [ ] **M1.1** Synthesize findings into this plan + commit. (this doc)

### Phase 2 — Architecture

- [ ] **M2.1** Freeze the decisions in §2 and update the risk register if
  needed. (this doc, expanded during implementation)

### Phase 3 — Implementation

- [ ] **M3.1 — Strict scoping + mode detection**
  - Add `HUSH_AGENT_STRICT_SCOPE` / `HUSH_AGENT_COOPERATE` prompt constants.
  - Add `hush_agent_detect_mode()` (explicit/broadcast/ambiguous).
  - Explicit delegation: scope each robot's `ask` to its own clause.
  - Test: `@A tell a joke. @B was it funny?` → non-overlapping replies.
- [ ] **M3.2 — Two-robot cooperation**
  - Inject cooperation prompt; sequential complementary execution.
  - Test: `@A @B` broadcast → non-overlapping parts.
- [ ] **M3.3 — Leader election**
  - `hush_agent_elect_leader()`: Major → skill rank → LLM elect (fallback).
  - Add leadership-skill const list.
- [ ] **M3.4 — Leader plan + parse + ordered/parallel dispatch**
  - New plan job kind + leader system prompt.
  - `hush_agent_parse_plan()` (line format).
  - Extend follow queue for per-robot `ask` + `order` + `parallel`.
  - Test: 3-robot broadcast → one plan note + per-robot sub-tasks.
- [ ] **M3.5 — Docs + full test sweep**
  - Update `docs/ROBOT_TO_ROBOT.md`, `UI_SPEC.md` (plan rendering note).
  - `./configure && make && make test`.

### Final Phase — Verify, polish, integrate

- [ ] Full test suite + `check_agent.sh` additions.
- [ ] Final commit, push, PR, auto-merge, remove worktree, delete branch.

## 4. Open micro-decisions (defaulted; call out if wrong)

1. **LILO** treated as a synonym of FIFO; **FILO** treated as LIFO. (Only two
   distinct orderings exist.)
2. **Leadership skill set** = the five IDs in §2.3.
3. **Two robots including Major** still = cooperate (no leader); leader only
   at 3+, per Q1/Q8.
