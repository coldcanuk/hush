# PLAN: Deepseek API provider + compact robot-edit buttons (RDAP)

Branch: `gb/deepseek-robot-btns`
Worktree: `worktrees/deepseek-robot-btns`
Base: `main` `d6d7f20be`

## 1. Methodology

RDAP. Four-minds gate is recorded in
[`../research/RESEARCH_DEEPSEEK_ROBOT_BTNS.md`](../research/RESEARCH_DEEPSEEK_ROBOT_BTNS.md).
Commit after every Milestone on this branch. Land only via PR.
Never write `main`.

## 2. Scope

Locked in the research file.

**Primary Goal**

1. Edit-robot footer is one compact line: [Save Robot] [Close]
   [Delete Robot] (Raise uses [Raise Robot] for the primary).
2. Deepseek API is a first-class SaaS radio (`deepseek-api`) in
   the existing HTTP API family.

**Non-Goals**

Live `/chat/completions`, prefix/FIM/JSON/Responses clients,
Anthropic-compat second host, new modules, shrinking hive
Close/Exit.

**Success Criteria**

Research architecture lock + unit/shell tests +
`./configure && make && make test` + PR merge + worktree removed.

**Constraints**

- C11, write-legible-c §14, `fn ≤ 40`, depth ≤ 2, ≤ 4 params.
- Prime Directive worktree. Embed UI after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo`.
- Secrets only in `pass`. Overlay never stores key material.

**Assumptions**

- `/v1/models` works for scan (both paths 401 without a key).
- Compact drawer actions are the requested exception to 44px.
- HTTP API robots stay labels (no child process).

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. Scan path mismatch → type-in still works.
2. Count enums drift → bump both + assert 9.
3. Launch grep for old Delete copy → update check.
4. `.btn.danger` used elsewhere → scope CSS to the drawer row.
5. Overlay/session size → one extra four-key object.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `d6d7f20be`, worktree
      `/opt/repo/hush/worktrees/deepseek-robot-btns` on
      `gb/deepseek-robot-btns`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/deepseek-robot-btns`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: four-minds evidence + Deepseek contract +
      provider/button inventory in
      `docs/research/RESEARCH_DEEPSEEK_ROBOT_BTNS.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on `gb/deepseek-robot-btns`.
- Commit: `Milestone 1.1: research + frozen plan for Deepseek and robot buttons`

### Phase 2 — Architecture (docs / contracts)

#### M2.1 UI_SPEC + README

- [ ] Task 1 of M2.1: In `UI_SPEC.md` bump version to
      `2026-08-19 (RDAP M2, gb/deepseek-robot-btns)`. Add
      `deepseek-api` to the eight-id list (now nine). Move
      Deepseek into the HTTP API family table. Delete
      "Deepseek is not a radio this slice." Document the
      compact `#agent-drawer .actions` row and short labels
      Raise Robot / Save Robot / Close / Delete Robot.
- [ ] Task 2 of M2.1: In `README.md` provider paragraph, add
      Deepseek next to Gemini / xAI / OpenAI / Anthropic.
- Verify: `rg -n 'deepseek-api' UI_SPEC.md README.md` and
      `rg -n 'not a radio this slice' UI_SPEC.md` is empty.
- Commit: `Milestone 2.1: UI_SPEC and README name Deepseek and compact actions`

### Phase 3 — Provider id

#### M3.1 Headers + tables

- [ ] Task 1 of M3.1: `hush_provider.h` — `HUSH_PROVIDER_COUNT = 9`,
      `#define HUSH_PROVIDER_HOST_DEEPSEEK "https://api.deepseek.com"`,
      comment "nine named runtimes".
- [ ] Task 2 of M3.1: `hush_roster.h` — `#define HUSH_ROSTER_PROVIDER_DEEPSEEK "deepseek-api"`,
      comment "nine named runtimes".
- [ ] Task 3 of M3.1: `hush_roster.c` — `HUSH_ROSTER_PROVIDER_COUNT = 9`
      and append `HUSH_ROSTER_PROVIDER_DEEPSEEK` to the table.
- [ ] Task 4 of M3.1: `hush_provider.c` — append meta row
      `{ HUSH_ROSTER_PROVIDER_DEEPSEEK, "Deepseek API", HUSH_PROVIDER_FAMILY_API, HUSH_PROVIDER_HOST_DEEPSEEK, "" }`.
      No new scan dialect (reuse Bearer `/v1/models`).
- Verify: `rg -n 'HUSH_PROVIDER_COUNT = 9|HUSH_ROSTER_PROVIDER_COUNT = 9|deepseek-api' hush-c/include hush-c/src`.
- Commit: `Milestone 3.1: deepseek-api is a named HTTP API runtime`

### Phase 4 — UI

#### M4.1 Compact actions + ninth radio

- [ ] Task 1 of M4.1: `hush-c/demo/index.html` CSS — add
      `#agent-drawer .actions { flex-wrap: nowrap; gap: 6px; margin-top: 12px; }`
      and `#agent-drawer .actions .btn { flex: 1 1 0; padding: 6px 8px; font-size: 0.78rem; min-height: 36px; }`
      plus `#agent-drawer .actions .btn.danger { width: auto; margin-top: 0; }`.
      Leave global `.btn.danger` for other drawers.
- [ ] Task 2 of M4.1: Move `#agent-delete` inside `.actions` after Close.
      Labels: `Raise Robot`, `Close`, `Delete Robot`.
- [ ] Task 3 of M4.1: JS copy — `resetAgentDraft` / `openAgentDrawer`
      use `Raise Robot` / `Save Robot`. Confirm stays
      `Delete this robot?`.
- [ ] Task 4 of M4.1: Add radio
      `<label><input type="radio" name="agent-provider" value="deepseek-api"> Deepseek API</label>`
      and `PROVIDERS["deepseek-api"] = "Deepseek API"`.
- [ ] Task 5 of M4.1: `./scripts/embed-ui.sh hush-c/demo`
- Verify: `rg -n 'deepseek-api|Save Robot|Delete Robot' hush-c/demo/index.html`
      and the three buttons sit inside one `.actions` div.
- Commit: `Milestone 4.1: Deepseek radio and one-line robot actions`

### Phase 5 — Tests

#### M5.1 Unit + shell

- [ ] Task 1 of M5.1: `test_provider.c` — `hush_provider_is_id("deepseek-api")`
      true; default host `HUSH_PROVIDER_HOST_DEEPSEEK`; family `api`;
      `n == HUSH_PROVIDER_COUNT` labeled `"nine"`.
- [ ] Task 2 of M5.1: `test_roster.c` — `hush_roster_is_provider("deepseek-api")`.
- [ ] Task 3 of M5.1: `check_provider.sh` — GET includes `"deepseek-api"`.
      Optional: POST save host/model for `deepseek-api` without
      leaking the key.
- [ ] Task 4 of M5.1: `check_launch.sh` — grep `deepseek-api`,
      `Save Robot` or `Raise Robot`, `Delete Robot`; keep
      `Delete this robot?` confirm if still present. Drop the
      old `Delete this robot` button grep if the label changed.
- [ ] Task 5 of M5.1: `cd hush-c && make test` (or repo
      `./configure && make && make test`).
- Commit: `Milestone 5.1: Deepseek and compact-action tests`

### Phase 6 — Verify, PR, cleanup

#### M6.1 Full suite + land

- [ ] Task 1 of M6.1: `./configure && make && make test` from the
      worktree. Apply write-legible-c §14 to the C diff.
- [ ] Task 2 of M6.1:
      `git add . && git commit -m "Complete: Deepseek API radio + compact robot-edit actions"`
      if anything remains; then `git push -u origin HEAD`.
- [ ] Task 3 of M6.1:
      `gh pr create --base main --head gb/deepseek-robot-btns`
      then `gh pr merge --auto --merge` (or `gh pr merge --merge`
      if auto-merge is still disallowed).
- [ ] Task 4 of M6.1: After MERGED, from `/opt/repo/hush`:
      `git checkout main && git pull --ff-only origin main`
      `git worktree remove worktrees/deepseek-robot-btns`
      `git branch -d gb/deepseek-robot-btns`
      `git push origin --delete gb/deepseek-robot-btns`
- Verify: `git worktree list` has no deepseek tree; `git status`
      on main is clean.
- Commit: (on the feature branch before merge only)

## 4. Audit of this plan

- Every Task names its Milestone, has a command or snippet, and
  a verify step.
- Phase 1 last task is the research → plan gate (this commit).
- Worktree path is `/opt/repo/hush/worktrees/deepseek-robot-btns`
  (Prime Directive, not `../gb-*-wt`).
- Land is PR-only. No local merge to `main`.
- Tasks are small: docs, then two count tables, then HTML, then
  tests, then PR.

Plan is frozen for execution.
