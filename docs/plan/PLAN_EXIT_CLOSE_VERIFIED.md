# PLAN_EXIT_CLOSE.md — Verification Gate

Base: main ad35d4f3f (fresh worktree gb/exit-close-design)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_EXIT_CLOSE_CURRENT.md
- Confirmed: all primary DoD items already present on this base.

## DoD (satisfied)
1. Header shows labeled Close and Exit
2. Close: POST /api/close + window.close(); port still listening; re-attach works
3. Exit: confirm, POST /api/exit, process exits 0, port/pidfile gone
4. --quit and SIGINT/SIGTERM take same cleanup, exit 0
5. --close prints re-attach hint, exits 0 (server stays)
6. --no-open still starts headless
7. Cleanup always: turn, store, socket, pidfile
8. Exit codes: 0 clean, non-zero on error
9. --help, desktop Actions=Quit, README Close vs Exit
10. make + make test pass; HTML greps for buttons
11. PR → merge → worktree removed (pending)

## Verification executed
- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_exit.sh → exit routes ok
- Explicit greps for hive-close/exit, /api/close/exit, g_shutdown, pidfile, unlink, --close/--quit
- No new C required

## Constraints
- Prime Directive: gb/* only; PR to main
- C11 + legible-c (existing)
- Re-embed after HTML (satisfied in prior)
- Hick: header choices bounded
- No new auth on exit

