# PLAN — lingering hush-relay + OS window × dialog

Branch: `gb/close-x-dialog`
Worktree: `worktrees/close-x-dialog`
Base: `main` `b41b7f71c`
Date: 2026-08-18
Gate: `RESEARCH_CLOSE_X_DIALOG.md` (H1 cause P=0.663, H5 lever P=0.311)

## 1. Methodology

RDAP — Double Diamond + Spiral + atomic Milestones. Commit after every
Milestone on this worktree branch. Land only via PR. Prime Directive
overrides the user template: path is `worktrees/close-x-dialog`, never
`../gb-close-x-dialog-wt`. Never merge onto local `main`.

## 2. Scope

**Primary Goal**

When someone means to leave, they get three labeled choices:

1. **Exit the application** — `POST /api/exit`, reap, process exits 0.
2. **Close the window** — `POST /api/close`, GUI gone, relay stays.
3. **Cancel** — stay (or re-attach if the OS × already killed the window).

The OS/PWA × cannot host that window (Brave `--app`). The hive owns
`#hive-leave`. When the last `--app` child dies without Exit/Close in
flight, a native zenity follow-up asks the same three verbs.

**Non-Goals**

- Customizing or restyling the OS ×.
- `beforeunload` as the three-choice UI.
- Auto-Exit on last window close (choosing Exit for the operator).
- Replacing `--app` with GTK/Qt.
- Tray, remote auth, attach-on-EADDRINUSE rewrite.
- hush_agent / thread pane / OAuth / rail docks.

**Success Criteria / DoD**

1. `#hive-leave` exists with `#leave-exit`, `#leave-close`, `#leave-cancel`.
   Each control ≥44px.
2. `#hive-close` and `#hive-exit` open `#hive-leave` (one decision).
3. `#leave-exit` → `POST /api/exit` → `window.close()`. No second confirm.
4. `#leave-close` → `POST /api/close` → `window.close()`; banner if it stays.
5. `#leave-cancel` hides the drawer. Relay stays. Window stays.
6. Close still leaves the port up (`check_exit.sh` unchanged contract).
7. Exit still dies, code 0, pidfile gone, `--app` child reaped.
8. Last tracked `--app` pid going away without a recent close/exit
   forks zenity (or no-ops with the attach hint if zenity is missing).
   Exit / Close / Cancel map as locked in RESEARCH.
9. `SIGCHLD` stays `SIG_IGN`. Death is `kill(pid, 0)` in the pump.
10. UI_SPEC §10 revised. README Close vs Exit names the three choices
    and admits the OS × is Brave's.
11. `./configure && make && make test` pass.
12. PR → auto-merge → worktree removed. Never write `main`.

**Constraints**

- C11 + write-legible-c §14 (fn ≤40, depth ≤2, named literals, ≤4
  params or struct, asserts on state leaves).
- Worktree `gb/*` + PR only.
- Re-embed after every HTML change: `./scripts/embed-ui.sh hush-c/demo`.
- Hick: one chooser, not a fourth header verb.
- No auth on `/api/close` / `/api/exit`.
- zenity is optional; missing binary must not crash the relay.

**Assumptions**

- GUI = Brave/Chromium `--app` (live E2).
- `window.close()` works for `--app`.
- Operator's `kill -9` is to free `:10555` for a new binary (E8).
- A1 (they only click ×) stays unverified; the chooser covers that habit
  and the in-app buttons.

**Top Risks** (from RESEARCH)

1. Zenity is late (window already gone). Mitigation: in-page chooser
   is the primary path; zenity is the × follow-up.
2. `SIGCHLD` vs agent/pass waitpid. Mitigation: leave `SIG_IGN`;
   `kill(pid, 0)` in the pump.
3. Zenity must not block the poll loop. Fork + track.
4. Double prompt. Ignore last-child if `/api/close` or `/api/exit`
   happened this session (`g_leave_ack`).
5. Hick. Rail Close/Exit open the same drawer.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- Task 1 of M0.1: `git checkout main && git pull --ff-only origin main`.
  Verify: `CLEAN_OK` at `b41b7f71c`.
- Task 2 of M0.1: `git worktree add -b gb/close-x-dialog worktrees/close-x-dialog`.
  Verify: `WORKTREE_OK /opt/repo/hush/worktrees/close-x-dialog`.

### Phase 1 — Research & Discovery (GATE, this commit)

- Task 1 of M1.1: four-minds evidence E1–E14, A1–A5, H1–H5, Bayes.
  File: `RESEARCH_CLOSE_X_DIALOG.md`.
- Task 2 of M1.1: freeze this plan. File: `PLAN_CLOSE_X_DIALOG.md`.
- Task 3 of M1.1: commit.

```bash
git add RESEARCH_CLOSE_X_DIALOG.md PLAN_CLOSE_X_DIALOG.md
git commit -m "Milestone 1.1: research leftover listener and OS-X chooser"
git push -u origin HEAD
```

Verify: `git log -1 --oneline` on `gb/close-x-dialog`.

### Phase 2 — Define / Architecture

#### M2.1 UI_SPEC §10 revision

- Task 1 of M2.1: rewrite `UI_SPEC.md` §10.
  - Version line: `gb/close-x-dialog`.
  - OS × is Brave's. It cannot host three buttons.
  - `#hive-leave` contract (ids, verbs, Fitts, no double confirm).
  - `#hive-close` / `#hive-exit` open `#hive-leave`.
  - Last `--app` child + zenity follow-up. `g_leave_ack`. `kill(pid,0)`.
  - Close still never kills. Exit still always does.
- Task 2 of M2.1: commit.

```bash
git add UI_SPEC.md
git commit -m "Milestone 2.1: UI_SPEC leave chooser and last-window zenity"
```

Verify: `rg -n 'hive-leave|leave-exit|zenity' UI_SPEC.md`.

### Phase 3 — Relay last-window sensor + zenity

#### M3.1 Track app-window pids distinctly; pump notices death

Files: `hush-c/include/hush_relay.h`, `hush-c/src/hush_relay.c`.

- Task 1 of M3.1: named literals
  `HUSH_LEAVE_ZENITY`, `HUSH_LEAVE_TITLE`, `HUSH_LEAVE_TEXT`,
  extra-button labels. `g_leave_ack` set by `/api/close` and `/api/exit`.
- Task 2 of M3.1: public `hush_relay_note_leave(int is_exit)` so HTTP
  can set the ack (0 = close, 1 = exit). Exit still calls
  `hush_relay_request_shutdown`.
- Task 3 of M3.1: in `hush_relay_pump`, after `hush_agent_poll`, call
  `hush_relay_watch_app()`:
  - For each tracked child, `kill(pid, 0)`.
  - If all `--app` children are gone, `g_leave_ack` is 0, and
    `g_shutdown` is 0: `hush_relay_prompt_leave()`.
  - `hush_relay_prompt_leave` forks zenity (non-blocking), tracks the
    pid. Missing zenity: print the attach hint, set `g_leave_ack`.
- Task 4 of M3.1: when the zenity child exits, map status:
  - extra button / OK → Exit (`hush_relay_request_shutdown`)
  - close-the-window button → set `g_leave_ack`, stay up
  - cancel / escape → `hush_open_app_window`, set `g_leave_ack`
  Exact zenity argv locked in the implementation comment after a
  one-shot `zenity --help-general` read. Prefer:

```
zenity --question \
  --title="Leave the hive?" \
  --text="The window closed. The hive is still standing." \
  --ok-label="Exit the application" \
  --extra-button="Close the window" \
  --cancel-label="Cancel"
```

  Confirm mapping against zenity 4.0.1 exit codes in a throwaway
  (`zenity --help-general`); document in a 4-line comment.

- Task 5 of M3.1: write-legible-c §14 on every new function.
  `SIGCHLD` stays `SIG_IGN`.

```bash
git add hush-c/include/hush_relay.h hush-c/src/hush_relay.c hush-c/src/hush_http.c
git commit -m "Milestone 3.1: last-app-window zenity leave prompt"
```

Verify: `./configure && make` in `hush-c` (or worktree `./configure && make`).

### Phase 4 — In-page `#hive-leave`

#### M4.1 Dialog + wire Close/Exit through it

File: `hush-c/demo/index.html`, then embed.

- Task 1 of M4.1: CSS for `#hive-leave .leave-actions` stacked
  ≥44px buttons. Exit is `.btn.danger`. Close is `.btn`. Cancel is
  `.btn.ghost`.
- Task 2 of M4.1: markup after `#relay-drawer`:

```html
<div class="drawer" id="hive-leave" role="dialog" aria-label="Leave the hive">
  <div class="panel">
    <h2>Leave the hive?</h2>
    <p class="help">Exit stops every process. Close leaves the hive standing. Cancel stays.</p>
    <div class="leave-actions">
      <button class="btn danger" id="leave-exit" type="button">Exit the application</button>
      <button class="btn" id="leave-close" type="button">Close the window</button>
      <button class="btn ghost" id="leave-cancel" type="button">Cancel</button>
    </div>
  </div>
</div>
```

- Task 3 of M4.1: JS. `openLeave()` adds `.show`. `#hive-close` and
  `#hive-exit` call it. `#leave-exit` is today's exit handler without
  `confirm()`. `#leave-close` is today's close handler. `#leave-cancel`
  removes `.show`. Escape hides it.
- Task 4 of M4.1: `./scripts/embed-ui.sh hush-c/demo` from worktree root.

```bash
git add hush-c/demo/index.html hush-c/include/hush_ui_html.h
git commit -m "Milestone 4.1: in-page Exit / Close / Cancel chooser"
```

Verify: `rg -n 'id="hive-leave"|id="leave-exit"|id="leave-close"|id="leave-cancel"' hush-c/demo/index.html`.

### Phase 5 — Tests and docs

#### M5.1 Greps, exit contract, README

- Task 1 of M5.1: `hush-c/tests/check_launch.sh` greps for
  `id="hive-leave"`, `id="leave-exit"`, `id="leave-close"`,
  `id="leave-cancel"`, `openLeave`.
- Task 2 of M5.1: `check_exit.sh` keeps Close-stays / Exit-dies.
  Optional: grep `hush_relay_prompt_leave` / `g_leave_ack` in
  `hush_relay.c` so the sensor cannot silently vanish.
- Task 3 of M5.1: README Close vs Exit paragraph names the three
  choices and says the OS × is Brave's; last-window zenity follows.
  NOSTR.md one line if it mentions Close/Exit.
- Task 4 of M5.1: `./configure && make && make test`.

```bash
git add hush-c/tests/check_launch.sh hush-c/tests/check_exit.sh README.md NOSTR.md
git commit -m "Milestone 5.1: leave-dialog tests and Close vs Exit docs"
```

Verify: last line of `make test` is `ALL TESTS PASSED`.

### Phase 6 — Land

#### M6.1 PR, merge, delete worktree

```bash
git push -u origin HEAD
gh pr create --base main --head gb/close-x-dialog \
  --title "Leave chooser: Exit / Close / Cancel (OS × follow-up)" \
  --body "…"
gh pr merge --auto --merge
# wait MERGED
cd /opt/repo/hush
git checkout main && git pull --ff-only origin main
git worktree remove worktrees/close-x-dialog
git branch -d gb/close-x-dialog 2>/dev/null || true
git push origin --delete gb/close-x-dialog 2>/dev/null || true
```

Verify: `git worktree list` shows only `/opt/repo/hush` on `main`.
State: “Grok Build complete.”

## 4. Audit of this plan

- Every Task names its Milestone, has CLI/code, has a verify step.
- Phase 1 ends with RESEARCH + PLAN commit (this file).
- Worktree path is `worktrees/close-x-dialog` (Prime Directive).
- Land is PR + auto-merge, not local merge.
- Tasks are atomic (spec, C sensor, HTML, tests, PR).
- Close ≠ Exit preserved. H4 (`beforeunload` fake ×) rejected.

Frozen for execution.
