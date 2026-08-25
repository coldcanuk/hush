# RESEARCH: Thread UX (PLAN_THREAD_UX) - current base audit

Base: 070625ce1 (fresh worktree from clean main post #63)

## Audit vs PLAN_THREAD_UX.md DoD

**Thread scroll (H1, M3.1)**
- THREAD_PIN_PX = 48
- threadNearBottom(box)
- paintThreadStream:
  - near = !shown || forcePin || threadNearBottom(box)
  - keep = box.scrollTop
  - render
  - if (near) scrollHeight else keep
- openThreadPane passes !wasOpen for forcePin on first open
- Matches spec: no forced scrollTop unless near bottom or just opened.

**Composer (M3.1)**
- #msg and #thread-msg are <textarea rows="6">
- Shift+Enter newline, Enter submit present.

**Canvas (M3.2)**
- #code-canvas-hi with tok- classes (golang/Go keywords)
- #canvas-k popover
- Ctrl+K, POST /api/fixup path

**Rail (M3.3)**
- rail-info, rail-min, rail-max, blank-btn elements from prior rail work.

**Fixup (M3.4)**
- /api/fixup implemented (canvas fixup flavor, no hive note)

**Tests (M4.1)**
- check_pwa.sh + check_fixup.sh contain required greps
- make test: ALL PASS

## Conclusion
All DoD items present and passing.

Features delivered in prior atomic PRs. This worktree:
- M1.1 research gate (this file)
- Full verification gate (build + tests + checks)
- Commit, push, PR, auto-merge, cleanup per PRIME_DIRECTIVE to close the plan.

No new functional code required.
