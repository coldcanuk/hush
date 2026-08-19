# PLAN: conversation intelligence + channel policy (RDAP)

Branch: `gb/conv-intel-policy`
Worktree: `worktrees/conv-intel-policy`
Base: `main` `e8afb87f8`

## 1. Methodology

RDAP. Four-minds gate is recorded in
[`../research/RESEARCH_CONV_INTEL_POLICY.md`](../research/RESEARCH_CONV_INTEL_POLICY.md).
Commit after every Milestone on this branch. Land only via PR.
Never write `main`.

## 2. Scope

Locked in the research file.

**Primary Goal**

1. Scenario catalog is the product contract.
2. Manage Channel Policy leash persists and is honored before any
   Grok child starts.
3. Chatty / multi-note human bursts coalesce; confirm when needed.
4. Humans-only rooms stay quiet. Robot-only rooms are explicit.

**Non-Goals**

NLP classifier, live-child cancel, voice barge-in, nested NIP-10,
Codex/Goose live CLIs, robot-originated roots, renaming starter
channels.

**Success Criteria**

Research architecture lock + unit/shell tests +
`./configure && make && make test` + PR merge + worktree removed.

**Constraints**

- C11, write-legible-c §14, `fn ≤ 40`, depth ≤ 2, ≤ 4 params.
- Prime Directive worktree. Embed UI after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo`.
- Named constants for burst / hops / cues / job caps.

**Assumptions**

- English cue list is enough.
- 1s poll granularity is acceptable for `burst_ms`.
- Empty roster + `kind=open` remains whole-hive.

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. Confirm notes re-enter consider → `t`=`hush-confirm`.
2. `hush_launch.c` bloat → dedicated policy leaves.
3. Session JSON overflow → six short fields.
4. Burst vs poll → second-resolution flush.
5. Thread composer re-mention → stop auto-pills on follow-up.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `e8afb87f8`, worktree
      `/opt/repo/hush/worktrees/conv-intel-policy` on
      `gb/conv-intel-policy`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/conv-intel-policy`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: four-minds evidence, scenario catalog, Bayes in
      `RESEARCH_CONV_INTEL_POLICY.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on `gb/conv-intel-policy`.
- Commit: `Milestone 1.1: research + frozen plan for conv intel policy`

### Phase 2 — Architecture

#### M2.1 UI_SPEC + README contracts

- [ ] Task 1 of M2.1: add UI_SPEC §20 Channel policy + conversation
      intelligence (kind, robot_reply, burst, confirm, Manage Channel
      Policy block, `t=hush-confirm`).
- [ ] Task 2 of M2.1: extend Data/API `POST /api/channel` manage
      fields and session `channels[]` policy keys.
- [ ] Task 3 of M2.1: README one-paragraph: Manage Channel leashes
      robots; chatty bursts confirm.
- Verify: `rg -n "robot_reply|burst_ms|hush-confirm" UI_SPEC.md README.md`
- Commit: `Milestone 2.1: spec channel policy and burst confirm`

#### M2.2 C headers

- [ ] Task 1 of M2.2: extend `hush_launch_channel_t` with named
      policy fields + `hush_launch_set_channel_policy`.
- [ ] Task 2 of M2.2: add `hush-c/include/hush_intel.h`
      (`consider`, `poll`, `init`).
- Verify: headers compile via later make; prototypes have contracts.
- Commit: `Milestone 2.2: launch policy + hush_intel headers`

### Phase 3 — Persistence + HTTP

#### M3.1 vibe.json + session JSON

- [ ] Task 1 of M3.1: persist / restore policy keys next to roster
      lists (`channel_kind`, `channel_robot_reply`, …).
- [ ] Task 2 of M3.1: emit policy on session `channels[]`.
- [ ] Task 3 of M3.1: extend `test_launch.c` for save/restore.
- Verify: `./tests/test_launch` (via `make -C hush-c test` later).
- Commit: `Milestone 3.1: persist channel policy`

#### M3.2 manage HTTP

- [ ] Task 1 of M3.2: `hush_http_channel_manage` reads policy fields
      and calls `hush_launch_set_channel_policy`.
- Verify: later `check_launch.sh` POST manage with policy.
- Commit: `Milestone 3.2: HTTP manage writes policy`

### Phase 4 — Intelligence

#### M4.1 hush_intel module

- [ ] Task 1 of M4.1: implement `hush_intel.c` (hold table, cue
      match, recap note with `t=hush-confirm`, hop/cooldown/max_jobs,
      forward to `hush_agent_consider`).
- [ ] Task 2 of M4.1: `hush_http_serve_post` calls `hush_intel_consider`;
      relay pump calls `hush_intel_poll`.
- [ ] Task 3 of M4.1: `tests/test_intel.c` for silent / hold / confirm
      / off / hop-0.
- Verify: `./hush-c/tests/test_intel` + `make test`.
- Commit: `Milestone 4.1: hush_intel burst and leash`

#### M4.2 thread composer mention hygiene

- [ ] Task 1 of M4.2: thread follow-up posts only new pills +
      explicit `@`, not every member robot.
- Verify: `rg` in `index.html` that follow-up no longer maps the
      whole member set to `mention_N` by default.
- Commit: `Milestone 4.2: thread follow-up is mention-gated`

### Phase 5 — Manage Channel UI

#### M5.1 Policy block

- [ ] Task 1 of M5.1: Policy radios + advanced `<details>` in
      `#manage-chan`. Save posts the six fields.
- [ ] Task 2 of M5.1: `./scripts/embed-ui.sh hush-c/demo`
- [ ] Task 3 of M5.1: `check_launch.sh` greps `#manage-policy`,
      `robot_reply`, `burst_ms`.
- Verify: `make test`.
- Commit: `Milestone 5.1: Manage Channel policy UI`

### Phase 6 — Verify, PR, cleanup

#### M6.1 full suite

- [ ] Task 1 of M6.1: `./configure && make && make test`
- [ ] Task 2 of M6.1: write-legible-c §14 on every touched `.c/.h`
- Verify: ALL TESTS PASSED.

#### M6.2 land

- [ ] Task 1 of M6.2: `git push -u origin HEAD`
- [ ] Task 2 of M6.2: `gh pr create --base main --head gb/conv-intel-policy`
- [ ] Task 3 of M6.2: `gh pr merge --auto --merge` and wait
- [ ] Task 4 of M6.2: remove worktree after MERGED
- Commit (if needed): `Complete: conversation intelligence + channel policy`

## 4. Audit of this plan

- Every Task names its Milestone, has a command or file, and a
  verify step.
- Phase 1 synthesis gate is this commit.
- Worktree lifecycle matches PRIME_DIRECTIVE (PR, not local merge).
- Tasks are atomic; one commit per Milestone.

Plan frozen for execution.
