# RESEARCH: Thread UX (current base audit) - gb/thread-ux

Base: cce2d81fb (post pills-rail-voice-exit PR #58)

## Audit vs PLAN_THREAD_UX.md DoD / Milestones

**Composer (M3.1)**
- `#msg` and `#thread-msg` are `<textarea rows="6">` — present.
- Shift+Enter newline, Enter submit logic present in form handlers.
- Mention pills work with the textareas.

**Thread scroll / snap-back (H1, M3.1)**
- `THREAD_PIN_PX = 48`
- `threadNearBottom(box)`
- `paintThreadStream`:
  - `const near = !shown || forcePin || threadNearBottom(box);`
  - `const keep = box.scrollTop;`
  - ... render ...
  - `if (near) box.scrollTop = box.scrollHeight; else box.scrollTop = keep;`
- `openThreadPane` calls with `!wasOpen` for forcePin on first open.
- This matches the required behavior: does not force scrollTop unless near bottom or just opening.

**Canvas highlight + Ctrl+K (M3.2)**
- `#code-canvas-hi` overlay with tok- classes (kw, str, cmt, num, ghost)
- Language map includes golang/Go keywords.
- `#canvas-k` popover, Ctrl/Cmd+K handling.
- `POST /api/fixup` path exists (canvas FIM / fixup routes already in http + agent).
- Scroll sync between edit and hi.

**Tool rail (M3.3)**
- Rail elements: `rail-info`, `blank-btn` references in plan, `rail-min`, `rail-max` present in current HTML from prior rail work.
- Install first, `i` help popovers, two-col grids.
- Matches the compact rail from previous slices.

**Fixup API (M3.4)**
- `/api/fixup` is implemented (from canvas-fim and prior work).
- Uses agent job flavor, no hive note.
- Tests exist (`check_fixup.sh` or integrated).

**Tests (M4.1)**
- `check_pwa.sh`, `check_fixup.sh`, `check_launch.sh` contain required greps.
- Baseline: ALL TESTS PASSED.

## Current state on this base
Most of the plan (M2, M3, M4) is already implemented from prior atomic PRs (thread-ux, rail, canvas-fim, etc.).

The plan document itself has many [x] marks, indicating it was largely delivered.

## Atomic work for this worktree
- M1.1: Research gate + current-base audit (this file).
- Verify no regression on current main.
- Run full make test + checks.
- If any small hygiene, do it as M4/M5.
- Then PR + full PRIME_DIRECTIVE lifecycle.

No large missing features found. This is primarily verification + closing the plan on the latest base.
