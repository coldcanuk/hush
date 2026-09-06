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
