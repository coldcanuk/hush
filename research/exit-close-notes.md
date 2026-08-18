# Exit/Quit vs Close Design Notes (Phase 1)

## Current Hush behavior (observed in code + user report)
- Single binary: hush-relay listens on TCP, serves HTTP PWA + Nostr line protocol.
- `hush_relay_run(port, open_ui)`:
  - Ignores SIGPIPE, SIGCHLD.
  - Binds listen socket.
  - If EADDRINUSE and open_ui: prints "already listening", forks browser --app to the URL, returns OK (attach path).
  - Else: creates store, enters poll loop.
  - Loop: poll with 1s timeout. On non-EINTR error: break.
  - On exit: turn_shutdown, store_destroy, close(ls), return HUSH_OK.
- Main: parses --open/--no-open, calls run, returns 0 on HUSH_OK else 1.
- Desktop: Exec=hush-relay --open
- No pidfile for the relay itself.
- No SIGINT/SIGTERM handler.
- "Close" buttons in HTML only close local drawers/modals.
- Browser --app window is managed by the browser; C code can only open, not close.

## Desired semantics (user explicit)
- **Close**: The GUI (browser --app / PWA window) disappears. The core relay process keeps running and listening. Clicking the launcher again re-attaches (opens a GUI window to the running server).
- **Exit / Quit**: Full, clean termination of the relay process. Proper signal handling, cleanup, and exit codes.
- They are distinct operations.
- Launcher click while running should re-attach GUI (current EADDRINUSE path kind of does this).
- Proper exit codes: 0 for clean normal operation / intentional quit, non-zero for errors.

## Constraints from existing architecture
- C11, legible, single binary, embedded UI.
- The "GUI" is external (browser --app or xdg-open). We cannot reliably force-close arbitrary browser windows from the relay without brittle tools.
- Attach is currently "if bind fails with EADDRINUSE and we want UI, just open another window".
- Cleanup code already exists on the normal return path.

## Research findings (patterns)
1. Classic "server + UI" separation:
   - Server runs as long-lived process.
   - GUI is a client that connects or is spawned to point at the server.
   - "Close GUI" = client exits (user closes window or we don't spawn it).
   - "Quit server" = server receives termination request and exits cleanly.

2. Re-attach pattern (already partially present):
   - On launcher click: try to bind. If taken, just spawn UI pointing at localhost:port.
   - This gives "click launcher = get a window if server is up".

3. Graceful shutdown:
   - Install handler for SIGINT (Ctrl+C) and SIGTERM.
   - Use a flag + wake the poll (self-pipe is the portable safe way in C; or accept that EINTR + flag check works for this simple case).
   - On flag: break loop, run existing cleanup, return a "QUIT" status or HUSH_OK with note.
   - Main maps to exit code 0 for clean quit.

4. Explicit --quit / --close:
   - --quit: if server is running (bind fails), send SIGTERM to it (need pid), wait briefly, exit 0.
   - --close: no-op success if running (or future "tell running instance to drop its UI state"). For now, can be synonym for "ensure not forcing a window".
   - Launcher for "Close" could be a separate desktop action or just user closing the browser tab/window.

5. Pid / discovery:
   - Write pidfile on successful listen (XDG_RUNTIME_DIR or state dir).
   - On --quit, read pidfile, kill -TERM, unlink.
   - Fallback: use `fuser` or `ss -tlnp` but pidfile is clean.

6. Exit codes (hush_status_t already exists):
   - Keep HUSH_OK = 0 for clean (including intentional quit).
   - Document that normal user quit via signal or --quit returns 0.
   - Errors keep negative codes mapped to 1 in main (or map specific ones).

7. Desktop integration:
   - Keep current "Hush" entry as "Open / Attach".
   - Optionally add a "Quit Hush" .desktop or Actions= section that runs `hush-relay --quit`.
   - "Close" is mostly "close the window you have open".

8. Rebuild / install issue (from history):
   - Always re-embed + rebuild + reinstall when changing UI or behavior.
   - `make install` (local) or explicit cp to ~/.local/bin after build.

## Risks identified
- Browser window close is user action; we can't "close the window" from C reliably → "Close" will mean "do not force-open UI" + user closes window + launcher re-attach still works.
- Multiple instances: we rely on bind failure for attach; pidfile helps for --quit.
- Signal safety: only set flag in handler, do real work in main loop.
- Port already in use by something else: current error path is fine.

## Next (for plan)
- Add graceful signal handling with flag.
- Add pidfile write/remove.
- Add --quit and --close to CLI (and update help/desktop).
- Make attach path (EADDRINUSE) the normal "re-attach" behavior.
- **In-app labeled Close + Exit in the hive header** (user correction 2026-08-18).
  The OS/PWA `×` is not our Close. Drawer Close is not our Close.
- POST /api/close = detach ack (process stays). POST /api/exit = set g_shutdown.
- Ensure cleanup always runs and exit code is sensible.
- Tests: check_launch HTML greps + check_exit.sh for close-stays / exit-dies.
- Update README/help.

