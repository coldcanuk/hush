# PLAN_CLOSE_X_DIALOG.md — Verification Gate

Base: main ce415223d (fresh worktree gb/close-x-dialog)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_CLOSE_X_DIALOG_CURRENT.md
- Confirmed: all primary DoD items already present on this base from prior exit/close/relay slices.

## DoD checklist (satisfied)
1. #hive-leave with leave-exit / leave-close / leave-cancel (≥44px targets)
2. hive-close and hive-exit open the chooser
3. leave-exit → POST /api/exit → window.close(); no second confirm
4. leave-close → POST /api/close → window.close(); banner if stays
5. leave-cancel hides drawer; relay and window stay
6. Close still leaves port up (check_exit contract)
7. Exit still dies 0, pidfile gone, --app child reaped
8. Last --app death without recent close/exit forks zenity (or prints attach hint)
9. SIGCHLD stays SIG_IGN; death via kill(pid,0) in pump
10. UI_SPEC §10 revised; README names three choices and OS × is Brave's
11. make && make test pass
12. PR → auto-merge → worktree removed (pending)

## Verification executed
- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_exit.sh → exit routes ok (close leaves up, exit dies, reaped)
- Explicit greps:
  - HTML: hive-leave, leave-*, hive-close/exit openLeave
  - http.c: /api/close, /api/exit
  - relay.c: g_leave_ack, hush_leave_run_zenity, kill(pid,0), watch_app, zenity fork
  - check_exit: close/exit behavior
- No new C required

## Constraints
- Prime Directive: gb/* only; PR to main
- C11 + legible-c (existing)
- Re-embed after HTML (satisfied in prior)
- Hick: one chooser
- No auth on close/exit
- Zenity optional; missing binary prints hint

