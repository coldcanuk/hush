# 2026-08-18 RDAP: Exit (Quit) vs Close for Hush Relay

## Scope Locked
Primary goal: Distinguish and implement two clear operations:
- **Close**: The GUI (browser --app / PWA window) is dismissed. The relay core keeps running and listening on the port. Subsequent launcher clicks re-attach a GUI window to the running process.
- **Exit / Quit**: Full, clean termination of the `hush-relay` process (proper signals, cleanup, exit codes).

Non-goals:
- Forcing the browser to close an arbitrary --app window from C (impossible reliably).
- System tray / notification area icon.
- Authenticated remote control channel for quit.
- Windows service semantics.

Success / DoD (measurable):
1. Launcher click (hush-relay --open) when nothing running: starts server + opens GUI.
2. Launcher click when server already running: re-attaches (opens another GUI window to same server) without starting a second listener.
3. User closes the browser --app window → GUI gone, server still running (verified by netstat/ss + ability to re-attach).
4. `hush-relay --quit` (or SIGINT/SIGTERM): cleanly shuts down the server process. Exit code 0 for intentional clean quit.
5. `hush-relay --no-open`: starts server headless (no browser spawn).
6. Proper cleanup always runs on normal exit paths (turn_shutdown, store_destroy, close listen socket).
7. Exit codes: 0 for clean success/quit, non-zero for errors (bind, store, etc.).
8. Updated help, desktop file (Actions or separate entry for Quit if useful), README.
9. `make && make test` pass.
10. Worktree `gb/exit-close-design` → PR → merge → delete per PRIME_DIRECTIVE.

## Current Behavior (research findings from code)
- `hush_relay_run(port, open_ui)`:
  - Only ignores SIGPIPE + SIGCHLD.
  - Binds. On EADDRINUSE + open_ui: prints "already listening", calls `hush_open_app_window` (fork + execlp browser --app), returns OK. This is the current re-attach path.
  - Otherwise enters poll loop.
  - Loop: `poll(..., 1000ms)`. Exits only on poll error that is not EINTR.
  - On any return path: `hush_turn_shutdown`, `hush_store_destroy`, `close(ls)`, return HUSH_OK.
- Main: parses only --open / --no-open / port / -h. Calls run, `return (st==HUSH_OK)?0:1`.
- Desktop: `Exec=hush-relay --open`.
- No pidfile for the relay process itself (turn writes one for its child).
- No graceful SIGINT/SIGTERM handler for the main poll server.
- Browser "close" buttons only affect local drawers/modals (settings, call, profile).
- "Stop" documented only as Ctrl+C (which currently may not be clean).

## Research Synthesis (Unix / GUI+daemon patterns)
- Classic pattern: long-lived server process + separate GUI client.
- "Close GUI" = stop spawning or let user close the window. Server keeps listening.
- Re-attach on launcher click = try bind; if taken, just spawn UI to localhost:port.
- Graceful shutdown:
  - Install SIGINT (Ctrl+C) + SIGTERM handlers that set a flag (async-signal-safe).
  - Wake the poll (EINTR is already handled by continuing; we check flag after).
  - On flag: break, run cleanup, return.
- Pidfile (XDG_RUNTIME_DIR or ~/.local/state/hush/relay.pid) for `--quit` implementation.
- `--quit`: if we can find a running instance (pidfile or bind fail), send SIGTERM, wait briefly, unlink, exit 0.
- `--close` (for launcher "close" semantics): currently can be a no-op that succeeds if server is up (or future "tell server not to auto-open"). For MVP, document that closing the browser window + launcher click = re-attach.
- Exit codes: keep HUSH_OK → 0 for clean (including intentional quit via signal or --quit). Errors → 1 (or map specific status).
- Desktop: current entry = "Hush" (open/attach). Add `Actions=Quit;` or a second .desktop "Hush Quit" that runs `--quit`.

## Architecture Decisions (locked for this slice)
- No new control socket or HTTP /quit endpoint in this slice (keep simple; use signals + pidfile).
- Pidfile location: `$XDG_RUNTIME_DIR/hush/relay.pid` or fallback `~/.local/state/hush/relay.pid` (or /tmp for tests).
- Re-attach remains the EADDRINUSE + open_ui path (it already works for "click launcher again").
- Add CLI flags: `--quit`, keep `--open` / `--no-open`.
- Graceful signal handling: set volatile flag on SIGINT/SIGTERM, check after poll.
- Always run the existing cleanup on exit from the loop.
- Update `hush_print_help`, desktop file (Actions or comment), and a short section in README.
- Legible-c for all .c/.h changes.
- Re-embed + rebuild + reinstall (cp to ~/.local/bin) is user responsibility after source change (document it).

## Risks + Mitigations
1. Cannot force-close browser --app from C → "Close" means user closes window or we don't force --open; re-attach still works.
2. Pidfile races / stale → write with O_EXCL or check alive with kill(pid,0) before using.
3. Multiple users on same machine → use per-user XDG paths.
4. Build/install drift (seen before) → after changes: `make -C hush-c; cp hush-c/hush-relay ~/.local/bin/`.
5. Signal handler safety → only set flag; no malloc/stdio in handler.

## Verification Performed (this research)
- Full read of hush_relay.c (loop, attach, open_app, cleanup, signals).
- Read hush_relay_main.c, hush_relay.h, hush_turn.c (shutdown), hush_store.c (destroy).
- Desktop file, help text, current launch behavior.
- User report history (old binary served old UI; launcher always --open).
- Unix patterns for daemon + detachable GUI confirmed.

References: existing RESEARCH.md (prior first-launch work), code, desktop file, standard C signal + poll + pidfile patterns.

