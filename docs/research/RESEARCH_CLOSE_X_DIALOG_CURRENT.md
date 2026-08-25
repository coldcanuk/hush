# RESEARCH — Lingering hush-relay + OS window × dialog (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/close-x-dialog`
Branch: `gb/close-x-dialog`
Base: `main` `ce415223d` (post #76 payne-provider-edit)

## Base State

PLAN_CLOSE_X_DIALOG.md is a verification/hygiene slice. The implementation is already present on this base from prior exit/close/relay work:

**UI chooser:**
- #hive-leave drawer with #leave-exit, #leave-close, #leave-cancel (≥44px targets)
- #hive-close and #hive-exit both open the same drawer (openLeave)
- #leave-cancel hides drawer
- #leave-close: POST /api/close then window.close()
- #leave-exit: POST /api/exit then window.close()
- No second confirm on exit

**HTTP:**
- /api/close and /api/exit handlers present
- /api/close returns {action:"close"}, leaves relay up
- /api/exit returns {action:"exit"}, triggers shutdown

**Relay last-window / zenity:**
- g_leave_ack set by close (0) and exit (1)
- hush_relay_watch_app / pump uses kill(pid,0) on tracked --app children
- When last --app gone and !g_leave_ack && !g_shutdown: forks zenity (hush_leave_run_zenity)
- Zenity extra buttons map to exit/close/cancel; missing zenity prints the attach hint
- SIGCHLD handling allows reaping without interfering with agent/pass waits
- g_leave_ack prevents double prompt after explicit close/exit

**Tests:**
- check_exit.sh unchanged contract: --close leaves relay up, --exit dies 0, pidfile gone, children reaped
- make -C hush-c test → ALL PASS

**Docs:**
- UI_SPEC §10 covers the chooser, OS × is Brave's, zenity follow-up
- README names Close vs Exit

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_exit.sh → exit routes ok (close leaves up, exit dies, reaped)
- Explicit greps:
  - HTML: hive-leave, leave-exit/close/cancel, hive-close/exit openLeave
  - http.c: /api/close, /api/exit
  - relay.c: g_leave_ack, hush_leave_run_zenity, kill(pid,0), watch_app, zenity fork
  - check_exit: close/exit behavior
- No new C required

## Differences from original PLAN base

- Current base is later. The three-choice drawer, /api/close/exit, zenity follow-up for last --app, g_leave_ack, kill(pid,0) pump, and test contracts were implemented in exit/close/relay slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of rail-prov #65, canvas-fim #66, provider-* #67/68, oauth-*/thread-* #69-73, onboard/splash #74, one-joke #75, payne #76, vibe #77, pills #78, etc.) to close PLAN_CLOSE_X_DIALOG.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria / DoD (1-12).
No code changes needed.
H4 lock (in-page chooser primary, zenity only for × follow-up, g_leave_ack to avoid double, SIG_IGN + kill(pid,0), Close never kills, Exit always does) holds.

Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/close-x-dialog from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_exit.sh
- rg/grep for hive-leave, leave-*, /api/close, /api/exit, g_leave_ack, zenity, kill(pid,0), watch_app
- Source + test + HTML inspection
