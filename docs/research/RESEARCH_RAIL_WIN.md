# RESEARCH — Minimize, window chrome, rail v2

Methodology: **RDAP** Phase 1. Four-minds `/trouble` lock.
Worktree: `/opt/repo/hush/worktrees/rail-win`
Branch: `gb/rail-win`
Base: `main` `6227ad7db`

## Mode

**TOOLED.** Shell, files, live hive, X11 headers, COSMIC session.

## Phase 0 — Context Register

Surveyed session extensions: analyze, apps, buzz_publish, developer,
extensionmanager, skills, summon, todo. Unused and not consulted:
summarize, tom, code_execution, chatrecall, computercontroller,
autovisualiser, memory, tutorial.

No documentation or observability MCP is relevant. COSMIC's
`org.freedesktop.impl.portal.desktop.cosmic` exposes Access,
FileChooser, ScreenCast, Screenshot, Settings — **not** a window
minimize / undecorate API. `[M1]`

Goose-doc-guide does not apply (not a goose recipe or provider
config).

**Data:** The portal listing is the cheapest falsifier of "ask the
desktop portal to iconify us." It cannot.
**Sherlock:** What is the portal conspicuously silent on? Foreign
toplevels. That silence is the clue: page JS and xdg-desktop-portal
cannot do this job.
**Linus:** We do not read Wikipedia before fixing a no-op click.
Xlib is on disk. Use it.
**Brian Cox:** The live Brave process is ozone-wayland *now*. Any
X11 geodesic only exists after we change the launch argv. Time
ordering matters: undecorate cannot run against today's window.

## Symptom (operator)

1. The Minimize button does not work. Fix it.
2. After min/max work, remove the standard top bar with the `x`
   (operator called this "PWA windowless").
3. New tool-rail mock-up: Install+`i`, Profile/Settings, Call/Invite+`i`,
   Add Channel+`i`, New Robot+`i` / New Project+`i`, Minimize/Maximize,
   Close/Exit.
4. Delete the left-nav **Create** section.

## Evidence

**E1** `#rail-min` does not iconify. It collapses the rail, and
returns immediately while a thread is parked.

```
$("rail-min").addEventListener("click", () => {
  if (railParked) return;
  setRailCollapsed(true);
  saveRail();
});
```

`hush-c/demo/index.html` 2604–2608. `railParked` is set true by
`parkRailForThread()` (3703–3714) whenever `#thread-pane` is open.

**E2** `#rail-max` is the Fullscreen API, errors swallowed.

```
if (document.fullscreenElement)
  await document.exitFullscreen();
else
  await document.documentElement.requestFullscreen();
```

`index.html` 2609–2616. Comment: "some --open browsers refuse
fullscreen".

**E3** Prior lock (PR #48) *defined* Minimize as rail collapse.

`UI_SPEC.md` 485–487: "Minimize (`#rail-min`, collapses the rail to
the hamburger) | Maximize (`#rail-max`, Fullscreen API)".
`docs/plan/PLAN_THREAD_UX.md` 18: "**Out.** … Real OS window
minimize."

That lock is what the operator is now rejecting.

**E4** `--open` already claims "no browser chrome" and still leaves
the OS title bar. `hush_relay.c` 564: `standalone app window (no
browser chrome)`. `README.md` 166–167: `--app=` "with no browser tab
strip or URL bar." `UI_SPEC.md` 72–74: "The OS/PWA window `×` belongs
to the Chromium-family `--app` window. It is **not** our chrome.
Page JS cannot put three labeled buttons on that close-box."

**E5** Manifest is already `"display": "standalone"`
(`hush-c/demo/manifest.webmanifest:8`). That applies to an
*installed* PWA, not to `hush-relay --open`. Install is gated on
`beforeinstallprompt` (`index.html` 3901–3911) and is often never
fired for `http://127.0.0.1`.

**E6** Live hive (2026-08-19 18:57 local): pid 415003
`hush-relay --open` → `/home/chuck/.local/bin/hush-relay` (452000
bytes, contains `paintThreadStream` and `rail-min` — PR #48 is
loaded). Child: `brave --class=hush-relay --app=http://127.0.0.1:10555/`.
GPU process: `--ozone-platform=wayland`. `DISPLAY=:1`
`WAYLAND=wayland-1` `XDG_CURRENT_DESKTOP=COSMIC`.
`xprop -root _NET_CLIENT_LIST` is empty. The `--app` window is not
an X11 client.

**E7** `libx11-dev` and `Xlib.h` are installed. `hush-relay` does
not link `-lX11`. `configure` does not probe X11.

**E8** Left-nav Create block (`index.html` 616–624): `#new-chan`,
`#add-chan`, `#new-proj`, `#add-proj`, `#raise-agent`,
`#invite-human`. Handlers: 1687, 2062, 2235, 2308. Drawers
`#agent-drawer` and `#member-drawer` stay. `check_launch.sh` 41–42
greps the strings `Raise a robot` and `Invite human` (drawer copy,
not the nav label).

**E9** No `/api/window`. POST dispatch ends at provider login
(`hush_http.c` 1299–1304). Close/Exit already exist as
`/api/close` and `/api/exit`.

**E10** Chromium `--app=` hides tab strip and URL bar. It does not
strip WM decorations. `display: fullscreen` / kiosk would hide the
bar by covering the session — that is not a window. The honest
name is **frameless** (Motif `_MOTIF_WM_HINTS` decorations=0), not
"windowless PWA".

## Smuggled assumptions

| Id | Claim | Tag | Falsifier |
|---|---|---|---|
| A1 | Minimize is broken because the click never fires | assumption | E1 quotes a listener. Falsified. |
| A2 | `display: standalone` already removed the `×` | assumption | E5+E6: live Brave still has OS chrome. |
| A3 | Page JS can hide the OS title bar | assumption | E4 UI_SPEC already said no. |
| A4 | COSMIC portal can iconify us | assumption | M1: no such interface. |
| A5 | X11 can see today's Brave window | assumption | E6 `_NET_CLIENT_LIST` empty. |
| A6 | Forcing `--ozone-platform=x11` will map via XWayland | unverified | Launch and `xprop` the new class. |
| A7 | COSMIC honors Motif decorations=0 on XWayland | unverified | After launch, decorations gone or not. |
| A8 | Maximize should stay Fullscreen API | assumption | Operator listed it next to Minimize as a window control. |

**Sherlock on A6/A7:** those two are the only remaining suspects
after the lock. If ozone-x11 maps and Motif is ignored, we still
have working min/max and a leftover `×`. That is a residual, not a
reason to skip the lock.

## Timeline

1. PR #48 defined Minimize = collapse rail (E3).
2. Operator restarted; live binary now includes that handler (E6).
3. Operator pressed Minimize, including almost certainly with a
   Thread open (`railParked` → no-op, E1).
4. Operator still sees the OS `×` (E4, E6 ozone-wayland).
5. Operator asked for rail v2 + delete Create.

No time-reversal. The "broken" button is doing exactly what #48
coded.

## Hypotheses

**H1 — Wrong verb.** Minimize is rail-collapse (and a parked
no-op). The operator wants WM iconify. Explains E1, E3, the
symptom. Does not explain the leftover `×` (that is H4).
Falsifier: press Minimize with no thread open; rail collapses,
window stays. **Data 8/10** on this being the click bug.

**H2 — Handler dead.** Overlay / missing node. Does not explain
the quoted listener (E1). **Linus: [not evidence] for a second
theory.**

**H3 — Maximize also dead.** Fullscreen refused inside `--app`
(E2 catch). Possible but not the stated bug. Falsifier: press
Maximize, see whether `fullscreenElement` is set.

**H4 — Chrome leftover is `--app` decorations, not missing
standalone.** Explains E4, E5, E6, E10. Falsifier: Motif
undecorate after ozone-x11; `×` gone or still there.

**H5 — Need a real installed PWA / window-controls-overlay.**
Would not fix `--open` (E5 never fires on localhost). Larger than
one module. **Linus veto.**

## Bayes (top three)

Priors from this codebase: last rail slice *deliberately* refused
OS minimize (E3), so H1 is the base-rate favorite. H4 is almost
certain for the `×` but is a second lever. H3 is a side issue.

| H | Prior | P(E\|H) | Unnormalized | Posterior |
|---|------:|--------:|-------------:|----------:|
| H1 wrong verb / parked no-op | 0.70 | 0.95 | 0.665 | **0.824** |
| H4 leftover `--app` decorations (for the `×`) | 0.20 | 0.70 | 0.140 | **0.174** |
| H3 fullscreen refused | 0.10 | 0.02 | 0.002 | **0.002** |

```
P(H1|E) = 0.665 / 0.807 = 0.824
P(H4|E) = 0.140 / 0.807 = 0.174
P(H3|E) = 0.002 / 0.807 = 0.002
```

H1 is the click bug. H4 is the chrome bug. They are not rivals;
they are two geodesics that share one launch change
(`--ozone-platform=x11`) so X11 can see the window.

## Phase 4 — Second debate

**Data:** Margin on H1 is 0.824 − 0.174 = 0.650. Numbers justify a
scoped fix. **7/10** (cap: live iconify not yet observed).
**Sherlock:** The single highest-VoI check is `xprop` on a new
`--ozone-platform=x11` window showing `WM_CLASS = hush-relay`.
Worth doing *as the implementation*, not as a delay. **7/10**.
**Linus:** H1's implied extra C is one small `hush_win.c` + one
POST. H5 is a PWA rewrite. Pursue H1+H4. Do not add libwayland.
**8/10** on smallest-diff.
**Brian Cox:** Arrow of time supports H1 (we shipped collapse last
slice; operator pressed it). Forcing ozone-x11 *changes* the
universe the next launch lives in. Residual A6/A7 stay labeled.
**7/10**.

Agreement: execute H1 (real minimize/maximize) and H4 (Motif
frameless) on the same ozone-x11 launch. Rail v2 is product, not
a rival hypothesis.

## Architecture lock

### Window controls

New module `hush_win.c` / `hush_win.h`, compiled when configure
finds `X11/Xlib.h`. `-DHUSH_HAVE_X11=1 -lX11`. Without X11 the
module becomes three stubs that return `HUSH_ERR_IO`.

Public:

- `hush_win_minimize(void)` — `IconifyWindow` on the first
  client whose `WM_CLASS` is `hush-relay`.
- `hush_win_maximize(void)` — toggle
  `_NET_WM_STATE_MAXIMIZED_VERT` + `_HORZ` (not Fullscreen API).
- `hush_win_undecorate(void)` — `_MOTIF_WM_HINTS` decorations=0.

Find-window is one helper. Display is opened and closed per call
(no long-lived X connection in the poll loop). Bounded client-list
walk (`HUSH_WIN_CLIENT_MAX`).

`POST /api/window` `{action:"minimize"|"maximize"}`. Does not
insert a note. Does not shut down. HTTP replies
`{"ok":true,"action":"…"}` or `{"ok":false}`.

`#rail-min` / `#rail-max` POST that endpoint. Drop the
`railParked` early return on Minimize. Drop Fullscreen API as the
primary Maximize path.

### Launch

`hush_exec_app_browser` adds `--ozone-platform=x11` so the `--app`
window is an XWayland client COSMIC can decorate and we can
address. Keep `--class=hush-relay --name=Hush --app=url`.

`hush_relay_watch_app` calls `hush_win_undecorate()` once the
first live `--app` is latched (`g_saw_app` rising edge). Safe to
call repeatedly; helper no-ops if already frameless or no window.

### What "windowless" is

Not a PWA term. We are **frameless standalone `--app`**. Manifest
stays `display: standalone`. We do **not** switch to
`display: fullscreen` (kiosk). We do **not** add
`window-controls-overlay` (install-only, E5).

### Rail v2

Markup order:

1. `#install` full width (unchanged size/position) + `#rail-info`
   `i` → `#install-help`.
2. Divider.
3. Two-col: Profile | Settings.
4. Two-col: Call | Invite (`#invite-human`) + `#invite-info` `i`
   → "Click to invite a human to your vibe."
5. Divider.
6. Add Channel (`#add-chan`) + `#chan-info` `i` → what a channel
   is. Name field `#new-chan` lives in that popover.
7. Divider.
8. Two-col: New Robot (`#raise-agent`) + `#robot-info` `i` |
   New Project (`#add-proj`) + `#proj-info` `i`. `#new-proj`
   lives in the project popover.
9. Divider.
10. Two-col: Minimize | Maximize.
11. Two-col: Close | Exit.

Delete `<div class="create">` from `nav`. Channel list and robot
cards stay. Drawer copy "Raise a robot" / "Invite human" stays so
`check_launch.sh` still greps those strings.

Blank (`#blank-btn`) is removed.

Pair buttons stay 32px. `#install` and `#rail-toggle` stay ≥44px.

### Out of scope

- GitHub unfork.
- Raising `HUSH_EVENT_MAX_CONTENT`.
- Wayland foreign-toplevel protocol / libwayland.
- Changing Close/Exit semantics.
- Live hive restart (operator).

## Four-score on this lock (pre-fix, cap 7)

- Data **7/10** — would rise with a quoted `check_pwa` that greps
  `/api/window`, `--ozone-platform=x11`, and no `class="create"`.
- Sherlock **7/10** — would rise when live `xprop WM_CLASS` on the
  new window is `hush-relay` and Iconify actually docks it.
- Linus **7/10** — would rise if C stays inside `hush_win.c` + a
  15-line HTTP dispatch, no Wayland stack.
- Brian Cox **7/10** — would rise when the launch geodesic is
  ozone-x11 *before* the first undecorate call.

## Unanimous gate

- Data: **yes** **7/10** — H1+H4 are the two levers; rail v2 is
  markup.
- Sherlock: **yes** **7/10** — A6/A7 residual accepted.
- Linus: **yes** **7/10** — one optional X11 file.
- Brian Cox: **yes** **7/10** — watch_app rising edge is the
  right moment to undecorate.
