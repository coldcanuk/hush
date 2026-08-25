# RESEARCH: Thread UX (current base) - gb/thread-ux

Base: e8d7f8c30 (post rail-win PR #61)

## Audit vs PLAN_THREAD_UX.md DoD

**Thread scroll (H1, M3.1)**
- THREAD_PIN_PX = 48
- threadNearBottom + keep/near logic in paintThreadStream
- openThreadPane uses forcePin on first open
- Matches "does not assign scrollTop unless near bottom or just opened"

**Composer (M3.1)**
- #msg and #thread-msg are <textarea rows="6">
- Shift+Enter newline, Enter submit

**Canvas (M3.2)**
- #code-canvas-hi with tok- classes (golang/Go keywords present)
- #canvas-k popover, Ctrl+K, POST /api/fixup

**Rail (M3.3)**
- rail-info, rail-min, rail-max, blank-btn references present from prior rail work

**Fixup (M3.4)**
- /api/fixup implemented, no hive note for canvas fixup
- Agent fixup flavor exists

**Tests (M4.1)**
- check_pwa.sh, check_fixup.sh contain the greps
- Baseline: ALL TESTS PASSED on this worktree

## Conclusion
All DoD items are present on the current base. Features delivered in prior atomic PRs (thread-ux, canvas-fim, rail slices).

This worktree: verification gate + close per RDAP + PRIME_DIRECTIVE.

No new code changes required.
