# RESEARCH — Exit (Quit) vs Close — in-app buttons + process lifecycle (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/exit-close-design`
Branch: `gb/exit-close-design`
Base: `main` `ad35d4f3f` (post #79 deepseek-robot-btns)

## Base State

PLAN_EXIT_CLOSE.md is a verification/hygiene slice. The implementation is already present on this base from prior exit/close/relay work:

**In-app buttons:**
- Header: #hive-close (Close) + #hive-exit (Exit) with clear titles
- #hive-leave chooser (already verified in close-x slice) with leave-close / leave-exit / leave-cancel

**HTTP + lifecycle:**
- /api/close and /api/exit handlers
- g_shutdown flag, pidfile (XDG_RUNTIME or ~/.local/state), unlink on exit paths
- --close leaves relay up (prints re-attach hint)
- --quit / SIGTERM / /api/exit take shutdown + cleanup path, exit 0
- Port listening after close; gone after exit

**CLI:**
- --close, --quit, --no-open, --help documented
- Desktop Actions=Quit

**Tests:**
- make -C hush-c test → ALL PASS
- check_exit.sh: --close leaves relay up, --exit dies 0, pidfile gone, children reaped

**Docs:**
- UI_SPEC §10, README Close vs Exit section

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_exit.sh → exit routes ok (close leaves up, exit dies, reaped)
- Explicit greps:
  - HTML: hive-close, hive-exit, titles
  - http.c: /api/close, /api/exit
  - relay.c: g_shutdown, pidfile helpers, unlink, watch_app
  - main.c: --close/--quit paths
- No new C required

## Differences from original PLAN base

- Current base is later. Close/Exit buttons, /api/close/exit, pidfile, g_shutdown, --close/--quit, check_exit contract, UI_SPEC/ README were implemented in exit/close/relay slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of rail-prov, canvas-fim, provider-*, oauth-*, thread-*, onboard-*, splash-*, payne-*, vibe-*, pills-*, close-x, code-canvas, conv-intel, deepseek, etc.) to close PLAN_EXIT_CLOSE.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria / DoD.
No code changes needed.
H4 lock (Close never kills the relay; Exit always does; pidfile + unlink; g_shutdown flag; --close re-attach hint; cleanup always runs) holds.

Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/exit-close-design from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_exit.sh
- rg/grep for hive-close, hive-exit, /api/close, /api/exit, g_shutdown, pidfile, unlink, --close, --quit
- Source + test + HTML inspection
