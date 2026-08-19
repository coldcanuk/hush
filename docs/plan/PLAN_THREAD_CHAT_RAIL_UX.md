# PLAN: thread chat UX + free tool rail (RDAP)

Branch: `gb/thread-chat-rail-ux`
Worktree: `worktrees/thread-chat-rail-ux`
Base: `main` `5aec1f621`

## 1. Methodology

RDAP. Four-minds gate is recorded in [`../research/RESEARCH_THREAD_CHAT_RAIL_UX.md`](../research/RESEARCH_THREAD_CHAT_RAIL_UX.md).
Commit after every Milestone on this branch. Land only via PR.
Never write `main`.

## 2. Scope

Locked in the research file.

**Primary Goal**

1. Happy (Grok Build) replies with a real note, not
   `At ease. Grok Build returned nothing. — Happy`.
2. Thread pane is a hive-consistent, resizable chat that supports
   1:1 and 1:n.
3. Tool rail keeps free drag. Dock squares are gone. Double-click
   hamburger (and an open thread) parks the collapsed rail to the
   left of `hush` / vibe name.

**Non-Goals**

Codex/Goose live CLIs, nested NIP-10 trees, extra grok stderr pipe,
Close/Exit changes, themes, tray.

**Success Criteria**

Research architecture lock + `./configure && make && make test` +
PR merge + worktree removed.

**Constraints**

- C11, write-legible-c §14, `fn ≤ 40`, depth ≤ 2, ≤ 4 params.
- Prime Directive worktree. Embed UI after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo`.
- Effort token is a named string constant.

**Assumptions**

- grok 1.0.4 accepts `low` (verified this session).
- Fake grok in tests ignores the effort value.

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. Effort enum drifts → test greps `low`.
2. Thread resize vs modal overlay → drop the dimmer.
3. 1:n double-dispatch → mention robots only.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `5aec1f621`, worktree
      `/opt/repo/hush/worktrees/thread-chat-rail-ux` on
      `gb/thread-chat-rail-ux`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/thread-chat-rail-ux`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: four-minds evidence + Bayes in
      `RESEARCH_THREAD_CHAT_RAIL_UX.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on `gb/thread-chat-rail-ux`.
- Commit: `Milestone 1.1: research + frozen plan for thread chat and rail`

### Phase 2 — Architecture

#### M2.1 UI_SPEC contracts

- Task 1 of M2.1: Update `UI_SPEC.md` §13 (resizable hive thread pane,
  1:1 and 1:n, composer pills, `--reasoning-effort low`). Rewrite §15
  (free drag, no docks, hamburger home left of brand, thread parks rail).
- Verify: `rg -n "placeRailAtBrand|reasoning-effort low|thread-resize|1:n" UI_SPEC.md`.
- Commit: `Milestone 2.1: UI_SPEC for thread chat and dockless rail`

### Phase 3 — Grok effort token

#### M3.1 low, not none

- Task 1 of M3.1: `#define HUSH_AGENT_GROK_EFFORT "low"` and use it
  in `hush_agent_exec_grok`. Keep every other argv flag.
- Task 2 of M3.1: `check_agent.sh` greps `HUSH_AGENT_GROK_EFFORT` and
  `"low"`. Keep the fake-grok joke path.
- Verify: `make -C hush-c` compiles. `rg -n 'HUSH_AGENT_GROK_EFFORT|"low"' hush-c/src/hush_agent.c`.
- Commit: `Milestone 3.1: grok reasoning effort low`

### Phase 4 — UI

#### M4.1 Dockless rail + brand home

- Task 1 of M4.1: Remove `#rail-docks` markup, CSS, and JS. Free drag
  stays. Persist `{x,y,collapsed}` only.
- Task 2 of M4.1: `placeRailAtBrand()`. Double-click `#rail-toggle`
  collapses and homes. Open thread forces collapsed home; close
  restores the pre-thread pose.
- Verify: `rg -n "placeRailAtBrand|dblclick" hush-c/demo/index.html` and
  `rg -n "rail-docks|railAnchor" hush-c/demo/index.html` is empty.
- Commit: `Milestone 4.1: dockless rail homes beside the brand`

#### M4.2 Resizable 1:1 / 1:n thread pane

- Task 1 of M4.2: CSS + markup — floating hive panel, resize handle,
  hive composer box + pills + mention box, participant line.
- Task 2 of M4.2: JS — persist size, paint 1:n members, thread
  composer mentions, reopen/close, Escape.
- Task 3 of M4.2: `./scripts/embed-ui.sh hush-c/demo`.
- Verify: `rg -n "thread-resize|thread-pills|thread-mention" hush-c/demo/index.html`.
- Commit: `Milestone 4.2: resizable 1:1 and 1:n thread chat`

### Phase 5 — Tests + docs

#### M5.1 Checks and copy

- Task 1 of M5.1: `check_launch.sh` drops dock greps; requires
  `placeRailAtBrand`, `dblclick`, `thread-resize`.
- Task 2 of M5.1: README / UI_SPEC already updated; one-line README
  if the rail sentence still says “snap”.
- Verify: `./configure && make && make test`.
- Commit: `Milestone 5.1: tests for thread chat and dockless rail`

### Final Phase — Land

#### M6.1 PR + cleanup

- Task 1 of M6.1: push, `gh pr create --base main --head gb/thread-chat-rail-ux`.
- Task 2 of M6.1: `gh pr merge --auto --merge`. After MERGED:
  pull main, `git worktree remove worktrees/thread-chat-rail-ux`,
  delete branch.
- Verify: `git worktree list` has no this slug; main clean.
- Commit: none on main.

## 4. Audit of this plan

- Every Task names its Milestone, has a command or snippet, and a verify.
- Phase 1 synthesis gate is this commit.
- Worktree lifecycle matches PRIME_DIRECTIVE (PR, not local merge).
- Tasks are atomic. Flag change is isolated from UI.

## 5. Execute

Follow strictly. Commit after every Milestone.
