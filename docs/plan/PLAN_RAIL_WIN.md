# PLAN — Minimize, frameless --app, rail v2

Methodology: **RDAP** (Research-Driven Adaptive Planning).
Research lock: `docs/research/RESEARCH_RAIL_WIN.md`.
Worktree: `/opt/repo/hush/worktrees/rail-win`
Branch: `gb/rail-win`
Base: `main` `6227ad7db`

## Scope

**In.** Real WM Minimize / Maximize via `POST /api/window` and
optional X11 (`hush_win.c`). Launch Chromium-family `--app` with
`--ozone-platform=x11`. Motif-undecorate the window so the OS `×`
bar is gone. Rail v2 mock-up (Invite, Add Channel, New Robot, New
Project + `i` popovers). Delete left-nav Create.

**Out.** Wayland foreign-toplevel. `display: fullscreen` kiosk.
window-controls-overlay. Changing Close/Exit. Live hive restart.
GitHub unfork.

## Primary objective

Minimize iconifies the `--app` window. Maximize toggles WM
maximized. The window has no OS title bar. The rail matches the
new mock-up. Create is gone from the left nav.

## Success / DoD

- `#rail-min` POSTs `/api/window` `{action:"minimize"}`. No
  `railParked` early return. No `setRailCollapsed` as the verb.
- `#rail-max` POSTs `/api/window` `{action:"maximize"}`. Not the
  Fullscreen API as the primary path.
- `hush_exec_app_browser` argv contains `--ozone-platform=x11`.
- Manifest stays `"display": "standalone"` (not fullscreen).
- Served HTML has `#invite-human` on the rail, `#add-chan`,
  `#raise-agent`, `#add-proj`, `#chan-info`, `#robot-info`,
  `#proj-info`, `#invite-info`. No `<div class="create">`.
- `make -C hush-c test` → ALL TESTS PASSED.
- Landed via PR, not a local merge to `main`.

## Constraints

- Prime Directive: worktree `gb/rail-win` only; PR to `main`.
- C11 + write-legible-c on any `.c`/`.h`.
- X11 is optional (`HUSH_HAVE_X11`). No hard build dep.
- Fitts 44 px remains on `#install` and `#rail-toggle` only.
- Do not add libwayland.

## Assumptions

- A6: ozone-x11 maps via XWayland on COSMIC. Residual until
  operator restart.
- A7: COSMIC honors Motif decorations=0. Residual until restart.
- Drawers for invite / raise stay; only the nav Create block
  moves.

## Risks

1. COSMIC ignores Motif on XWayland → `×` remains. Mitigation:
   still ship ozone-x11 + Motif; residual is documented.
2. ozone-x11 fails to map → `/api/window` returns ok:false.
   Stubs already do that without X11.
3. `check_launch.sh` greps "Raise a robot" / "Invite human" —
   keep those strings on drawers.
4. `check_pwa.sh` still greps `blank-btn` — drop that grep.

## Phase 0 — Isolation

### M0.1 Worktree

- [x] Task 1 of M0.1: `git worktree add -b gb/rail-win worktrees/rail-win` from clean main `6227ad7db`.
- Verify: `git branch --show-current` → `gb/rail-win`.

## Phase 1 — Research

### M1.1 Research lock

- [x] Task 1 of M1.1: write `docs/research/RESEARCH_RAIL_WIN.md`.
- [x] Task 2 of M1.1: write this plan; commit both.

```
git add docs/research/RESEARCH_RAIL_WIN.md docs/plan/PLAN_RAIL_WIN.md
git commit -m "Milestone 1.1: research lock minimize + frameless + rail v2"
```

Verify: files exist under `docs/`.

## Phase 2 — Define

### M2.1 Spec + README

- [x] Task 1 of M2.1: update `UI_SPEC.md` §4 sidebar, §10 chrome
      note, §15 rail mock-up, API table `POST /api/window`.
- [x] Task 2 of M2.1: update `README.md` rail paragraph and
      `--open` chrome sentence (frameless, not "windowless PWA").
- Commit: `Milestone 2.1: spec rail v2 and frameless --app`.

## Phase 3 — Implement

### M3.1 Rail markup + Create gone

- [x] Task 1 of M3.1: replace `#tool-rail` body with v2 mock-up;
      delete `<div class="create">`; keep hidden `#new-chan` /
      `#new-proj` inside popovers; rewire `#invite-human`,
      `#add-chan`, `#raise-agent`, `#add-proj` (ids stay).
- [x] Task 2 of M3.1: `#rail-min` / `#rail-max` POST `/api/window`.
- Commit: `Milestone 3.1: rail v2 and window POST from UI`.

### M3.2 hush_win + HTTP + launch

- [x] Task 1 of M3.2: add `hush_win.h` / `hush_win.c` (optional
      X11). Public minimize / maximize / undecorate.
- [x] Task 2 of M3.2: `POST /api/window` in `hush_http.c`.
      `hush_exec_app_browser` adds `--ozone-platform=x11`.
      `hush_relay_watch_app` calls `hush_win_undecorate` on latch.
- [x] Task 3 of M3.2: `configure` probes `X11/Xlib.h`; writes
      `HUSH_HAVE_X11` + `-lX11` into `config.mk` when present.
      `hush-c/Makefile` honors it.
- Commit: `Milestone 3.2: X11 window controls and ozone-x11 launch`.

## Phase 4 — Verify

### M4.1 Tests

- [x] Task 1 of M4.1: `check_pwa.sh` greps `/api/window`,
      `ozone-platform=x11` is in `hush_relay.c`, no
      `class="create"`, `id="invite-info"`. Drop `blank-btn`.
- [x] Task 2 of M4.1: `make -C hush-c test`.
- Commit: `Milestone 4.1: window and rail v2 checks`.

## Phase 5 — Land

### M5.1 PR

- [ ] Task 1 of M5.1: push `gb/rail-win`, `gh pr create --base main`.
- [ ] Task 2 of M5.1: merge (auto if allowed, else `--merge`).
- [ ] Task 3 of M5.1: pull main, remove worktree, delete branch.

## Audit

Every task has a command or snippet and a verify. Research gate is
M1.1. Worktree lifecycle is M0.1 + M5.1. Tasks are atomic.

Frozen for execution after M1.1 commit.
