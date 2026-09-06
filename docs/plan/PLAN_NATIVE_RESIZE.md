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

Pending implementation and verification.
