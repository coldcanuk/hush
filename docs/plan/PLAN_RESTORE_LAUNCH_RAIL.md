# PLAN: restore launch JS + UI_SPEC §15 tool rail (RDAP)

Branch: `gb/restore-launch-rail`
Worktree: `worktrees/restore-launch-rail`
Base: `main` `59f5ed59f` (PR #96)

## 1. Methodology

RDAP. Research gate:
[`../research/RESEARCH_RESTORE_LAUNCH_RAIL.md`](../research/RESEARCH_RESTORE_LAUNCH_RAIL.md).
Commit after every Milestone on this branch. Land only via PR.
Never write `main`. Worktree path is `/opt/repo/hush/worktrees/<slug>`
(Prime Directive), not `../gb-*-wt`.

## 2. Scope

Locked in the research file.

**Primary Goal**

1. Served hive page: zero `pageerror`. Developer Log close/clear exist
   in the DOM before listeners run (drawer before `<script>`).
2. `#tool-rail` matches `UI_SPEC.md` §15: free drag, clamp, persist
   `{x,y,collapsed}` only, `placeRailAtBrand` left of `.brand`,
   dblclick `#rail-toggle` homes, thread park/restore. No 360 /
   `!important` / nanny / poison stack.
3. `check_launch.sh` fails abort + 360 lock; `make test` green on the
   restored contract.
4. Live `hush-relay` + headless load: no pageerror, badge not stuck
   `connecting…`, computed rail `left` moves on a free place/drag.

**Non-Goals**

Inventory/mention/chat copy except the aborting HTML. Merging leftover
`gb/fix-tool-rail-*` / PR #94. C11 protocol. Spec rewrite to 360.
Unbounded polish. Local merge onto `main`.

**Success Criteria**

Research + architecture lock + HTML/embed/tests + launch probe + PR
merged + this worktree removed.

**Constraints**

- HTML/JS in `hush-c/demo/index.html` only (plus `check_launch.sh`,
  RDAP docs). No C body changes; embed header is generated.
- After every HTML edit: `make -C hush-c` (Makefile embeds).
- Prime Directive worktree + PR.

**Assumptions**

- `npx playwright` / Chromium cache from the host can drive the live
  probe. If not, capture launcher failure; criterion 3 is the bar.
- Last spec-shaped rail functions are `16dcdd431` (quoted in research).

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. Abort-only or nanny-only leaves the other defect. Do F1 then F2
   in order, verify both.
2. String-grep tests stay green on a broken UI. New assertions first
   fail on `main` HTML, then pass after the strip.
3. Wrong worktree. Stay in `worktrees/restore-launch-rail`.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean `main` `59f5ed59f`, worktree
      `/opt/repo/hush/worktrees/restore-launch-rail` on
      `gb/restore-launch-rail`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/restore-launch-rail`.

### Phase 1 — Research (GATE)

- [x] Task 1 of M1.1: quote abort + §15 vs current HTML; high-level
      fix list F1–F7 + dead-tail blast radius in
      `docs/research/RESEARCH_RESTORE_LAUNCH_RAIL.md`.
- [x] Task 2 of M1.1: this plan (remaining phases).
- Verify: both files exist on `gb/restore-launch-rail`.
- Commit: `Milestone 1.1: research + frozen plan for launch JS and §15 rail`

### Phase 2 — Architecture

#### M2.1 Abort-then-strip lock

- Task 1 of M2.1: freeze in this file: (1) abort first, (2) one
  `placeRail` writer, (3) brand home at x=8 from `16dcdd431`,
  (4) do not merge leftover rail tips. No `UI_SPEC.md` edit.
- Verify: `rg -n "Abort first|one writer|placeRail\\(8" docs/plan/PLAN_RESTORE_LAUNCH_RAIL.md docs/research/RESEARCH_RESTORE_LAUNCH_RAIL.md`.
- Commit: `Milestone 2.1: architecture lock abort-then-strip-to-§15`

Architecture (frozen 2026-08-26):

1. **Abort first, then strip nanny.**
2. **one writer:** clamp `placeRail` + persist `{x,y,collapsed}`.
3. **Home:** `placeRailAtBrand()` → `placeRail(8, y)` + header pad.
4. **Leftover `gb/fix-tool-rail-*` are not the fix.**

### Phase 3 — Implementation

#### M3.1 Developer Log drawer before boot script

- Task 1 of M3.1: cut `#dev-log-drawer` block from after `</script>`
  and paste it before `<script>` (after `#chan-menu` / with other
  drawers).
- Task 2 of M3.1: wrap
  `$("dev-log-close")` / `close2` / `clear` listeners:
  `if ($("dev-log-close")) { … }` (same pattern as `if ($("dev-log"))`).
- Verify: `python3 -c` that markup line of `id="dev-log-close"` < first
  `<script>` line; `rg 'dev-log-close"\)\.addEventListener'` still exists.
- Commit: `Milestone 3.1: Developer Log drawer precedes boot script`

#### M3.2 Strip 360 stack, restore §15 rail

- Task 1 of M3.2: delete ultra-early IIFE + `forceRailEarly` at script
  top; delete nanny `setInterval` and `setTimeout(0)` snap at script
  end.
- Task 2 of M3.2: CSS `#tool-rail` → `top: 10px; right: 12px` (no
  `left: 360px`). Collapsed `#rail-grip` min-width 44px.
- Task 3 of M3.2: restore clamp-only `placeRail`; `placeRailAtBrand`
  `placeRail(8, y)`; `loadRail` restores `saved.x`/`saved.y`/
  `saved.collapsed` (ignore `homed` in LS); `saveRail` writes
  `{x,y,collapsed}` only; `restoreRailAfterThread` restores saved x,y;
  toggle click keeps in-place collapse (re-place at 8 if `railHomed`);
  toggle **and** grip dblclick call `placeRailAtBrand()`; remove
  rail-bar pure-toggle dblclick; resize uses `placeRail(rect.left,
  rect.top)` with no 360 snap.
- Task 4 of M3.2: `make -C hush-c` (embeds `hush_ui_html.h`).
- Verify: `rg -n 'GOOD = 360|360px !important|left: 360px' hush-c/demo/index.html`
  is empty; `rg -n 'saved\.x|placeRailAtBrand|placeRail\(8' hush-c/demo/index.html`
  hits; header contains escaped `placeRail(8`.
- Commit: `Milestone 3.2: §15 rail restore, 360/nanny/poison gone`

#### M3.3 check_launch.sh contract

- Task 1 of M3.3: extend `hush-c/tests/check_launch.sh` after the
  existing profile-before-script check:
  - `id="dev-log-close"` line < `<script>` line
  - fail on `left: 360px !important`, `GOOD = 360`,
    `r.style.left = "360px"`
  - require `saved.x`, `saved.y`, `function saveRail`
  - `saveRail` body must not contain `homed:`
  - `#rail-toggle` dblclick handler includes `placeRailAtBrand`
  - keep docks/`railAnchor` forbidden
- Task 2 of M3.3: from worktree `./configure && make && make test`.
  Save full log to the implementer scratch `make-test.txt`.
- Verify: `make test` prints `ALL TESTS PASSED`; new greps would fail
  on pre-change HTML (spot-check by quoting the added fail strings).
- Commit: `Milestone 3.3: launch HTML contract catches abort and 360 lock`

### Phase 4 — Launch probe

#### M4.1 hush-relay + Playwright

- Task 1 of M4.1: `make -C hush-c`, boot
  `./hush-c/hush-relay <port> --no-open`.
- Task 2 of M4.1: Playwright: zero `pageerror`; after ≥2s badge text
  is not `connecting…`; `placeRail(80,80)` computed left is not
  `360px`; a grip drag changes computed left. Write `rail-verify.txt`
  in scratch. If Playwright cannot start, write that failure.
- Commit: `Milestone 4.1: launch probe for zero pageerror and free rail X`

### Final Phase — Land

#### M5.1 PR + cleanup

- Task 1 of M5.1: `git push -u origin HEAD`.
- Task 2 of M5.1: `gh pr create --base main --head gb/restore-launch-rail`.
- Task 3 of M5.1: `gh pr merge --auto --merge`; wait until merged.
- Task 4 of M5.1: `cd /opt/repo/hush && git pull --ff-only origin main`;
  `git worktree remove worktrees/restore-launch-rail`; delete
  `gb/restore-launch-rail`. Leftover `worktrees/fix-tool-rail-*` may
  be removed after this land (not before).
- Verify: `/opt/repo/hush` on `main` is clean; this worktree is gone.

## 4. Audit (pre-execution)

- Every task has a command/snippet and a verify step.
- Phase 1 synthesis gate is this commit.
- Worktree lifecycle is Prime Directive (PR, not local merge).
- Tasks are atomic (drawer move, then rail strip, then tests, then probe).
