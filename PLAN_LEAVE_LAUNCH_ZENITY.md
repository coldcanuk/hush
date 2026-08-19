# PLAN — do not raise leave zenity at launch

Branch: `gb/leave-launch-zenity`
Worktree: `worktrees/leave-launch-zenity`
Base: `main` `9cf2406a8`
Date: 2026-08-18
Gate: `RESEARCH_LEAVE_LAUNCH_ZENITY.md` (H1 P=0.947)

## 1. Methodology

RDAP. Commit after every Milestone on this worktree branch. Land only
via PR. Prime Directive: path is `worktrees/leave-launch-zenity`.
Never write `main`.

## 2. Scope

**Primary Goal**

Launching `hush-relay --open` must not raise the Cancel / Close / Exit
zenity. That dialog stays a follow-up for a *real* last `--app` window
that died without `/api/close` or `/api/exit`.

**Non-Goals**

- Restyling `#hive-leave` or zenity.
- Replacing Flatpak Brave with a PATH wrapper.
- Auto-Exit on last window.
- Changing attach-on-EADDRINUSE.
- Tray, SIGCHLD default, beforeunload.

**Success Criteria / DoD**

1. `g_saw_app` becomes 1 only after `hush_child_is_app(pid)` is true
   for a live tracked child.
2. `--open` with no PATH browser and a stub zenity does **not** invoke
   zenity within the first 1.5s.
3. After a live `--app`-shaped child dies without leave ack, zenity
   still runs (existing last-window contract).
4. Close still leaves the port up. Exit still dies, code 0.
5. UI_SPEC §10 and README Close vs Exit name the latch.
6. `./configure && make && make test` pass.
7. PR → auto-merge → worktree removed.

**Constraints**

- C11 + write-legible-c §14.
- Worktree `gb/*` + PR only.
- zenity remains optional.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- `git worktree add -b gb/leave-launch-zenity worktrees/leave-launch-zenity`

### Phase 1 — Research & Discovery (GATE)

- Task 1 of M1.1: four-minds file `RESEARCH_LEAVE_LAUNCH_ZENITY.md`.
- Task 2 of M1.1: freeze this plan.
- Task 3 of M1.1: commit.

```bash
git add RESEARCH_LEAVE_LAUNCH_ZENITY.md PLAN_LEAVE_LAUNCH_ZENITY.md
git commit -m "Milestone 1.1: research launch-time zenity race"
```

### Phase 2 — Define

#### M2.1 UI_SPEC + README latch

- Task 1: UI_SPEC §10 — `g_saw_app` latches on a live `--app` cmdline,
  not on the launcher fork.
- Task 2: README Close vs Exit — zenity is not a launch dialog.
- Task 3: commit.

```bash
git add UI_SPEC.md README.md
git commit -m "Milestone 2.1: UI_SPEC latch g_saw_app on live --app"
```

### Phase 3 — Latch

#### M3.1 Set g_saw_app only for a live --app child

File: `hush-c/src/hush_relay.c`.

- Task 1: stop setting `g_saw_app` in `hush_open_app_window`.
- Task 2: in `hush_relay_watch_app`, after forgetting dead children,
  if any remaining child is `hush_child_is_app`, set `g_saw_app = 1`.
- Task 3: keep the existing "saw, now none alive → spawn" path.
- Task 4: commit.

```bash
git add hush-c/src/hush_relay.c
git commit -m "Milestone 3.1: latch g_saw_app on live --app only"
```

### Phase 4 — Verify

#### M4.1 Launch must not invoke zenity

File: `hush-c/tests/check_exit.sh`.

- Task 1: `--open` with PATH = stub zenity only. After wait_up + 0.3s,
  the stub log must be empty.
- Task 2: keep existing Close / Exit / fake-window reap checks.
- Task 3: `./configure && make && make test`.
- Task 4: commit.

```bash
git add hush-c/tests/check_exit.sh
git commit -m "Milestone 4.1: launch must not invoke zenity"
```

### Phase 5 — Land

#### M5.1 PR + auto-merge + delete worktree

```bash
git push -u origin HEAD
gh pr create --base main --head gb/leave-launch-zenity \
  --title "Do not raise leave zenity at launch" \
  --body "…"
gh pr merge --auto --merge
```

## 4. Risks

1. Never latching if Flatpak cmdline is only on a grandchild. Mitigation:
   the live tree already shows `bwrap … --class=hush-relay --app=` as
   the *direct* child (E8). If that ever changes, last-window zenity
   goes silent — better than a launch dialog.
2. Grace before first `--app` appears: with the latch, zenity cannot
   fire until after a live match, so the first-pump race is gone.

## 5. Out of scope later

PATH wrapper for `brave-browser` → Flatpak. Useful, not this bug.
