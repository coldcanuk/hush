# RESEARCH — leave chooser at launch

Branch: `gb/leave-launch-zenity`
Worktree: `worktrees/leave-launch-zenity`
Base: `main` `9cf2406a8` (PR #36)
Date: 2026-08-18
Mode: TOOLED four-minds (`/trouble`)

Using the four-minds debug protocol.

## Mode

TOOLED. File and shell access used in this session.

## Phase 0 — Context Register

MCP / extensions in this session: skills, apps, buzz_publish, todo,
analyze, extensionmanager, summon, developer. Available to enable:
summarize, tom, code_execution, chatrecall, computercontroller,
autovisualiser, memory, tutorial.

Consulted: none. No docs or observability MCP covers Chromium `--app`,
zenity, or this C11 poll pump.

Data: the contract is local — `hush_relay.c`, UI_SPEC §10, live
`hush-relay --open`.
Sherlock: the docs MCP is silent on whether a PATH miss of
`chromium`/`brave-browser` is treated as "window closed." That silence
is the clue.
Linus: we do not enable computercontroller "for completeness."
Brian Cox: PR #35 (`5aec1f621`) landed the last-window zenity. The
complaint is after that merge. Cause precedes complaint.

## User report (quoted)

> something isn't right with the close/quit window. When I launch a new
> instance of the app, I get the cancel/close/quit window in the
> background. it buggers things. fix it.

What is broken: leave chooser (Cancel / Close / Quit) appears at launch,
in the background.
What the user tried: `[UNVERIFIED]` — they did not list prior attempts.
Files in play: `hush-c/src/hush_relay.c` (`hush_open_app_window`,
`hush_relay_watch_app`, `hush_leave_spawn`), live pid 328252.

## Phase 1 — Evidence

**E1** `hush_open_app_window` tracks the *first* fork and sets
`g_saw_app = 1` before any browser exists. Quoted
`hush-c/src/hush_relay.c:279-287`:

```
    pid = fork();
    if (pid < 0)
        return;
    if (pid > 0) {
        hush_relay_track_child(pid);
        g_saw_app = 1;
        return;
    }
    hush_exec_app_browser(url, app_arg);
```

**E2** `hush_exec_app_browser` tries PATH names first, then epiphany,
then Flatpak, then `xdg-open`. Quoted
`hush-c/src/hush_relay.c:293-317`:

```
        "chromium",
        "chromium-browser",
        "google-chrome",
        "google-chrome-stable",
        "brave-browser",
        …
    execlp("flatpak", "flatpak", "run", "com.brave.Browser",
           "--class=hush-relay", app_arg, (char *)NULL);
```

**E3** This host has no PATH browser matching the first loop. Quoted
command output:

```
bash: line 1: type: chromium: not found
bash: line 1: type: brave-browser: not found
flatpak is /usr/bin/flatpak
Brave	com.brave.Browser	1.92.141	stable	flathub	user
```

**E4** `hush_child_is_app` requires the tracked pid's `/proc/pid/cmdline`
to contain `--class=hush-relay` and `--app=http://127.0.0.1:<port>/`.
Quoted `hush-c/src/hush_relay.c:767-773` and `:1056-1064`:

```
    if (pid == g_leave_pid)
        return 0;
    return hush_child_cmdline_matches(pid, g_listen_port);
…
    if (strstr(line, "--class=hush-relay") == NULL)
        return 0;
    return strstr(line, needle) != NULL;
```

**E5** `hush_relay_watch_app` forgets dead children, then if `g_saw_app`
is set and no live app remains, it immediately forks zenity. Quoted
`hush-c/src/hush_relay.c:983-994`:

```
    hush_leave_forget_dead();
    hush_leave_poll();
    if (g_shutdown || g_leave_ack || g_leave_pid > 0)
        return;
    if (hush_leave_app_alive())
        return;
    if (!g_saw_app)
        return;
    g_saw_app = 0;
    hush_leave_spawn();
```

**E6** The first pump tick after announce is that watch. Quoted
`hush-c/src/hush_relay.c:614-617`:

```
        hush_agent_poll(g_store);
        hush_relay_watch_app();
        nf = hush_fill_pollfds(fds, ls);
        pr = poll(fds, (nfds_t)nf, HUSH_POLL_TIMEOUT_MS);
```

**E7** Isolated reproduction, this session. PATH stripped to a stub
`zenity` only. Current `hush-relay --open 18765`. After 1.5s:

```
=== zenity log ===
ZENITY_RAN
=== relay log ===
hush-relay 0.0.1
listening on http://127.0.0.1:18765/
…
=== children ===
    PID STAT CMD
```

Zenity ran with no `--app` child alive.

**E8** Live operator session at 22:32 has two Brave `--app` children
under the leftover relay. Quoted:

```
 328252       1  328251 SN   hush-relay --open
 328257  328252  328251 SN    \_ bwrap --args 73 -- brave --class=hush-relay --app=http://127.0.0.1:10555/
 328345  328252  328251 SN    \_ bwrap --args 74 -- brave --class=hush-relay --app=http://127.0.0.1:10555/
```

328345 started three seconds after 328257. Consistent with a first
window plus a Cancel re-attach.

**E9** In-page `#hive-leave` starts hidden (`.drawer { display: none }`)
and only `openLeave` adds `.show`. Quoted
`hush-c/demo/index.html:374-378` and `:2240-2249`:

```
    .drawer, .stage {
      display: none; …
    }
    .drawer.show, .stage.show { display: flex; }
…
    $("hive-close").addEventListener("click", openLeave);
    $("hive-exit").addEventListener("click", openLeave);
```

The launch-time chooser is therefore zenity, not the in-page drawer.

**E10** UI_SPEC §10 says zenity fires when the last `--app` child is
gone, not at launch. Quoted `UI_SPEC.md:96-99`:

```
- Last `--app` child gone: the relay notices with `kill(pid, 0)` in the
  poll pump (`SIGCHLD` stays ignored). If no `/api/close` or `/api/exit`
  ran this session (`g_leave_ack` clear) and `g_shutdown` is clear, it
  forks `zenity --question` (non-blocking) with the same three verbs.
```

### Smuggled assumptions

| Id | Claim | Status | Falsifier |
|---|---|---|---|
| A1 | The in-page `#hive-leave` drawer opens at load. | Falsified by E9. | `rg openLeave` — only Close/Exit. |
| A2 | The operator clicked Close/Exit at launch. | Unverified. | Ask; not required — E7 reproduces without a click. |
| A3 | `g_saw_app` means a live `--app` window. | Falsified by E1+E4+E7. | Repro already ran. |
| A4 | Brave Flatpak exec is instant enough to beat the first pump. | Falsified by E3+E7. | Isolated PATH. |

Data: E7 is the cheapest experiment and it already ran.
Sherlock: Data, you claim the PATH miss is the whole story — why does
E8 show two live Brave windows *now*? Because Cancel re-opens. That
does not falsify the launch race; it is the race's second half.
Linus: Sherlock, stop. E7 is the machine. The cheapest falsifier already
fired zenity with no browser. That is the bug.
Brian Cox: No objection on the arrow. Announce (`T0`) → first
`watch_app` (`T0+ε`) → child still `execlp`-ing → `g_saw_app` but not
`hush_child_is_app` → zenity. Brave arrives later (`T0+3s` in E8).
Later cannot cause earlier.

Linus strikes "user is annoyed" as `[not evidence]`. Kept: E1–E10.

## Phase 2 — Hypotheses

**H1 — First-fork death is treated as last-window-gone.**
`g_saw_app` is set on the helper pid (E1). That pid is not an `--app`
cmdline (E4) and dies as soon as `execlp("chromium")` fails (E2, E3).
First `watch_app` (E5, E6) therefore spawns zenity (E7).
Explains: E1–E8, E10. Does not explain: a chooser that is the HTML
drawer (E9 says it is not).
Falsifier already run: E7.
Data **9/10**. Linus **9/10**. Sherlock **8/10** (A2 unused). Brian **9/10**.

**H2 — `#hive-leave` is shown on first paint.**
Explains: "cancel/close/quit window" wording.
Does not explain: E9 (hidden until click), E7 (zenity stub ran with no
HTML involved).
Sherlock: a theory that needs a click the repro did not make. **2/10**.

**H3 — Leftover listener from a prior session raises zenity on attach.**
`hush_relay_bind` on EADDRINUSE forks a new window and returns without
entering the pump (`hush_relay.c:546-553`). Attach cannot spawn zenity.
Does not explain: E7 (fresh bind, empty port).
Linus **1/10**.

**H4 — Agent/provider child pids confuse `hush_leave_app_alive`.**
`hush_child_is_app` filters by cmdline (E4). Agent jobs do not match.
Does not explain: E7 (no agent, zenity still ran).
Data **1/10**. Mechanism never observed on this path.

**H5 — Zenity from a previous session is still up and looks like launch.**
Explains: a leftover dialog.
Does not explain: E7 (fresh stub logged `ZENITY_RAN` after this
`--open`). Live `pgrep zenity` at diagnosis was empty.
Brian Cox: time reversal if we blame a later leftover for a launch
dialog. **2/10**.

## Phase 3 — Bayes (H1, H2, H3)

Priors (base rates after PR #35):

| H | Prior | Why |
|---|---|---|
| H1 | 0.70 | New last-window sensor + PATH-less Flatpak host. |
| H2 | 0.15 | User wording matches the HTML drawer. |
| H3 | 0.15 | Leftover listeners are common here (prior /trouble). |

Likelihoods, citing evidence:

- P(E \| H1) = 0.95 — E1+E4+E5+E7 are the mechanism on the page.
- P(E \| H2) = 0.10 — E9 hides the drawer; E7 is zenity.
- P(E \| H3) = 0.15 — attach never enters the pump.

Unnormalized:

- H1: 0.70 × 0.95 = 0.665
- H2: 0.15 × 0.10 = 0.015
- H3: 0.15 × 0.15 = 0.0225
- sum = 0.7025

Posteriors:

- P(H1 \| E) = 0.665 / 0.7025 = **0.947**
- P(H2 \| E) = 0.015 / 0.7025 = **0.021**
- P(H3 \| E) = 0.0225 / 0.7025 = **0.032**

## Phase 4 — Second debate

Data: H1 dominates by 0.915. The numbers justify a scoped latch, not a
chooser rewrite. **8/10** on scoped fix (cap: verification still pending).
Sherlock: the highest-VoI leftover would be "does zenity still fire if
we only set `g_saw_app` after `hush_child_is_app`?" That *is* the fix
and the test. Waiting for more evidence is waste. **8/10** proceed.
Linus: H1's diff is smaller than H2's (no HTML) and H3's (no attach
rewrite). Cheap falsifying deploy *is* the test. **9/10**.
Brian Cox: the arrow is launch → dead helper → zenity → later Brave.
A static latch on "have we ever seen a live `--app` cmdline" matches
that timeline. **8/10**.

Agreement: all four yes on H1.

## Phase 5 — Problem & scope

**Problem.** `g_saw_app` is set when the launcher fork is born, not when
an `--app` window is alive. On this host the first `execlp` names are
missing, so that fork dies before Flatpak Brave replaces it. The first
poll tick treats "saw a child, none alive, none is `--app`" as last
window gone and raises zenity. The operator sees Cancel / Close / Quit
behind the new window. Cancel then opens a second window (E8).

**Scope of work.** Latch `g_saw_app` only when `hush_child_is_app` is
true for a live tracked pid. Do not spawn zenity until that latch has
been true and then gone. Keep last-window zenity after a real `--app`
death without `/api/close`/`/api/exit`. Do not change `#hive-leave`,
attach-on-EADDRINUSE, Exit/Close HTTP, or SIGCHLD=`SIG_IGN`.

## Phase 6 — Plan

See [`../plan/PLAN_LEAVE_LAUNCH_ZENITY.md`](../plan/PLAN_LEAVE_LAUNCH_ZENITY.md).

## Unanimous agreement gate

Data: **yes**. **8/10** — mechanism quoted; fix not yet executed.
Sherlock: **yes**. **8/10** — E7 already is the falsifier inverted.
Linus: **yes**. **8/10** — one predicate change, one shell assertion.
Brian Cox: **yes**. **8/10** — causal order is launch, not leftover.

Pass. Implement on `gb/leave-launch-zenity` only.
