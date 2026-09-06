# Native resize compatibility fix

Research/synthesis gate: [RESEARCH_NATIVE_RESIZE.md](../research/RESEARCH_NATIVE_RESIZE.md).
Worktree: `worktrees/resize-drag-lag`, branch `gb/resize-drag-lag`.

## Phase A — Observable regression (Milestone 1)

1. Capture repeated native COSMIC drags with sync on/off.
2. Add an isolated Xvfb property test of `hush_win_undecorate`.
3. Commit the diagnosis and failing regression on the feature branch.

## Phases B/C — Protocol change (Milestone 2)

1. Detect the COSMIC desktop token within `XDG_CURRENT_DESKTOP`.
2. Remove only `_NET_WM_SYNC_REQUEST` from the matching Hush window's
   `WM_PROTOCOLS`; preserve other desktop/window/protocol behavior.
3. Keep named border constants and explicit Xlib resource cleanup.

## Phase D — Verification (Milestone 3)

1. Run `python3 hush-c/tests/check_win.py`, including no-X11 behavior,
   desktop scoping, protocol preservation, absent properties, and repeat calls.
2. Execute the compiled C entry point on the isolated COSMIC browser; repeat
   native edge/corner drags and inspect actual window geometry.
3. Run `./configure`, `make`, and `make test`; review the C checklist and diff.

## Phase E — Delivery (Milestone 4)

1. Record the observed results below and in the PR description.
2. Push, open PR, review, auto-merge; update main by fast-forward pull.
3. Remove the merged worktree/branch; install the tested binary for the local app.

## Verification / changelog

Fixed COSMIC/Xwayland native resize stalls by removing only the optional
`_NET_WM_SYNC_REQUEST` protocol from Hush's window setup. Other desktops keep
synchronization. Close/ping/custom protocols and resize border hints remain.

- Strict C11 `./configure && make -j4`: exit 0.
- `python3 hush-c/tests/check_win.py`: `window check: OK`.
- Compiled C entry point applied to the isolated COSMIC browser:
  `native C resize check: 12 consecutive corner/left/vertical drags passed`.
  Every drag produced 40 distinct native geometries from 40 pointer samples;
  final width/height matched the requested pointer delta within two pixels.
- Fresh normal startup:
  `real --open launch: COSMIC resize workaround applied; close/ping preserved`.
- `PASSWORD_STORE_DIR=/tmp/hush-resize-evidence/test-password-store make test`:
  `ALL TESTS PASSED`. The first invocation without credential isolation stopped
  at `launch check failed: cold session should be logged out`; the empty
  password store removes the developer's saved identity from that test.
- `git diff --check`: exit 0.

C review: bounded desktop/protocol scans; no recursion/goto; touched functions
under 40 lines and nesting depth at most two; protocol allocation released on
all successful-read paths; assertions on mutation/borrowed resource invariants;
new native helpers declared at file top; Xlib-required int length documented;
shared `hush_status_t` and public signatures retained for repository ABI.

Residual limit: the reproduction uses a nested instance of the installed
COSMIC compositor with software rendering. Physical-monitor/GPU timing was not
measured. The compatibility path opts out of synchronized repainting on COSMIC,
so it trades that synchronization for responsive asynchronous native resizing.
