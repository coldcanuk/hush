# PLAN — Thread scroll, compose, canvas, rail

Methodology: **RDAP** (Research-Driven Adaptive Planning).
Research lock: `docs/research/RESEARCH_THREAD_UX.md`.
Worktree: `/opt/repo/hush/worktrees/thread-ux`
Branch: `gb/thread-ux`
Base: `main` `53c9779e1`

## Scope

**In.** Stop the thread pane snap-back (H1). Six-line wrapping
composer on hive and thread. In-page canvas highlighter for the
locked popular-language list (Go included). Canvas Ctrl+K via
`POST /api/fixup` (no hive note). Compact two-column tool rail with
`i` install popover; Install stays first.

**Out.** Live hive restart. GitHub unfork. Highlight.js / Prism /
CDN. Changing `#stream` pinning. Real OS window minimize. Wiring
Blank. Raising content bound. Streaming. Agent joke hygiene.

## Primary objective

A human can scroll a live thread upward and it stays put. A reply
wraps to six lines then scrolls. Canvas colors Go (and the locked
list). Ctrl+K rewrites a selection. The rail matches the mock-up at
normal button size.

## Success / DoD

- `openThreadPane` / `paintThreadStream` does not assign `scrollTop`
  unless the user was within `THREAD_PIN_PX` of the bottom or the
  pane just opened.
- `#msg` and `#thread-msg` are `<textarea rows="6">`.
- Served HTML contains `code-canvas-hi`, language `golang` / `go`
  keyword paint, `#canvas-k`, `/api/fixup`, `id="rail-info"`,
  `id="blank-btn"`, `id="rail-min"`, `id="rail-max"`.
- `make -C hush-c test` → ALL TESTS PASSED.
- Landed via PR, not a local merge to `main`.

## Constraints

- Prime Directive: worktree `gb/thread-ux` only; PR to `main`.
- C11 + write-legible-c on any `.c`/`.h`.
- No new highlighter dependency.
- Fitts 44 px remains on `#install` and `#rail-toggle` only.
- `HUSH_AGENT_JOBS_MAX` stays 4. Fixup uses a job flavor, not a
  fifth slot table.

## Assumptions

- Minimize = collapse rail. Maximize = Fullscreen API.
- Blank = disabled reserved slot.
- Ctrl+K waits on one grok child; timeout is `HUSH_AGENT_TIMEOUT_S`.

## Risks

1. Waiting on grok from the HTTP thread blocks poll — mitigate with
   a short sleep loop that still calls `hush_agent_poll`.
2. Highlight overlay desyncs from textarea scroll — bind `scroll`.
3. Enter-to-send vs newline — Shift+Enter newline, document in spec.
4. Fullscreen denied in some `--open` browsers — Maximize no-ops
   quietly.

## Phase 0 — Isolate

### M0.1 Worktree
- [x] Task 1 of M0.1: `gb/thread-ux` at `worktrees/thread-ux` from
      clean `main` `53c9779e1`.
- Verify: `git branch --show-current` → `gb/thread-ux`.

## Phase 1 — Research gate

### M1.1 Research
- [ ] Task 1 of M1.1: `docs/research/RESEARCH_THREAD_UX.md` (this
      slice). Four-minds, Bayes H1 0.950, architecture lock.
- [ ] Task 2 of M1.1: this plan file. Commit both.
- Verify: files exist under `docs/research` and `docs/plan`.
- Commit: `Milestone 1.1: research thread ux snap-back`

## Phase 2 — Define

### M2.1 Spec + README
- [ ] Task 1 of M2.1: `UI_SPEC.md` version line `gb/thread-ux`.
      §13 composer 6-line textarea; thread paint pin rule; canvas
      overlay highlighter + Ctrl+K / `/api/fixup`. §15 rail mock-up.
- [ ] Task 2 of M2.1: README one paragraph (composer, sticky
      thread scroll, canvas color + Ctrl+K, compact rail).
- Verify: `rg -n 'THREAD_PIN_PX|rows=\"6\"|/api/fixup|rail-info' UI_SPEC.md`.
- Commit: `Milestone 2.1: spec thread ux`

## Phase 3 — Implementation

### M3.1 Thread paint + composer
- [ ] Task 1 of M3.1: `paintThreadStream` + `THREAD_PIN_PX` in
      `hush-c/demo/index.html`. `render` must not force-pin.
- [ ] Task 2 of M3.1: `#msg` and `#thread-msg` → textarea rows=6,
      wrap, overflow-y auto. Enter submit, Shift+Enter newline.
      Mention handlers keep working.
- Verify: `rg -n 'THREAD_PIN_PX|rows=\"6\"' hush-c/demo/index.html`.
- Commit: `Milestone 3.1: sticky thread scroll and 6-line composer`

### M3.2 Canvas highlight + Ctrl+K UI
- [ ] Task 1 of M3.2: overlay `pre#code-canvas-hi`, language map,
      Go keywords, scroll sync.
- [ ] Task 2 of M3.2: `#canvas-k` popover, Ctrl/Cmd+K, Apply/Cancel.
      POST `/api/fixup`, replace selection.
- Verify: `rg -n 'code-canvas-hi|canvas-k|golang' hush-c/demo/index.html`.
- Commit: `Milestone 3.2: canvas highlight and ctrl-k popover`

### M3.3 Tool rail
- [ ] Task 1 of M3.3: markup + CSS per lock. Install stays first.
      `i` toggles help. Two-col grids. Blank disabled. Min/Max.
- Verify: `rg -n 'rail-info|blank-btn|rail-min|rail-max' hush-c/demo/index.html`.
- Commit: `Milestone 3.3: compact tool rail`

### M3.4 POST /api/fixup
- [ ] Task 1 of M3.4: `hush_agent` fixup flavor (no note insert,
      `--max-turns` `HUSH_AGENT_FIXUP_TURNS` `"1"`).
- [ ] Task 2 of M3.4: `hush_http_serve_fixup` + dispatch.
- Verify: `make -C hush-c` compiles. §14 checklist on touched C.
- Commit: `Milestone 3.4: api fixup without hive note`

## Phase 4 — Tests

### M4.1 PWA + HTTP greps
- [ ] Task 1 of M4.1: `check_pwa.sh` asserts composer textarea,
      `THREAD_PIN_PX`, `code-canvas-hi`, `canvas-k`, `rail-info`,
      `blank-btn`. New `check_fixup.sh` (or extend an existing
      check) POSTs `/api/fixup` against a fake grok and expects
      `{ok:true` and no new event count bump.
- Verify: `make -C hush-c test` → ALL TESTS PASSED.
- Commit: `Milestone 4.1: test thread ux and fixup`

## Phase 5 — Land

### M5.1 PR
- [ ] Task 1 of M5.1: push `gb/thread-ux`, PR → main, auto-merge,
      remove worktree after MERGED.
- Verify: `gh pr view` MERGED; main checkout clean.

## Audit (pre-exec)

- Research → plan-update gate is M1.1.
- Every task names its milestone, a command/snippet, and a verify.
- Worktree lifecycle matches Prime Directive (PR, not local merge).
- Tasks are atomic; one commit per milestone.
- H1 is the only incident; P1–P4 ride in the same slice because
  the operator bundled them.
