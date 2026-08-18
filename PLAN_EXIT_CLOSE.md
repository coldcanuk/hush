# PLAN: Proper Exit (Quit) vs Close for Hush Relay (RDAP)

Branch: gb/exit-close-design
Worktree: worktrees/exit-close-design
Base: main (034cfd3d5)

## 1. Select Planning Methodology
RDAP (Research-Driven Adaptive Planning) — Double Diamond + Spiral risk iterations + small atomic Milestones with strict DoD + commit per M.

## 2. Scope of Work

**Primary Goal**
Implement two distinct operations:
- **Close**: The GUI (browser --app window) is dismissed. The relay core keeps running and listening. Launcher click re-attaches a GUI to the running process.
- **Exit/Quit**: Full, clean termination of the relay process with proper signal handling, cleanup, and exit codes (0 for intentional clean quit).

**Non-Goals**
- Force-closing external browser windows from C.
- System tray.
- Authenticated control API.
- Changing the external-browser GUI model.

**Success Criteria / DoD (measurable)**
1. Launcher (hush-relay --open or desktop) when nothing running: starts server + opens GUI.
2. Launcher when server running: re-attaches (opens GUI window) via EADDRINUSE path without new listener.
3. User closes browser window: GUI gone, server still up (ss shows port, re-attach works).
4. `hush-relay --quit` or SIGINT/SIGTERM: clean shutdown, exit code 0.
5. `hush-relay --no-open`: headless server.
6. Proper cleanup always runs (turn, store, socket).
7. Exit codes: 0 for clean/quit, non-zero for errors.
8. Updated --help, desktop (Actions or Quit entry), README note.
9. `make && make test` pass.
10. Worktree lifecycle + PR followed.

**Constraints**
- C11 + write-legible-c.
- Single binary + embedded PWA.
- Worktree/gb/* + PR only.
- Re-embed after UI changes.

**Assumptions**
- "GUI close" is user action on browser window or not forcing --open.
- Launcher re-attach on bind failure is the mechanism for "click again".
- Pidfile + signals sufficient for --quit.

**Top Risks + Mitigations**
1. Stale pidfile → check alive with kill(pid,0), careful write.
2. Cannot force browser close → document user closes window; re-attach works.
3. Build drift → document embed+make+cp after changes.
4. Signal safety → only flag in handler.
5. Multiple launches → rely on bind + pidfile.

**Required Environment**
- gcc, make, standard Unix signals/poll/fork.
- For tests: curl, ss, pkill.

## 3. Comprehensive Plan (Phases → Milestones → Tasks)

### Phase 0 — Environment & Isolation Setup (COMPLETE)
- M0.1: Worktree gb/exit-close-design created on clean main (verified).

### Phase 1 — Research & Discovery (GATE)
- M1.1: Inventory current code (loop, signals, attach, main, desktop, cleanup).
  - Task 1.1.1: `cd worktrees/exit-close-design && grep -n -E 'signal|SIG|poll|EINTR|break|return HUSH|hush_open_app_window|EADDRINUSE' hush-c/src/hush_relay.c`
  - Task 1.1.2: `cat hush-c/src/hush_relay_main.c`
  - Task 1.1.3: `cat hush-relay.desktop`
  - Verification: Output shows only PIPE/CHLD ignore, poll loop exits only on error, attach on EADDRINUSE, desktop always --open, main returns 0/1.
- M1.2: Research Unix patterns for daemon+GUI (attach vs quit) + pidfile + signals.
  - Task 1.2.1: Create research/exit-close-notes.md with findings (self-pipe or flag, pidfile, re-attach via bind fail, --quit sends SIGTERM).
  - Verification: File exists with sections on current behavior, desired, patterns, risks.
- M1.3 (MANDATORY LAST): Synthesize into RESEARCH_EXIT_CLOSE.md (or append) + concrete PLAN_EXIT_CLOSE.md + commit.
  - Task 1.3.1: `git add RESEARCH_EXIT_CLOSE.md PLAN_EXIT_CLOSE.md RESEARCH.md && git commit -m "Milestone 1.3: Phase 1 synthesis gate — exit vs close research + plan"`
  - Task 1.3.2: `git push -u origin HEAD`
  - Verification: Commit message shows M1.3; plan file present with full RDAP structure.

### Phase 2 — Define / Architecture
- M2.1: Lock design (flags, signals, pidfile, attach message, desktop).
  - Task 2.1.1: Write detailed design section in PLAN (volatile flag + check after poll, pidfile in XDG_RUNTIME_DIR or ~/.local/state/hush/relay.pid, --quit reads pid + kill -TERM, --close as friendly no-op or "no open", friendly attach message).
  - Task 2.1.2: Update exit codes doc (0 for clean quit).
  - Verification: Design written, reviewed against constraints.
- M2.2: Update risk register + test strategy.
  - Commit after M2.

### Phase 3 — Implementation
- M3.1: Add graceful signal handling + flag.
  - Task 3.1.1: In hush_relay.c, add volatile sig_atomic_t g_shutdown = 0; handler for SIGINT/SIGTERM that sets it.
  - Task 3.1.2: In loop, on EINTR or after poll, if (g_shutdown) break;
  - Task 3.1.3: `./scripts/embed-ui.sh hush-c/demo 2>/dev/null || true; make -C hush-c clean; make -C hush-c`
  - Verification: Build succeeds; manual test Ctrl+C does cleanup and exits 0 (check logs, ss port gone).
  - Commit: "M3.1: Graceful SIGINT/SIGTERM handling with flag"
- M3.2: Pidfile support.
  - Task 3.2.1: Add pidfile write on successful listen (use XDG_RUNTIME_DIR/hush/relay.pid or fallback).
  - Task 3.2.2: Unlink on clean exit.
  - Task 3.2.3: Rebuild.
  - Verification: After start, pidfile exists and contains correct pid.
  - Commit.
- M3.3: Add --quit and --close to CLI + main.
  - Task 3.3.1: In hush_relay_main.c, handle --quit: read pidfile, if alive kill -TERM, unlink, return 0.
  - Task 3.3.2: Handle --close: print friendly message, return 0 (no-op for server; documents "GUI closed").
  - Task 3.3.3: Update hush_print_help().
  - Task 3.3.4: Rebuild + test `./hush-c/hush-relay --help`.
  - Verification: --quit stops a running instance cleanly (exit 0); --close exits 0 quickly.
  - Commit.
- M3.4: Improve attach message and desktop.
  - Task 3.4.1: Change "already listening" message to "Hush relay running — opening UI...".
  - Task 3.4.2: Update hush-relay.desktop with Actions=Quit; or add comment for --quit.
  - Task 3.4.3: Rebuild.
  - Verification: Launch while running shows friendly message and re-opens UI.
  - Commit.
- M3.5 (if needed): Minor C legible-c fixes + tests.
  - Full checklist if any .c/.h touched.
  - Extend check_launch.sh or add simple test for --quit.
  - Commit.

### Phase 4 — Verification, Polish, Integration & Cleanup
- M4.1: Full build + test.
  - `./configure && make clean && make && make test`
  - Manual: start, launcher click (re-attach), close browser, launcher re-attach, --quit, Ctrl+C.
  - Verification: all green, port released on quit, exit code 0 on clean quit.
- M4.2: Docs + polish.
  - Update README with "Close vs Quit" section.
  - Update help text if needed.
  - Run legible-c checklist on changed files.
- M4.3: Final commit + push.
  - `git add . && git commit -m "Complete: exit/quit vs close design — proper signals, --quit, pidfile, re-attach, exit codes 0 for clean quit"`
  - `git push -u origin HEAD`
- M4.4: PR + land.
  - `gh pr create --base main --head gb/exit-close-design --title "Proper Exit (Quit) vs Close for relay (signals, --quit, pidfile, re-attach)" --body "..." `
  - `gh pr merge --auto --merge`
- M4.5: Post-merge cleanup.
  - `cd /opt/repo/hush && git checkout main && git pull --ff-only origin main`
  - `git worktree remove worktrees/exit-close-design`
  - `git branch -d gb/exit-close-design || true`
  - `git push origin --delete gb/exit-close-design || true`
  - Verify only main worktree, clean.

## 4. Audit the Plan (before execution)
- Every Task has exact command or snippet + verification + M reference.
- Phase 1 gate (M1.3) present.
- Worktree lifecycle complete.
- Tasks atomic.
- legible-c called out.
- Research → plan gate explicit.
- Plan frozen after this audit.

## 5-7. Execute → Audit → Confirm
Follow strictly. Commit after every Milestone. Re-audit at end. State "Grok Build complete." only when all DoD + cleanup done.

## Notes
- Use `scripts/embed-ui.sh hush-c/demo` + make after any change that could affect served assets (even if not UI).
- Payne voice / disciplined: messages are clear, no fluff ("Hush relay running — opening UI...").
- Quinn: low cog load (one flag for quit, simple --quit/--close).
- Parker: serves the JTBD "I can reliably close the chat window while keeping my hive alive, or fully quit when done."

