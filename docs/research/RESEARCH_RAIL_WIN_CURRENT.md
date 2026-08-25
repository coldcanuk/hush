# RESEARCH: Rail Win (Minimize, frameless, rail v2) - current base audit

Base: a0aea890e (post thread-ux PR #60)

## Audit vs PLAN_RAIL_WIN.md DoD

**UI elements (M3.1 / plan DoD)**
- #rail-min, #rail-max present in rail.
- #invite-human, #add-chan, #raise-agent, #add-proj, #chan-info, #robot-info, #proj-info, #invite-info present in rail.
- No <div class="create"> in left nav (already removed in prior rail work).

**C / window handling**
- hush-c/src/hush_win.c has hush_win_minimize, hush_win_maximize, X11 iconify/maximize, Motif undecorate.
- hush_relay.c calls into win for --app launch.
- /api/window handler exists (from prior rail-prov / rail slices).

**Launch**
- hush_exec_app_browser uses --ozone-platform=x11 when HUSH_HAVE_X11.
- Manifest "standalone".

**Tests**
- check_launch.sh greps for rail elements, invite, raise, etc.
- Baseline on this worktree will be run.

## State on this base
The core of PLAN_RAIL_WIN (rail v2 markup, min/max, X11 frameless, Create gone) was delivered in prior atomic PRs (rail-win, rail-prov, thread-ux, pills-rail).

This worktree is for verification gate + close per the plan's M5.1.

No large missing pieces found.

## Plan for this worktree
- M1.1: Research gate (this file).
- Full build + test gate.
- Commit verification.
- PR + full PRIME_DIRECTIVE lifecycle to close the plan.

Per RDAP + PRIME_DIRECTIVE.
