# PLAN: Exit (Quit) vs Close — in-app buttons + process lifecycle (RDAP)

Branch: `gb/exit-close-design`
Worktree: `worktrees/exit-close-design`
Base: `main` `75d52f238` (rebased; original research was on `034cfd3d5`)

## 1. Methodology

RDAP — Double Diamond + Spiral risk iterations + atomic Milestones with
strict Definition of Done. Commit after every Milestone. Land only via PR.

## 2. Scope

**Primary Goal**

Two distinct, labeled operations the user can see and use:

- **Close** — dismiss the GUI. The relay keeps listening. Next launcher
  click re-attaches the GUI.
- **Exit** — quit. Kill the relay process, run cleanup, return exit code 0.

The OS/PWA window `×` is neither of these. The hive must ship its own
**Close** and **Exit** buttons.

**Non-Goals**

- Force-closing an arbitrary browser window from C.
- System tray.
- Authenticated remote control API.
- Changing the single-binary + `--app` window model.
- Restyling the OS `×`.

**Success Criteria / DoD**

1. Header shows labeled **Close** and **Exit** (not a lone `×`).
2. Close: `POST /api/close` then `window.close()`. Port still listening.
   `hush-relay --open` re-attaches.
3. Exit: confirm, `POST /api/exit`, process exits 0, port gone, pidfile gone.
4. `hush-relay --quit` and SIGINT/SIGTERM take the same cleanup path, exit 0.
5. `hush-relay --close` prints the re-attach hint and exits 0 (server stays).
6. `hush-relay --no-open` still starts headless.
7. Cleanup always runs: turn, store, listen socket, pidfile.
8. Exit codes: 0 clean/quit, non-zero on error.
9. `--help`, desktop Actions=Quit, README Close vs Exit section.
10. `./configure && make && make test` pass. HTML greps for the buttons.
11. PR → merge → worktree removed. Never write to `main`.

**Constraints**

- C11 + write-legible-c (fn ≤40, depth ≤2, named literals, no function-static
  mutables, asserts on internal helpers).
- Worktree `gb/*` + PR only.
- Re-embed after every HTML change: `./scripts/embed-ui.sh hush-c/demo`.
- Hick: header ≤5 primary choices.
- Listen is localhost-facing in practice; no new auth on `/api/exit`.

**Assumptions**

- GUI = Chromium-family `--app` window (or tab if that is what opened).
- `window.close()` works for `--app`. Tabs may refuse; then we show a banner.
- Same `g_shutdown` flag for SIGTERM, `--quit`, and `/api/exit`.

**Top Risks**

1. Stale pidfile → `kill(pid, 0)` before SIGTERM; unlink on every exit path.
2. `/api/exit` from a tab cannot be undone → confirm dialog.
3. `window.close()` no-op in a normal tab → banner, do not pretend we quit.
4. Signal-unsafe work in the handler → flag only.
5. Header Hick overflow → Close/Exit replace nothing essential; Call stays
   hidden until ready; Install stays opportunistic.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- Worktree `worktrees/exit-close-design` on `gb/exit-close-design`.
- Rebased onto `main` `75d52f238` (PR #24 robot cards).

### Phase 1 — Research (GATE, this commit)

- M1.1–M1.3 already landed CLI-only research.
- **M1.4 (this commit):** user correction — in-app Close/Exit are required.
  Update RESEARCH.md, this plan, UI_SPEC. Commit the gate.

### Phase 2 — Architecture (this commit)

Locked design below. UI_SPEC section 10 is authoritative for chrome.

### Phase 3 — Implementation

#### M3.1 Signals + shutdown flag

Files: `hush-c/src/hush_relay.c`, `hush-c/include/hush_relay.h`.

- `static volatile sig_atomic_t g_shutdown = 0;`
- Handler sets the flag only.
- `hush_relay_request_shutdown(void)` sets the same flag (HTTP Exit).
- After `poll`: EINTR continue unless `g_shutdown`; then break.
- On break: existing cleanup, then unlink pidfile (once M3.2 lands).

Verify: `make -C hush-c` compiles.

#### M3.2 Pidfile

- Path: `$XDG_RUNTIME_DIR/hush/relay.pid` else `$HOME/.local/state/hush/relay.pid`.
- Write after successful `listen`, before the poll loop (`0600`).
- Unlink on every return from `hush_relay_run` that got past listen.
- Helpers at file end: `hush_pidfile_path`, `hush_write_pidfile`,
  `hush_remove_pidfile`.

Verify: start `--no-open`, `cat` pidfile matches `pidof`/`pgrep`.

#### M3.3 CLI `--quit` / `--close`

`hush-c/src/hush_relay_main.c`:

- `--quit`: read pidfile, `kill(pid, 0)` then `SIGTERM`, wait up to 2s,
  unlink, return 0. Missing server → 0 (idempotent).
- `--close`: print
  `GUI closed. Relay still running. Click the launcher to re-attach.`
  return 0.
- Help lists both plus "Close vs Exit" paragraph.
- Attach message:
  `hush-relay already running on http://127.0.0.1:%u/ — opening UI...`

`hush-relay.desktop`:

```
Actions=Quit;
[Desktop Action Quit]
Name=Quit Hush
Exec=hush-relay --quit
```

Verify: `--help` shows the verbs; `--close` exits 0.

#### M3.4 HTTP `/api/close` and `/api/exit`

`hush-c/include/hush_http.h` + `hush-c/src/hush_http.c`:

- `POST /api/close` → `200 {"ok":true,"action":"close"}`. No flag.
- `POST /api/exit` → `200 {"ok":true,"action":"exit"}` then
  `hush_relay_request_shutdown()`.
- Dispatch in `hush_http_serve_api_post`. Small dedicated helpers.

Verify: curl against a `--no-open` instance; `/api/exit` drops the port.

#### M3.5 In-app buttons

`hush-c/demo/index.html`:

- Header: `#hive-close` "Close", `#hive-exit` "Exit".
- Confirm dialog for Exit.
- Close handler: POST `/api/close`, then `window.close()`, else banner.
- Exit handler: confirm → POST `/api/exit` → `window.close()`.
- CSS: `.iconbtn.danger` for Exit (not full-width form danger).
- Banner `#hive-banner` under the header, hidden by default.

Then: `./scripts/embed-ui.sh hush-c/demo`.

#### M3.6 Tests

`hush-c/tests/check_launch.sh` HTML greps:

- `id="hive-close"`
- `id="hive-exit"`
- `/api/exit`
- `/api/close`

New `hush-c/tests/check_exit.sh`:

1. Start `--no-open` on a throwaway port.
2. Assert pidfile exists.
3. `POST /api/close` → 200, session still answers.
4. `POST /api/exit` → 200, wait, port gone, pidfile gone, process exit 0.
5. Second run: `--quit` while up → process gone, exit 0.
6. `--close` with nothing up → exit 0.
7. `--help` mentions `--quit` and `--close`.

Wire into `hush-c/Makefile` `test` after `check_launch.sh`.

### Phase 4 — Verify, docs, land

- M4.1 `./configure && make clean && make && make test`
- M4.2 README "Close vs Exit" + help polish + write-legible-c §14
- M4.3 Final commit + push
- M4.4 `gh pr create` + `gh pr merge --auto --merge`
- M4.5 After MERGED: pull main, remove worktree, delete `gb/exit-close-design`

## 4. Audit (frozen)

- Every implementation Milestone has files, commands, and a verification.
- Phase 1 research gate is this commit (M1.4 + M2).
- Worktree lifecycle is the Prime Directive (PR, not local merge).
- In-app buttons are first-class, not a CLI footnote.

## 5–7. Execute → Audit → Confirm

Commit after every Milestone. State "Grok Build complete." only when the
PR is merged, the worktree is gone, and main is clean.

## Detailed design (locked)

### Shutdown flag

```c
static volatile sig_atomic_t g_shutdown = 0;

static void hush_shutdown_handler(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

void hush_relay_request_shutdown(void)
{
    g_shutdown = 1;
}
```

Handler is async-signal-safe. HTTP Exit calls the public setter.

### Poll loop

```
pr = poll(...)
if (pr < 0) {
    if (errno == EINTR) {
        if (g_shutdown) break;
        continue;
    }
    break;
}
if (g_shutdown)
    break;
```

The `for (;;)` loop is the intentional event pump (write-legible-c §5).

### Exit codes

| Path | Code |
|---|---|
| Clean run, Ctrl+C, SIGTERM, `--quit`, `/api/exit` | 0 |
| `--close`, `--help`, attach-only `--open` | 0 |
| Bind / store / unknown flag | 1 |

### Header chrome (Quinn)

```
[brand]     [Install?] [Profile] [Settings] [Call?] [Close] [Exit] [badge]
```

Close = ghost. Exit = danger. Both `min-height`/`min-width` 44px.

### Payne copy

- Close title: "Close the window. Hive stays standing."
- Exit title: "Quit the hive. Every process stops."
- Exit confirm: "Quit the hive? Every process stops."
- Close leftover banner: "Window stays open here. Close this window. The hive is still standing."
