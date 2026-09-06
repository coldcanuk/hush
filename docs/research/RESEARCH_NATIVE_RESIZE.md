# Native window resize stall — 2026-09-06

Using the four-minds debug protocol, TOOLED mode. Scope: the outer Hush
application window, as clarified by the user; the floating thread is separate.

## Context register / Phase 0

Available MCP integrations: GitHub, Google Drive, Gmail, Outlook, Sites, plugin
management, hotline. None supplies desktop observability or windowing docs.
Consulted local X11 tools, the installed COSMIC revision's public source, and
[EWMH resize synchronization](https://specifications.freedesktop.org/wm/latest-single/).
No external messages or issue transitions are required by Hush's workflow.

Data: Local display properties establish which protocols are active.
Sherlock: "Active" does not establish that the acknowledgement completes.
Linus: Compare one protocol on/off in an isolated compositor.
Brian Cox: Repeat successive drags to expose retained state. No objection.

## Phase 1 — Quoted evidence

- E1 — `hush-c/src/hush_relay.c:322`: `"--ozone-platform=x11", app_arg, (char *)NULL);`
- E2 — Desktop inspection: `XDG_SESSION_TYPE=wayland`,
  `XDG_CURRENT_DESKTOP=COSMIC`; `cosmic-comp --version`:
  `cosmic-comp 1.0.0 (git commit f8344126a16e7f8712fa123076fc34c9238493a7)`.
- E3 — `hush-c/src/hush_relay.c:1028`: `if (!g_saw_app)` followed by
  `(void)hush_win_undecorate();`.
- E4 — `hush-c/src/hush_win.c:223`: `mwm.decorations = (1L << 1) | (1L << 2);`.
- E5 — Baseline isolated COSMIC/Xwayland, Chromium 148, actual Hush page,
  40 XTest pointer motions at 20 ms intervals, native left/bottom borders:
  `"edge": "left"`, `"geometry_updates": 1, "samples": 40`.
  Restoring Chromium's original decoration value 1 still yielded
  `"geometry_updates": 1` or `"geometry_updates": 2`.
- E6 — Same window, page, compositor, border settings; toggle only the
  `_NET_WM_SYNC_REQUEST` entry in `WM_PROTOCOLS`:
  `resize_sync=True`: left, left, bottom, bottom each logged
  `"geometry_updates": 1, "samples": 40`.
  `resize_sync=False`: the same sequence each logged
  `"geometry_updates": 40, "samples": 40`.
- E7 — Post-experiment property output:
  `WM_PROTOCOLS(ATOM): protocols  WM_DELETE_WINDOW, _NET_WM_PING`.
  The counter property still exists; only the optional handshake is disabled.
- E8 — EWMH section 6.2: "The window manager SHOULD not resize the window
  faster than the client can keep up."

Raw experiment scripts/logs/screenshots are in `/tmp/hush-resize-evidence/`.
Measurements use distinct native window geometries, not a browser viewport
simulation. They establish a handshake-dependent stall in this stack, not
which side's acknowledgement implementation is defective.

Assumptions: the report's first drag was smooth [user report, not measured];
which physical monitor/scaling was involved [unverified]; nested software
rendering exactly matches the real desktop's GPU timing [unverified].
Falsifiers: repeated drags, original-decoration comparison, handshake on/off.
One initial no-sync left-edge miss disappeared when the target moved from
one pixel outside the client to the middle of the compositor border.

Data: E5 supplies a native reproduction.
Sherlock: "Native reproduction" does not make a missed border click a stall.
Linus: Center the probe on the border; exclude the missed click from E6.
Brian Cox: Original hints still stall; toggling sync restores subsequent drags.

## Phases 2–4 — Hypotheses, weights, second debate

| Hypothesis | Explains | Does not establish | Cheapest falsifier |
|---|---|---|---|
| H1: X11 resize-sync handshake stalls | E1, E2, E5–E8, successive-drag degradation | Which implementation is faulty | Toggle only sync advertisement |
| H2: conflicting decoration hints | E4, native edges | E6 reversibility | Restore original hints |
| H3: page resize/layout work stalls | Page changes during resizing | E6 with identical page | Hold page fixed for H1 comparison |

Data: H1 is observed through the protocol toggle; no coordinate-feedback writer
exists in Hush's outer-window path.
Sherlock: "H1" must not be expanded into a claim that all Chromium resizing is bad.
Linus: Limit the compatibility change to COSMIC and the matching Hush window.
Brian Cox: E3 rules out continuous decoration writes, not a one-time mismatch.

Subjective priors: H1 .50 (E1/E2 expose a bridged windowing path), H2 .30
(E4 is Hush's native mutation), H3 .20 (page layout participates in painting).
Likelihoods after E5/E6: .95, .03, .03 respectively. These estimates rank
diagnoses; they are not population statistics or measured probabilities.

`P(H|E) = prior * likelihood / sum(prior * likelihood)`.
Denominator: `.50*.95 + .30*.03 + .20*.03 = .490`.
Posteriors: H1 `.475/.490 = .969`; H2 `.009/.490 = .018`;
H3 `.006/.490 = .012` (rounding explains the .001 shortfall).

Data: 6/10 on the unimplemented path; H1 leads H2 by .951.
Sherlock: 6/10; "scoped to COSMIC" requires a negative test for other desktops.
Linus: 6/10; one native module is sufficient, no UI refactor.
Brian Cox: 6/10; on/off reversal supports the causal order. No objection.

## Phases 5–6 — Scope and synthesis gate

The reproduced incident is a COSMIC/Xwayland resize-sync stall. Hush opts
into this path by forcing Chromium's X11 backend. The optional resize handshake
can be removed from the Hush window's advertised protocols on COSMIC.

Preserve close/ping/custom protocols, native resize borders, min/max controls,
and synchronization on other desktops. No thread-panel changes. The plan is
in [PLAN_NATIVE_RESIZE.md](../plan/PLAN_NATIVE_RESIZE.md).

Data: Yes, 6/10; implement the observed workaround, then execute it through C.
Sherlock: Yes, 6/10; "preserve" must be asserted on real X11 properties.
Linus: Yes, 6/10; keep the diff in the existing window module and its test.
Brian Cox: Yes, 6/10; validate multiple consecutive native drags.

All four agree to execute the scoped plan. Confidence in the unexecuted C
implementation remains capped at 6/10 until its own verification output exists.

## Phase 7 — Observed implementation and review

- E9 — `python3 hush-c/tests/check_win.py` before implementation:
  `AssertionError: COSMIC: resize synchronization must be removed; close/ping/custom must remain`.
  After implementation: `window check: OK`.
- E10 — Compiled public C entry point, real isolated compositor/pointer drags:
  `native C resize check: 12 consecutive corner/left/vertical drags passed`.
  Each drag: `"geometry_updates": 40, "samples": 40`.
  Fresh app startup:
  `real --open launch: COSMIC resize workaround applied; close/ping preserved`.
- E11 — Full suite with an empty temporary password store: `ALL TESTS PASSED`.

Data: 9/10; E9–E11 exercise C behavior, actual pointer motion and normal launch.
Sherlock: 9/10; "normal launch" addresses whether direct-call verification
bypassed application setup. Negative desktop/window tests pass.
Linus: 9/10; native code remains in one module, with scoped helpers and no UI diff.
Brian Cox: 9/10; repeated drags preserve pointer/geometry timing. No objection.

Physical-desktop confirmation would raise Data/Brian's scores; wider compositor
version coverage would raise Sherlock's; sustained use without regressions would
raise Linus's. The Hush paperwork contract is a reviewed GitHub PR and merge,
per PRIME_DIRECTIVE.md; there is no GitLab incident issue in this task.

## Follow-up: reopen the incident after the real Brave launch failed

The earlier resolution was too broad: the isolated Chromium test used a
separate profile that the production launcher did not supply. The user reports
that bottom-right works, bottom-left stalls, then bottom-right also stalls.
That ordering is the acceptance test for this follow-up.

Context register: the same connectors offer no desktop telemetry. Local X11,
process inspection and isolated COSMIC remain relevant. Consulted
[Chromium's separate-profile guidance](https://www.chromium.org/developers/creating-and-using-profiles/).

Data: The previous binary was installed, but window setup is a separate fact.
Sherlock: "Installed" cannot establish that the target window was found.
Linus: Check its live class and protocol list before changing the workaround.
Brian Cox: Preserve the right-left-right sequence. No objection.

### Quoted evidence and assumptions

- F1 — Live window `0x800080`: `WM_NAME(UTF8_STRING) = "Hush"` and
  `WM_CLASS(STRING) = "127.0.0.1", "Brave-browser"`.
- F2 — Same window:
  `WM_PROTOCOLS(ATOM): protocols  WM_DELETE_WINDOW, _NET_WM_PING, _NET_WM_SYNC_REQUEST`.
  `_MOTIF_WM_HINTS(_MOTIF_WM_HINTS) = 0x2, 0x0, 0x1, 0x0, 0x0`.
- F3 — Live relay and installed binary both reported SHA-256
  `8dab10260931fd1011f67757838cfdf3a3b8bf5f4d7e0aadb1235e3fcf9d1021`.
- F4 — Existing lookup: `#define HUSH_WIN_CLASS_NAME        "hush-relay"`;
  diagnostic call on the shared-browser class:
  `shared-browser window: hush_win_undecorate status = -4`.
- F5 — Installed Flatpak Brave, isolated COSMIC, first exact sequence:
  `"edge": "bottom-right"`, `"geometry_updates": 37, "samples": 40`;
  `"edge": "bottom-left"`, `"geometry_updates": 2, "samples": 40`;
  `"edge": "bottom-right"`, `"geometry_updates": 1, "samples": 40`.
- F6 — Same Brave window with the Hush class and C workaround active:
  `owned-browser window: hush_win_undecorate status = 0`;
  six drags each report `"geometry_updates": 40, "samples": 40`;
  `Brave right-left-right: two consecutive sequences passed with window ownership`.
- F7 — Temporary separate-profile Flatpak launch produced
  `("127.0.0.1" "hush-resize-probe")` in the isolated X11 window tree.
- F8 — Existing startup gate: `if (!g_saw_app)` then
  `(void)hush_win_undecorate();`; existing relay reuse opens the browser and
  returns without starting another event pump.

Raw scripts/logs: `/tmp/hush-resize-followup/`. F5/F6 use the installed Brave,
not the cached Chromium test build. Physical-desktop smoothness after the new
launch remains unverified. The protocol and window-identity failure are observed.

Data: F1/F2/F4 show the previous workaround missed the real window.
Sherlock: "Missed the real window" also invalidates our old launch acceptance test.
Linus: Make browser isolation part of the launcher, not a test-only shim.
Brian Cox: Prepare after the page is alive; a launcher PID may already be gone.

### Hypotheses and numerical review

| Hypothesis | Explains | Does not establish | Falsifier |
|---|---|---|---|
| H1: shared-browser launch bypasses Hush window setup | F1/F2/F4/F5/F6/F8 | Future reopen behavior | Dedicated profile + page-ready preparation |
| H2: another left-edge geometry defect | F5 | F6's full recovery | Exact sequence with workaround demonstrably active |
| H3: stale executable | Could explain a missing workaround | F3 and class mismatch | Compare live and installed executable |

Subjective priors .75/.15/.10 reflect the live class/protocol mismatch (F1/F2),
the sequence specificity (F5), and the possibility of an old running binary.
Likelihoods .98/.05/.01 reflect F4/F6's reversal and F3's matching binaries.
Denominator: `.75*.98 + .15*.05 + .10*.01 = .7435`.
Normalized posteriors: `.735/.7435=.989`, `.0075/.7435=.010`,
`.001/.7435=.001` (diagnostic estimates, not population statistics).

Data: 6/10 for implementing H1; it leads H2 by .979.
Sherlock: 6/10; "H1" needs tests with the production launcher and a running relay.
Linus: 6/10; retain the proven handshake change and fix its delivery.
Brian Cox: 6/10; page readiness removes the launcher/window timing assumption.

### Scope, plan and agreement gate

Use a per-relay browser data directory, preserve the dedicated Hush class,
prepare native windows from page startup with bounded retry, and cover all
matching windows when a browser instance is reused. Other browser windows
remain outside the class match. No pointer/resize arithmetic changes.

Plan: [PLAN_NATIVE_RESIZE_FOLLOWUP.md](../plan/PLAN_NATIVE_RESIZE_FOLLOWUP.md).

Data: **yes, 6/10**; F6 supports the path, the production implementation is pending.
Sherlock: **yes, 6/10**; "all matching windows" must include a second-window test.
Linus: **yes, 6/10**; keep preparation out of drag handlers.
Brian Cox: **yes, 6/10**; repeat the exact sequence after fresh and reused launches.

### Implementation cross-examination and acceptance

After launcher changes, Data: 6/10; the argv fixture now observes
`--user-data-dir=` from Hush itself. Sherlock challenges "from Hush itself":
the final native test wrapper adds only debugging/initial-size options, not
class, profile, filesystem permission or URL. Linus: 6/10; bounded native and
Flatpak lists share one options record. Brian Cox: 6/10; browser singleton
forwarding still requires the page-ready task. All agree to continue.

After page preparation, Data: 6/10; the route dispatches `prepare` to the native
setup. Sherlock challenges "native setup": a first-window lookup misses a
reopened window. Linus: 6/10; preparation scans the bounded matching-client list,
while the old once-per-launcher call is removed. Brian Cox: 6/10; twenty bounded
page attempts cover mapping after page startup. All agree to continue.

After regression changes, Data: 8/10; output:
`window check: page-ready API handles late and reopened windows`.
Sherlock challenges "reopened": require the real Flatpak singleton path too.
Linus: 8/10; `browser launch check: OK` covers native/Flatpak, fresh/reused relay,
and profile paths containing spaces. Brian Cox: 8/10; browser interception below
covers absent windows and transport errors. All agree to final native testing.

- F9 — Strict `make -j4` exited 0. Focused regressions printed:
  `browser launch check: OK` and `window check: OK`.
- F10 — Full production launcher, installed Flatpak Brave, isolated COSMIC:
  `WM_CLASS(STRING) = "127.0.0.1", "hush-relay"`;
  `WM_PROTOCOLS(ATOM): protocols  WM_DELETE_WINDOW, _NET_WM_PING`;
  `_MOTIF_WM_HINTS(_MOTIF_WM_HINTS) = 0x2, 0x0, 0x6, 0x0, 0x0`.
  Ten drags each printed `"geometry_updates": 40, "samples": 40`;
  `Full launcher: right-left-right twice, then horizontal/vertical edges: PASS`.
- F11 — A second actual `hush-relay --open 18898` printed
  `Opening in existing browser session.` The new window had the same prepared
  properties, and six drags each printed `"geometry_updates": 40, "samples": 40`;
  `Reopened Hush window: right-left-right twice: PASS`.
- F12 — Installed Brave executing the actual embedded UI with intercepted
  window responses printed:
  `page preparation: transport failure + two absent windows -> fourth attempt succeeds; retries stop`;
  `page preparation: unavailable native window -> exactly 20 attempts; retries stop`.
- F13 — `PASSWORD_STORE_DIR=/tmp/hush-resize-followup/test-password-store make test`
  exited 0 and printed `ALL TESTS PASSED`. The temporary store was empty, avoiding
  restoration of the operator's identity in first-launch tests.

C review applied the write-legible-c section 14 checklist to changed regions:
new helpers stay below forty lines and depth two, loops have explicit bounds,
string formatting checks overflow, declarations/prototypes carry ownership
contracts, and acquired X11 displays close on every return path. POSIX exec
adapters intentionally fall through on failure; the existing status/API ABI is
preserved. Generated UI headers are rebuilt by Make and are not tracked.

Data: 9/10; F9–F13 cover delivery and observed resizing. Sherlock challenges
"delivery": the physical desktop still needs the installed binary launched with
its actual profile directory. Sherlock: 8/10 pending that check. Linus: 9/10;
no resize-event computation was added. Brian Cox: 9/10; fresh and reused launches
both retain 40/40 updates across the complete ordering. No objection to delivery.

- F14 — Additional top/right edge test:
  `Reopened Hush window: top/right edges in both directions: PASS`;
  all four drags printed `"geometry_updates": 40, "samples": 40`.
  The first two top probes were rejected as test-coordinate errors: `y-1`
  moved the whole window without changing height; `y-35` hit the header. The
  isolated desktop screenshot showed a 36-pixel server header above the client,
  and `y-40` hit the actual outer border. No source change was made for those
  failed probes. Left/bottom tests already covered both axis directions.

Verification milestone complete. Installation/physical-desktop property checks
remain the delivery milestone; the old live Hush window had already closed
when checked immediately before delivery.
