# PLAN — Canvas Fill-in-the-Middle (Tab completion)

Methodology: **RDAP** (Research-Driven Adaptive Planning).
Research lock: `docs/research/RESEARCH_CANVAS_FIM.md`.
Worktree: `/opt/repo/hush/worktrees/canvas-fim`
Branch: `gb/canvas-fim`
Base: `main` `f9e3b581e`

## Scope

**In.** Tab FIM on the canvas. Frontend debounce / pulse / ghost /
Tab. New `hush_canvas.c` + `hush_canvas.h`. Non-blocking
`POST /api/complete` + `GET /api/complete?t=`. Pump calls
`hush_canvas_poll`. Tests + UI_SPEC + README.

**Out.** In-process libcurl / `-lcurl` / pthreads. Live DeepSeek
or OpenAI FIM. Replacing Ctrl+K. Raising content bound. Streaming.
Live hive restart. GitHub unfork.

## Primary objective

Pause in the canvas (~300 ms). A theme-colored pulse sits at the
caret. A dim ghost appears. Tab inserts it. The hive poll loop
never sleeps on that request.

## Success / DoD

- Served HTML contains `activePrediction`, `predictionPos`,
  `CANVAS_FIM_MS`, `tok-ghost`, `fim-caret`, `/api/complete`.
- `#code-canvas-edit` keydown Tab inserts `activePrediction` when
  set and does not `preventDefault` when empty.
- Pulse CSS uses `var(--accent)` and `var(--accent-dim)`.
- `hush_canvas.h` exposes init / shutdown / poll / start / take.
- `POST /api/complete` returns a token without waiting on grok.
- `GET /api/complete?t=` returns pending, text, or error.
- No `#include <curl/curl.h>`. No `-lcurl`. No `pthread`.
- `make -C hush-c test` → ALL TESTS PASSED.
- Landed via PR, not a local merge to `main`.

## Constraints

- Prime Directive: worktree `gb/canvas-fim` only; PR to `main`.
- C11 + write-legible-c on every `.c`/`.h`.
- `HUSH_AGENT_JOBS_MAX` stays 4. Canvas owns one separate slot.
- `HUSH_EVENT_MAX_CONTENT` stays 4096. Ghost max 512.
- No CDN highlighter. Ctrl+K / `/api/fixup` unchanged.

## Assumptions

- A7: tests fake `grok` the same way `check_fixup.sh` does.
- A6: no live FIM key required.
- Overlay caret span is close enough (A8).

## Risks

1. grok ignores suffix → prompt + fake-grok tests.
2. GET poll forgotten → check_complete.sh covers both verbs.
3. Overlay wrap drift → visual residual only.

## Phase 0 — Isolation

### M0.1 Worktree

- [x] Task 1 of M0.1: `git worktree add -b gb/canvas-fim worktrees/canvas-fim` from clean main `f9e3b581e`.
- Verify: `git branch --show-current` → `gb/canvas-fim`.

## Phase 1 — Research

### M1.1 Research lock

- [x] Task 1 of M1.1: write `docs/research/RESEARCH_CANVAS_FIM.md`.
- [x] Task 2 of M1.1: write this plan; commit both.

```
git add docs/research/RESEARCH_CANVAS_FIM.md docs/plan/PLAN_CANVAS_FIM.md
git commit -m "Milestone 1.1: research lock canvas FIM"
```

- Verify: both files exist on `gb/canvas-fim`.

## Phase 2 — Define

### M2.1 Spec + README

- [x] Task 1 of M2.1: UI_SPEC §13 + API table: Tab FIM, pulse,
      ghost, `/api/complete` start+take. Version line `gb/canvas-fim`.
- [x] Task 2 of M2.1: README canvas paragraph: Tab completes a
      ghost; does not mention libcurl.
- Verify: `rg -n 'api/complete|tok-ghost|CANVAS_FIM' UI_SPEC.md README.md`.
- Commit: `Milestone 2.1: spec canvas FIM`

## Phase 3 — Implementation

### M3.1 Frontend

- [x] Task 1 of M3.1: CSS `.fim-caret` pulse (`--accent` /
      `--accent-dim`) and `.tok-ghost` (`--faint`).
- [x] Task 2 of M3.1: state, 300 ms debounce, prefix/suffix,
      start+poll `/api/complete`, paint ghost + caret, Tab/Esc.
- Verify: `rg -n 'activePrediction|predictionPos|CANVAS_FIM_MS|tok-ghost|fim-caret|/api/complete' hush-c/demo/index.html`.
- Commit: `Milestone 3.1: canvas FIM ghost and Tab`

### M3.2 hush_canvas + HTTP

- [x] Task 1 of M3.2: `hush_canvas.h` + `hush_canvas.c` (one
      slot, fork grok FIM prompt, poll, take ≤512).
- [x] Task 2 of M3.2: `hush_http` POST start / GET take. No
      nanosleep. Pump calls `hush_canvas_poll`. Init/shutdown
      next to agent.
- Verify: `rg -n 'hush_canvas_start|hush_canvas_poll|/api/complete' hush-c/src hush-c/include`.
- Verify: `rg -n 'curl/curl.h|-lcurl|pthread' hush-c` is empty for new hits.
- Commit: `Milestone 3.2: hush_canvas complete API`

## Phase 4 — Tests

### M4.1 Checks

- [x] Task 1 of M4.1: `check_pwa.sh` greps `tok-ghost`,
      `fim-caret`, `/api/complete`.
- [x] Task 2 of M4.1: `check_complete.sh` — fake grok prints
      `int x;`. POST returns token. GET eventually has that
      text. Event count unchanged. No `curl.h` in sources.
- Verify: `make -C hush-c test` → ALL TESTS PASSED.
- Commit: `Milestone 4.1: test canvas FIM`

## Phase 5 — Land

### M5.1 PR

- [ ] Task 1 of M5.1: push, `gh pr create --base main --head gb/canvas-fim`.
- [ ] Task 2 of M5.1: `gh pr merge --merge` (auto-merge is not
      enabled on this repo). Pull main. Remove worktree. Delete
      branch.
- Verify: `gh pr view` state MERGED. `git worktree list` has no
  `canvas-fim`. Main clean.

## Audit (before execute)

- Every task has a verify and a milestone id.
- Phase 1 ends with a synthesis gate (this file + RESEARCH).
- Worktree path is inside `/opt/repo/hush/worktrees/`.
- Land is PR-only.
- Tasks are one concern each.
- Plan frozen for execution.
