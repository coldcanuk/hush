# Native resize follow-up — window ownership

The incident was reopened after PR #148. The acceptance sequence is
bottom-right → bottom-left → bottom-right on the installed Flatpak Brave.
See the follow-up evidence in
[RESEARCH_NATIVE_RESIZE.md](../research/RESEARCH_NATIVE_RESIZE.md).

## Phase A / Milestone 1 — Reproduce the delivery failure

1. Inspect the running executable and native window's class/protocols.
2. Replay the exact corner sequence on isolated installed Brave.
3. Add a failing real-launch argv regression for native/Flatpak browsers,
   including reopening a running relay; commit/push the diagnosis.

## Phases B/C / Milestone 2 — Own and prepare app windows

1. Launch Chromium-family apps in a per-relay Hush browser profile; give
   Flatpak access only to that profile directory for this launch.
2. Prepare native windows from page startup with bounded retries, avoiding
   dependence on the short-lived browser launcher process.
3. Prepare every matching Hush window so reopening the same browser process
   also receives the workaround; preserve unrelated browser windows.

## Phase D / Milestone 3 — Verify

1. Execute launcher, native-window and page-preparation regressions.
2. Test the actual Flatpak launcher, a reopened GUI, and the exact corner
   sequence with window properties showing that preparation is active.
3. Run the strict build/full test suite and review the changed C regions.

## Phase E / Milestone 4 — Deliver

1. Record exact results, review the PR and merge through GitHub.
2. Install the tested binary and replace the old Hush app window/relay.
3. Inspect the real desktop's Hush class/protocols, then clean the worktree.

Milestones 1–3 are complete: diagnosis and regression, implementation, strict
build/full suite, startup retry, real Flatpak fresh/reopened corner sequences,
and all four edges in both directions. Exact evidence is F9–F14 in the research
record. Milestone 4 proceeds through reviewed GitHub merge, installation, live
window-property inspection, and worktree cleanup.
