# RESEARCH — lingering hush-relay + OS window × dialog

Branch: `gb/close-x-dialog`
Worktree: `worktrees/close-x-dialog`
Base: `main` `b41b7f71c` (PR #34 thread-think-hygiene)
Date: 2026-08-18
Mode: TOOLED four-minds (`/trouble`)

Using the four-minds debug protocol.

## Phase 0 — Context Register

MCP servers in this session: skills, apps, buzz_publish, todo, analyze,
extensionmanager, summon, developer. Available to enable: summarize, tom,
code_execution, chatrecall, computercontroller, autovisualiser, memory,
tutorial.

Consulted: none. No documentation or observability MCP covers Chromium
`--app` window-close, GTK, or this C11 poll server.

Data: no docs MCP exists for the window-manager × path. The contract is
local: UI_SPEC §10 / §18, `hush_relay.c`, `index.html`.
Sherlock: the docs MCP is silent on whether Brave `--app` fires
`beforeunload` for the OS ×. That silence is itself a clue — we cannot
cite a library; we must cite this process and this HTML.
Linus: we do not enable computercontroller "for completeness." Live `ps`
and the source are enough.
Brian Cox: the leftover listener started at 20:20:18, this diagnosis at
20:53. The process is older than the question. Cause precedes complaint.

## User report (quoted)

> I still have a lingering process problem
> `chuck@kiff:/opt/repo/hush$ ps aux | grep hush | grep -v grep | awk '{print $2}' | xargs kill -9`
> I still have to run this before running a new build. Is it because I'm
> not clicking the exit button and only click the x button? when someon
> clicks the x button a window should appear with three choices: Exit the
> application, Close the window or Cancel

What is broken: leftover `hush-relay` forces `kill -9` before a new
binary can bind `:10555`.
What the user believes: they click the window ×, not Exit. Tagged
`assumption` until evidence.
Files in play: `hush-c/demo/index.html`, `hush-c/src/hush_relay.c`,
`hush-c/src/hush_http.c`, `UI_SPEC.md` §10/§18, live pid 307857.

## Phase 1 — Evidence

**E1** Live leftover, 2026-08-18 20:53, quoted:

```
chuck     307857  0.0  0.0  48748  9300 ?        SN   20:20   0:00 hush-relay --open
LISTEN 0      8            0.0.0.0:10555      0.0.0.0:*    users:(("hush-relay",pid=307857,fd=3))
    PID    PPID     SID TT       STAT CMD
 307857       1  307856 ?        SN   hush-relay --open
lrwxrwxrwx 1 chuck chuck 0 Aug 18 20:20 /proc/307857/cwd -> /home/chuck
```

**E2** Child Brave `--app` still parented to the leftover relay:

```
chuck     307861  0.0  0.0   3592  2120 ?        SN   20:20   0:00 bwrap --args 73 -- brave --class=hush-relay --app=http://127.0.0.1:10555/
    PID    PPID     SID TT       STAT CMD
 307861  307857  307856 ?        SN   bwrap --args 73 -- brave --class=hush-relay --app=http://127.0.0.1:10555/
```

The Brave window is still alive. This is not "window gone, relay left."
It is "hive still running from 20:20." The operator's `kill -9` habit
fires when they want a *new build* to take the port, not only after ×.

**E3** Mapped binary is the installed copy, same inode age as the main
tree binary (PR #34 era), not an older image:

```
/proc/307857/exe -> /home/chuck/.local/bin/hush-relay
2026-08-18 20:19:31.934378503 -0400 /home/chuck/.local/bin/hush-relay 367064
2026-08-18 20:19:31.931235881 -0400 /opt/repo/hush/hush-c/hush-relay 367064
STARTED Tue Aug 18 20:20:18 2026
```

**E4** UI_SPEC §10, lines 67–83, quoted:

```
The OS/PWA window `×` is **not** our Close and **not** our Exit.
…
| **Close** | `#hive-close` | Detach the GUI. Relay stays up. …
| **Exit**  | `#hive-exit`  | Quit. Every process stops. Exit code 0. |
…
- Close: `POST /api/close` then `window.close()`.
- Exit: confirm "Quit the hive? Every process stops." then
  `POST /api/exit` then `window.close()`.
```

**E5** UI_SPEC §18, lines 373–380, quoted:

```
Exit / `--quit` / SIGTERM call `hush_relay_reap_children()` from
`hush_relay_cleanup`. …
Close and attach never reap. Attach copy:
“This is the process already listening. Exit or hush-relay --quit
before a new install can take the port.”
```

**E6** README Close vs Exit, quoted:

```
These are two different verbs. The OS/PWA window `×` is neither of them.
…
If `--open` attaches to a leftover listener, quit that process before a
new install can take the port.
```

**E7** `index.html` Close / Exit handlers (lines 2178–2188). No
`beforeunload`, no `pagehide`, no `visibilitychange`. `rg` of those
tokens in `hush-c/demo/index.html` and `hush-c/src` returned empty.

```
$("hive-close").addEventListener("click", async () => {
  try { await api("/api/close", {}); } catch (err) { /* still close the window */ }
  window.close();
  const banner = $("hive-banner");
  if (banner) banner.classList.add("show");
});
$("hive-exit").addEventListener("click", async () => {
  if (!confirm("Quit the hive? Every process stops.")) return;
  try { await api("/api/exit", {}); } catch (err) { /* process may already be gone */ }
  window.close();
});
```

**E8** Bind-on-EADDRINUSE attach (`hush_relay.c` 506–513):

```
if (errno == EADDRINUSE && open_ui) {
    fprintf(stdout,
            "hush-relay already running on http://127.0.0.1:%u/ — opening UI...\n"
            "This is the process already listening. Exit or hush-relay --quit "
            "before a new install can take the port.\n",
            (unsigned)port);
    hush_open_app_window(port);
    return HUSH_OK;
}
```

A new `hush-relay --open` after `make install` does **not** replace the
listener. It opens another Brave window onto the old process.

**E9** `hush_http_serve_close` is ack-only. `hush_http_serve_exit` sets
`g_shutdown`. Quoted:

```
static hush_status_t hush_http_serve_close(int fd)
{
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_CLOSE_JSON, sizeof(HUSH_HTTP_CLOSE_JSON) - 1);
    return HUSH_OK;
}
static hush_status_t hush_http_serve_exit(int fd)
{
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_EXIT_JSON, sizeof(HUSH_HTTP_EXIT_JSON) - 1);
    hush_relay_request_shutdown();
    return HUSH_OK;
}
```

**E10** `check_exit.sh` already asserts Close leaves the relay up and
Exit reaps a fake `--class=hush-relay` child. Quoted:

```
curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null \
    || fail "close must leave the relay up"
…
if kill -0 "$fake_pid" 2>/dev/null; then
    fail "exit left a --app child running"
fi
```

**E11** GUI is Chromium-family `--app`, not a GTK/Qt toplevel. Quoted
from `hush_exec_app_browser`:

```
execlp(browsers[i], browsers[i],
       "--class=hush-relay", "--name=Hush", app_arg, (char *)NULL);
```

Live child: `brave --class=hush-relay --app=http://127.0.0.1:10555/`.
There is no Hush-owned window manager. The OS × belongs to Brave.

**E12** Chromium `--app` window × destroys the renderer. Page JS cannot
replace the OS close-box with a custom three-button window. `beforeunload`
can only show the *browser's* "Leave site?" confirm (two buttons: Leave /
Stay), and Chromium often suppresses even that unless the page has
unsaved input. It cannot present "Exit / Close / Cancel". [UNVERIFIED as
a live Brave click on this host; verified as the HTML/C contract and as
the WHATWG/Chromium limitation that no API exists to customize the OS ×.]

**E13** Desktop file already has a real Exit action:

```
Actions=Quit;
[Desktop Action Quit]
Name=Quit Hush
Exec=hush-relay --quit
```

The window × is still Brave's.

**E14** Stale pidfile from a previous test is not the live leftover:

```
/run/user/1000/hush/relay-10555.pid  → 307857 (alive)
/run/user/1000/hush/relay-18767.pid  → 312782 (dead)
```

Linus: E14 is not the user's `kill -9` problem. Strike as a side fact.

### Smuggled assumptions

- **A1** "I only click the ×, never Exit." User belief. Falsifier: we
  cannot read the operator's last click. Smallest check: if Brave child
  is still parented (E2), they have *not* closed the window either.
  E2 **falsifies** "I already clicked × and the window is gone" for
  *this* leftover. It does **not** falsify the general habit.
- **A2** "The OS × can host a custom three-choice window." Falsified by
  E11 + E12: we do not own that chrome.
- **A3** "Close should kill the relay." Contradicted by E4, E5, E6, E10.
  User now asks for × to *offer* Exit or Close, not to collapse them.
- **A4** "A leftover always means Exit is broken." Falsified by E8 + E9:
  Close and × are designed to leave the listener. Exit *does* die
  (`check_exit.sh`).
- **A5** "`beforeunload` can show Exit / Close / Cancel." Falsified by
  the browser API: one generic string, two OS buttons, often suppressed.

Sherlock on A1: "Data, you claim the leftover is the designed Close
path, but E2 shows the window is *still open*. Why does the operator
need `kill -9` if they have not even closed the window?"
Linus: because the next `make && ./hush-relay --open` hits EADDRINUSE
(E8) and attaches. They kill to free the port for a new binary. The ×
habit is the *usual* way they think they quit; today's snapshot also
covers "I just want a new build and the old hive is still up."
Brian Cox: two timescales. T0 launch 20:20. T1 they work. T2 they
build again and the port is taken. The × may have been used on *earlier*
sessions. This session the window is still up. Both paths need the same
fix: make the × (or last-window-gone) ask, and keep Exit as the only
kill.

## Phase 2 — Hypotheses

**H1 — Designed Close/× leave the listener; operator uses × or just
rebuilds; `--open` attaches (E8). No intercept exists (E7).**
Explains: E1, E4, E5, E6, E7, E8, E9, the user's `kill -9` ritual.
Does not explain: why E2's Brave is still up *right now* (they may not
have clicked × this session).
Falsifier: if `/api/exit` left pid 307857 alive, H1 is wrong.
Cheapest: `curl -s -X POST http://127.0.0.1:10555/api/exit` would kill
it — do **not** run that as a diagnostic on the operator's live hive
without asking. `check_exit.sh` already proves the path.
Data **8/10** this is the mechanism. Sherlock **7/10** (A1 unverified
as the *only* click). Linus **9/10** on "this is why the port is busy."

**H2 — Exit is broken: `/api/exit` or reap fails, so even Exit leaves
the process.**
Explains: a leftover after a *claimed* Exit.
Does not explain: E10 (test asserts death + reap), E9 (sets `g_shutdown`),
E2 (Brave still child — Exit would have reaped it).
Falsifier: `check_exit.sh` already ran on this binary family.
Linus: **2/10**. Do not "fix" Exit.

**H3 — Unreaped grok / xterm children hold the port, not hush-relay.**
Explains: a leftover that is not `hush-relay`.
Does not explain: E1 (`hush-relay` itself listens on 10555).
Brian Cox: **1/10**. Wrong particle.

**H4 — We can intercept the OS × with `beforeunload` and paint a custom
three-button modal before the window dies.**
Explains: the user's requested UX if the API existed.
Does not explain: E11, E12. The OS × destroys the document; a page modal
never wins that race. `beforeunload` is not three labeled buttons.
Sherlock: a theory that requires a browser API we do not have is not a
fix. **2/10** as a mechanism for *this* chrome.
Linus: **0/10** as the implementation. Do not ship a fake `beforeunload`
modal and call it the OS ×.

**H5 — Last Brave `--app` child exit can be observed by the relay, which
then shows a *native* three-choice dialog (zenity) or, if we cannot
intercept ×, we put the same three choices in-page on a dedicated close
intent and on last-child-gone.**
Explains: E11 (we own the parent, not the chrome). Explains the user's
three verbs without lying about the OS ×.
Does not explain: SIGCHLD is currently `SIG_IGN` (`hush_relay.c:482`),
so the parent does not currently notice the child dying. That is a
missing sensor, not a contradiction.
Falsifier: after flipping SIGCHLD to a reap-wait and tracking the
`--app` pid, killing the Brave window should be visible in the relay.
Data **7/10** as the *implementable* intercept. Linus **6/10** until we
admit last-child-gone fires *after* the window is already gone — the
zenity dialog then appears as a *follow-up*, not as a blocker on the ×.
That is still the honest UX: "The window closed. Exit the hive, leave
it standing, or I didn't mean to."

## Phase 3 — Bayes (top three: H1, H4, H5)

Priors (base rates in this repo + this leftover):

| H | Prior | Why |
|---|---|---|
| H1 | 0.55 | Prior slices already documented this (RESEARCH_PILLS, UI_SPEC §10). Live E1+E8 match. |
| H4 | 0.10 | User's requested mechanism. Low because Chromium does not offer it. |
| H5 | 0.35 | Implementable intercept at the process we own. |

Likelihoods P(E | H):

- P(E | H1) = 0.95 — leftover listener + attach copy + no intercept is
  exactly the designed world (E1, E4–E9).
- P(E | H4) = 0.20 — if a custom × modal already worked we would not
  see a silent leftover after ×; also E7 shows no handler at all.
- P(E | H5) = 0.70 — compatible with the leftover (sensor off today)
  and with E11 (parent can watch the child). Slightly lower than H1
  because last-child-gone is a *fix path*, not the current cause.

Unnormalized:

- H1: 0.55 × 0.95 = 0.5225
- H4: 0.10 × 0.20 = 0.0200
- H5: 0.35 × 0.70 = 0.2450
- sum = 0.7875

Posteriors:

- P(H1\|E) = 0.5225 / 0.7875 = **0.663**
- P(H4\|E) = 0.0200 / 0.7875 = **0.025**
- P(H5\|E) = 0.2450 / 0.7875 = **0.311**

H1 is the *cause*. H5 is the *lever*. H4 is rejected as a mechanism.

```
P(H_i | E) = P(E | H_i) * P(H_i) / sum_j [ P(E | H_j) * P(H_j) ]
```

## Phase 4 — Second debate

Data: H1 leads by 0.663 − 0.311 = 0.352. The cause is not a broken
Exit. The numbers justify a scoped UX change, not a reap rewrite.
**8/10** on diagnosing; **6/10** on any unverified fix (hard cap).

Sherlock: "What single piece of evidence would move these most?" A
recorded last click. We will not get it. Next-best: after we ship the
dialog, the operator no longer needs `kill -9`. Worth implementing
without waiting. **7/10** do not stall.

Linus: H1's implied *cause* fix is "stop attaching / auto-exit on ×" —
that collapses Close into Exit and I veto it (prior RESEARCH_PILLS
already locked this). H5's implied fix is a small in-page chooser plus
an optional last-child zenity. That is the smallest *honest* diff that
matches the three verbs. Do **not** add a GTK window manager. Do **not**
touch `hush_agent`. **8/10** on smallest-diff path.

Brian Cox: the arrow is leftover listener → next `--open` attaches →
operator `kill -9` to free the port. Last-child-gone is *after* the ×,
so a zenity follow-up is causally late but still on the geodesic from
"I clicked × and thought I quit." In-page three-choice on a *synthetic*
close (we intercept in-page attempts; we cannot intercept the OS ×)
plus last-child-gone covers both timescales. **8/10**.

Agreement: cause = H1. Implementation lever = H5 (in-page chooser that
the OS × cannot host, plus last-window-gone native prompt). Reject H4
as the × interceptor. Preserve Close ≠ Exit.

Disagreement recorded: Linus wants last-child zenity only if we can
keep it out of the C hot path and skip it when Exit already ran.
Sherlock wants the in-page dialog to also be what `#hive-close` opens,
so Close is no longer a silent detach. Data accepts both. Brian Cox
accepts last-child-gone as a follow-up, not a blocker.

## Phase 5 — Problem & scope (plain)

The leftover is the designed Close/attach world plus a window × that
Hush does not own. `hush-relay --open` binds or attaches; it never
replaces a live listener. Exit (`#hive-exit`, `--quit`, SIGTERM)
already dies and reaps. The operator's `kill -9` is how they free
`:10555` after they dismiss the Brave window (or after they leave it
up and rebuild). The OS × cannot host Exit / Close / Cancel.

What will change: (1) a hive-owned three-choice dialog
(`#hive-leave`) with Exit the application / Close the window / Cancel;
(2) the OS × cannot be customized, so we watch the last `--app` child
and, when it dies without an Exit in flight, raise a native zenity
question with the same three verbs; (3) `#hive-close` and a documented
in-page path open `#hive-leave` instead of silently detaching;
(4) UI_SPEC §10 is revised so the × is *answered*, not claimed as our
chrome. What will not change: Close still does not kill the relay;
Exit still does; attach on EADDRINUSE stays; no tray; no GTK rewrite;
no `beforeunload` fake modal; no auth on `/api/close` or `/api/exit`.

## Unanimous agreement gate (research / plan only)

No C/HTML fix in this commit. Gate is on freezing RESEARCH + PLAN.

- Data: **yes**. **8/10** — evidence is quoted; fix is not yet run.
- Sherlock: **yes**. **7/10** — A1 stays assumption; H4 rejected.
- Linus: **yes**. **8/10** — smallest honest UX; Close stays Close.
- Brian Cox: **yes**. **8/10** — timeline holds; last-child is late
  but on the geodesic.

## Architecture lock (for PLAN.md)

1. **`#hive-leave`** — existing `.drawer` / `.panel` pattern. Title
   "Leave the hive?". Three Fitts ≥44px buttons:
   - `#leave-exit` — same path as `#hive-exit` (confirm already in
     the chooser; do not double-confirm). `POST /api/exit`, `window.close()`.
   - `#leave-close` — same path as today's `#hive-close`. `POST /api/close`,
     `window.close()`, banner if the window stays.
   - `#leave-cancel` — hide the drawer. Nothing posted.
2. **`#hive-close`** opens `#hive-leave` (does not silently close).
   **`#hive-exit`** may keep its one-shot confirm *or* also open
   `#hive-leave` with Exit pre-emphasized. Prefer one chooser for both
   so Hick stays one decision. Decision: both Close and Exit on the
   rail open `#hive-leave`. Rail labels stay.
3. **Last `--app` child gone:** flip `SIGCHLD` from `SIG_IGN` to a
   handler that only sets a flag (or use `waitpid(WNOHANG)` in the
   pump — prefer pump wait so we stay signal-safe). When the last
   tracked `--app` pid is gone and `g_shutdown` is clear, fork
   `zenity --question` (or a three-button `--list` / two questions)
   with Extra/Cancel. Map:
   - Exit the application → `hush_relay_request_shutdown()`
   - Close the window → no-op (already closed)
   - Cancel → `hush_open_app_window` (re-attach)
   `zenity` is present (`/usr/bin/zenity` 4.0.1). If zenity is
   missing, print the attach hint and leave the hive standing
   (today's Close semantics).
4. **Do not** use `beforeunload` as the three-choice UI.
5. **Do not** reap on Close or on last-child-gone Close/Cancel.
6. Tests: HTML greps for `#hive-leave`, `#leave-exit`, `#leave-close`,
   `#leave-cancel`. `check_exit.sh` keeps Close-stays / Exit-dies.
   New assertion: POST close still leaves the port; the new dialog
   does not change those verbs.

## Non-goals

- Replacing Brave `--app` with a GTK/Qt shell.
- System tray.
- Auto-Exit when the last window closes (that is choosing Exit *for*
  the operator).
- Changing attach-on-EADDRINUSE.
- Authenticated control API.
- Touching hush_agent / thread pane / OAuth / rail docks.

## Risks

1. Last-child-gone zenity appears after the window is gone — operator
   may miss it if they already walked away. Mitigation: also put the
   chooser on `#hive-close` / `#hive-exit` so the in-app path is
   obvious; README says the OS × cannot be ours.
2. `SIGCHLD` change interacts with `hush_agent` / `hush_pass` /
   `hush_provider` waitpid. Mitigation: do **not** install a
   catching handler that steals those waits; poll `waitpid(WNOHANG)`
   only for tracked `--app` pids in the pump. Keep `SIGCHLD` as
   `SIG_IGN` if that is safer, and detect death via `kill(pid, 0)`.
   **Lock: detect with `kill(pid, 0)` in the pump. Leave SIGCHLD
   ignored.** Linus wins this one.
3. zenity blocks if run in the poll thread. Mitigation: fork + track
   like the browser; read the exit status on the next pump ticks.
4. Double dialog if they click Close *and* the window dies. Mitigation:
   a `g_leave_prompted` / JS `leaveOpen` flag; native prompt only when
   the window died *without* a recent `/api/close` or `/api/exit`.
5. Hick: three verbs on × plus two rail buttons. Mitigation: rail
   buttons become the same chooser.

## Synthesis

Cause H1 (P=0.663): leftover listener is Close/attach by design; no ×
intercept exists. Lever H5 (P=0.311): hive-owned `#hive-leave` plus
last-`--app`-child `kill(pid,0)` + zenity follow-up. Reject customizing
the OS × (H4 P=0.025). Close never kills. Exit always does.

Plan frozen in `PLAN_CLOSE_X_DIALOG.md`.
