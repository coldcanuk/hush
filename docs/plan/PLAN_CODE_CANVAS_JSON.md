# PLAN: JSON C0 escape, code canvas, Buzz origin note (RDAP)

Branch: `gb/code-canvas-json`
Worktree: `worktrees/code-canvas-json`
Base: `main` `711c1f788`

## 1. Methodology

RDAP. Four-minds gate is recorded in
[`../research/RESEARCH_CODE_CANVAS_JSON.md`](../research/RESEARCH_CODE_CANVAS_JSON.md).
Commit after every Milestone on this branch. Land only via PR.
Never write `main`.

## 2. Scope

Locked in the research file.

**Primary Goal**

1. `/api/events` is legal JSON when a note contains TAB / CR / other C0.
   The thinking chip can clear. Happy’s weekday reply becomes visible.
2. Fenced code in notes paints as a code block. Large / multi-file
   replies open a right-hand canvas (download, selector, save to a
   recorded project).
3. README states Hush began as a Buzz fork and does not sync Buzz.

**Non-Goals**

Monaco, raising 4096, Grok timeout/turns, GitHub unfork click,
streaming, new highlighter library.

**Success Criteria**

Research architecture lock + `./configure && make && make test` +
PR merge + worktree removed.

**Constraints**

- C11, write-legible-c §14, `fn ≤ 40`, depth ≤ 2, ≤ 4 params.
- Prime Directive worktree. Embed UI after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo`.
- Canvas writes stay inside a recorded project path.

**Assumptions**

- Fence at line start is enough.
- Operator will detach the GitHub parent if the Sync button must go.
- One project (or the first listed) is the save target when several
  exist.

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. Escape room `o + 2` truncates `\u00HH` → guard `o + 6`.
2. Canvas POST as arbitrary write → resolve under project path only.
3. Fence misfire → line-start fences only.
4. GitHub Sync remains until the operator unforks.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `711c1f788`, worktree
      `/opt/repo/hush/worktrees/code-canvas-json` on
      `gb/code-canvas-json`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/code-canvas-json`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: four-minds evidence + Bayes in
      `RESEARCH_CODE_CANVAS_JSON.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on `gb/code-canvas-json`.
- Commit: `Milestone 1.1: research + frozen plan for JSON canvas`

### Phase 2 — Architecture

#### M2.1 UI_SPEC + README

- Task 1 of M2.1: UI_SPEC Data/API: `hush_json_escape` emits `\t`
  `\r` `\n` `\u00HH` for C0. `POST /api/canvas` `{project, path,
  content}` writes inside that project directory.
- Task 2 of M2.1: UI_SPEC §13 addendum: fenced code → `.code-block`;
  `#code-canvas` pane; file selector; download; save to project.
- Task 3 of M2.1: README origin paragraph (Buzz historical; no sync).
- Verify: `rg -n "code-canvas|\\\\t|block/buzz" UI_SPEC.md README.md`.
- Commit: `Milestone 2.1: spec JSON escape, canvas, Buzz origin`

### Phase 3 — JSON escape (the hang)

#### M3.1 C0 escape

- Task 1 of M3.1: `hush_json_escape` maps TAB/CR/LF and other C0 to
  legal JSON. Guard needs 6 bytes for `\u00HH`. Named constants.
- Task 2 of M3.1: same mapping in `hush_launch_json_escape`.
- Task 3 of M3.1: `check_agent.sh` (or a focused shell check) POSTs a
  note whose content contains a TAB; `GET /api/events` must be
  `python3 -c json.loads` clean and contain `\\t`.
- Verify: `make -C hush-c` compiles. §14 on touched C.
- Commit: `Milestone 3.1: escape C0 in JSON event bodies`

### Phase 4 — UI canvas

#### M4.1 fences + pane + POST

- Task 1 of M4.1: CSS `#code-canvas`, `.code-block`, selector, wrap.
- Task 2 of M4.1: `paintNote` splits fences into `<pre><code>`;
  Download / Canvas actions; `#code-canvas` editor + selector.
- Task 3 of M4.1: `POST /api/canvas` in `hush_http.c` writes only
  under a launch project path.
- Task 4 of M4.1: `./scripts/embed-ui.sh hush-c/demo`.
- Verify: `rg -n "code-canvas|code-block|api/canvas" hush-c/demo/index.html hush-c/src/hush_http.c`.
- Commit: `Milestone 4.1: fenced code, canvas pane, project save`

### Phase 5 — Tests + docs

#### M5.1 Checks

- Task 1 of M5.1: `check_launch.sh` greps `code-canvas`, `code-block`,
  `api/canvas`.
- Task 2 of M5.1: `./configure && make && make test`.
- Verify: ALL TESTS PASSED.
- Commit: `Milestone 5.1: tests for canvas and JSON tab`

### Phase 6 — Land

#### M6.1 PR + cleanup

- Task 1 of M6.1: push, `gh pr create --base main --head gb/code-canvas-json`.
- Task 2 of M6.1: `gh pr merge --auto --merge`. Wait MERGED.
- Task 3 of M6.1: on main checkout, `git pull --ff-only`,
  `git worktree remove worktrees/code-canvas-json`, delete branch.
- Verify: `git worktree list` is only main; `git status` clean.
- Commit: none on main.

## 4. Plan audit

- Every Task names its Milestone, has a command or file, and a verify.
- Phase 1 research → plan-update gate is this commit.
- Worktree path is inside `/opt/repo/hush/worktrees/`.
- Land is PR, not local merge.
- Tasks are atomic enough for one commit per Milestone.

Frozen for execution.
