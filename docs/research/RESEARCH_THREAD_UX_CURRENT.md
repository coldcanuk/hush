# RESEARCH: Thread UX current base audit - gb/thread-ux

Base: b0d311282 (post previous merges)

## Audit vs PLAN_THREAD_UX.md DoD

**Thread scroll (H1)**
- THREAD_PIN_PX = 48
- threadNearBottom(box)
- paintThreadStream uses near = !shown || forcePin || threadNearBottom(box)
- keep = box.scrollTop; render; if (near) scrollHeight else keep
- openThreadPane passes !wasOpen for forcePin on first open
- Matches spec exactly.

**Composer**
- #msg and #thread-msg are <textarea rows="6">
- Shift+Enter newline logic present.

**Canvas**
- #code-canvas-hi overlay with tok- classes (golang keywords in map)
- #canvas-k popover
- Ctrl+K handling, POST /api/fixup

**Rail**
- rail-info, rail-min, rail-max, blank-btn elements present from prior rail work.

**Fixup**
- /api/fixup implemented (canvas fixup flavor, no hive note)

**Tests**
- check_pwa.sh and check_fixup.sh contain the greps
- make test: ALL PASS on this worktree

## Conclusion
All Success/DoD items are present and passing on the current base.

Features were delivered across prior atomic PRs (thread-ux, canvas-fim, rail, pills-rail).

This worktree performs:
- M1.1 research gate (this file)
- Full verification gate
- Commit, push, PR, auto-merge, cleanup per PRIME_DIRECTIVE to close the plan.

No new functional changes required.
