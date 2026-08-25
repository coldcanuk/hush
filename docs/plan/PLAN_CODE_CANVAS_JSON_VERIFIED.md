# PLAN_CODE_CANVAS_JSON — VERIFIED (M0–M5)

Worktree: `worktrees/code-canvas-json`
Branch: `gb/code-canvas-json`
Base: `main` `089dfa012` (post #80)

## Methodology
RDAP verification gate. Features pre-landed on base from prior slices. This worktree:
- Fresh worktree per PRIME_DIRECTIVE
- Research/audit gate (CURRENT.md)
- Explicit DoD greps, `make test`, check_*.sh
- No new C/UI changes required (verification + hygiene)
- Commits per milestone on gb/*
- Land via PR + auto-merge + cleanup

## M0.1 Isolation — COMPLETED
- Worktree created from clean main on gb/code-canvas-json
- `pwd` = /opt/repo/hush/worktrees/code-canvas-json
- Branch: gb/code-canvas-json
- Base commit: 089dfa012
- Verify: `git worktree list`, `git branch --show-current`

## M1.1 Research gate + CURRENT.md — COMPLETED
- `docs/research/RESEARCH_CODE_CANVAS_JSON_CURRENT.md` written with four-minds summary, evidence, Bayes, commands
- Plan frozen in `docs/plan/PLAN_CODE_CANVAS_JSON.md`
- Verify: files exist, content includes "H1 (illegal C0...)", "make -C hush-c test", "rg ... code-canvas"

## M2.1 UI_SPEC + README — COMPLETED
- README: "Hush began as a fork of [Buzz]... We do not track, fetch, or sync Buzz."
- UI_SPEC.md: documents `POST /api/canvas`, `.code-block`, `#code-canvas` pane, selector, save contract
- hush_json_escape contract (C0 → \t \r \n \u00HH) described
- Verify: `rg -n "began as a fork" README.md`, `rg -n "POST /api/canvas" UI_SPEC.md`, `rg -n "code-block" UI_SPEC.md`

## M3.1 C0 escape — COMPLETED
- `hush-c/src/hush_json.c`: hush_json_is_ctrl, hush_json_put_u (6-byte \u00HH), hush_json_put_byte handles \t \r \n + other C0
- `hush_launch_json_escape` delegates to `hush_json_escape`
- `hush-c/tests/test_json.c`: ctrl test expects "a\\tb\\r\\u0001"
- Verify: `make -C hush-c` compiles with -Werror; `rg "put_u|is_ctrl" hush-c/src/hush_json.c`; test_json "ctrl len"

## M4.1 Fences + pane + POST — COMPLETED
- HTML: `#code-canvas`, `.code-block[data-lang]`, splitFences, paintNote, canvas-file selector, POST /api/canvas
- C: `hush_http_serve_canvas` (project-scoped via hush_http_find_project + canvas_join/write; refuses ../)
- `/api/canvas` route registered
- Verify: `rg -n 'id="code-canvas"' hush-c/demo/index.html`; `rg -n "serve_canvas" --type c`; `rg -n "/api/canvas" hush-c/demo/index.html`; check_launch greps

## M5.1 Checks — COMPLETED
- `./configure && make clean && make && make -C hush-c test` → ALL TESTS PASSED (test_json ctrl, launch canvas POST, etc.)
- `sh hush-c/tests/check_launch.sh` passes with greps:
  - 'id="code-canvas"'
  - 'code-block'
  - '/api/canvas'
  - 'splitFences'
  - 'canvas-file'
- Live canvas POST test in check (project "alpha", path write, .. refused)
- Verify: "ALL TESTS PASSED", "launch routes ok"

## M6.1 PR + cleanup — PENDING (next)
- Push gb/code-canvas-json
- `gh pr create --base main --head gb/code-canvas-json --title "PLAN_CODE_CANVAS_JSON: JSON C0 escape, code canvas, Buzz origin note" --body "..."` 
- `gh pr merge --auto --merge`
- Post-merge: git checkout main && git pull --ff-only && git worktree remove worktrees/code-canvas-json && git branch -d gb/code-canvas-json && git push origin --delete gb/code-canvas-json
- Verify: only main worktree; clean status; no gb/* left

## Commands executed (verification)
- git checkout main && git pull --ff-only && git worktree add -b gb/code-canvas-json worktrees/code-canvas-json
- ./configure && make clean && make && make test
- sh hush-c/tests/check_launch.sh
- rg/grep for all DoD strings (canvas ids, escape fns, README Buzz, UI_SPEC)
- python/json.loads TAB-in-event test harness (strict JSON + \t evidence)
- make -C hush-c test (ALL PASS multiple times)

## Conclusion
M0.1–M5.1 verified on base + this gate.
All DoD greps, builds, and tests pass.
Ready for M6.1 commits + PR lifecycle.
M2.1 verified on base: UI_SPEC canvas contract + README Buzz origin sentence present and grepped.
M3.1 C0 escape verified on base: hush_json_escape covers TAB/CR/other C0 with 6-byte guard; hush_launch delegates; test_json ctrl case passes; make compiles -Werror.
