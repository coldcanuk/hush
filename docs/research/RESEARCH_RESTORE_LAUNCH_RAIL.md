# RESEARCH: restore launch JS + UI_SPEC §15 tool rail

Worktree: `/opt/repo/hush/worktrees/restore-launch-rail`
Branch: `gb/restore-launch-rail`
Base: `main` `59f5ed59f` (PR #96)

Methodology: RDAP. Isolation is Prime Directive (`worktrees/<slug>` + PR),
not the pasted `../gb-*-wt` / local merge-to-main snippet.

## 1. Primary goal / non-goals / DoD (locked)

**Primary goal.** The served hive page loads with zero `pageerror`.
`#tool-rail` matches `UI_SPEC.md` §15 (free drag, persist `{x,y,collapsed}`
only, brand home, thread park/restore). `make test` fails the abort
pattern and the 360/nanny/poison lock.

**Non-goals.** Inventory grid / mention fidelity / chat copy except the
HTML that aborts. Merging leftover `gb/fix-tool-rail-*` or open PR #94.
C11 relay/protocol. Rewriting `UI_SPEC.md` §15 to match 360. Unbounded
delight/polish. Direct writes to `main`.

**Success.** Criteria in the execution plan
`docs/plan/PLAN_RESTORE_LAUNCH_RAIL.md`.

## 2. High-level fix list (comprehensive)

Two **in-scope defects** (this slice). Everything else is either
restored as a *consequence* of un-breaking the script, or explicitly
out of scope.

### In scope — must fix

| ID | What | Why |
|---|---|---|
| F1 | Move `#dev-log-drawer` (ids `dev-log-close`, `dev-log-close2`, `dev-log-clear`, `dev-log-body`) **before** `<script>`, matching the profile-drawer rule already in `check_launch.sh`. Guard the three listeners with `if ($(id))`. | Line 2988 `$("dev-log-close").addEventListener` throws because markup is after `</script>` (4855). |
| F2 | Strip 360 / `!important` inject / ultra-early force / nanny `setInterval` / `setTimeout(0)` snap / `GOOD = 360` poison in `placeRail` / always-360 `loadRail` / `saveRail` rewrite / `restoreRailAfterThread` always-360 / CSS `left: 360px`. | Inverts §15. Live: `style.left="80px"` still computes `360px`. |
| F3 | Restore clamp-only `placeRail`; `placeRailAtBrand()` immediately left of `.brand` (`placeRail(8, y)` + header pad, last spec-shaped impl at `16dcdd431`); `#rail-toggle` dblclick **homes**; persist `{x,y,collapsed}` only (drop `homed` from LS); thread park remembers pose, restore uses saved x,y unless grip drag cleared park. | §15 + PLAN_THREAD_CHAT_RAIL_UX. |
| F4 | Grip dblclick also homes (`placeRailAtBrand`). Remove rail-bar “pure toggle” dblclick. Keep single-click ☰ as in-place collapse. | Spec names toggle home; grip home was the #88 user ask and does not invert §15. Collapse-in-place on *dblclick* was #91 vs spec. |
| F5 | CSS: `top: 10px; right: 12px` (no left lock). `#rail-grip` min-height 44px; collapsed grip min-width **44px** (spec ≥44; not 16, not 72). | Fitts + §15. |
| F6 | `embed-ui.sh` so `hush_ui_html.h` matches demo. | Binary serves the embed. |
| F7 | Strengthen `check_launch.sh`: drawer-before-script for `dev-log-close`; forbid `360px !important`, `GOOD = 360`, nanny `left = "360px"`; require `saved.x` / `saved.y` restore; require toggle dblclick → `placeRailAtBrand`; saveRail must not persist `homed`; keep docks absent. | Current greps only `id="tool-rail"`, `placeRailAtBrand`, `dblclick`. |

### Restored as a consequence of F1 (dead tail after the throw)

These functions already exist; they never *run*. Fixing F1 binds them.
No separate feature rewrite.

- `tick()` / `paint()` / `setInterval(tick, 1000)` → badge leaves
  `connecting…` while relay is up.
- `loadRail()` call at end of script.
- Rail: toggle click/dblclick, grip pointerdown/dblclick, window
  pointermove/up/cancel, resize.
- Thread: `#thread-resize`, `#thread-msg` input/keydown, thread mention
  apply buttons.
- Calls: `#call-join` / `#call-leave` / `#call-agent`, tile mute.
- Code canvas: close/file/edit/scroll/keydown, k-apply, download, save,
  per-block open.
- Channel manage: `#chan-menu`, `#manage-close`, `#manage-invite-add`,
  `#manage-save`.
- Inventory: paint-time item pointerdown (inside `paint` after abort —
  `paint()` itself never runs), header Seed/Clear/Raise.
- PWA: `serviceWorker.register`, `beforeinstallprompt`, install click.
- `applyTheme` at boot.

### Out of scope (listed so they are not “forgotten”, not worked here)

| Item | Disposition |
|---|---|
| Inventory grid / mention fidelity / progressive acks | Non-goal; source is present; `paint()` will run again after F1. |
| Leftover `worktrees/fix-tool-rail-*` and open PR #94 | Do not merge. Delete leftover trees **after** this PR lands. |
| `UI_SPEC.md` rewrite to 360 | Forbidden. |
| C11 relay/protocol | Untouched. |
| Themes, new widgets, unbounded polish | Non-goal. |
| Extra `homed` **in-memory** flag for header pad / thread park | Keep in RAM; do not persist. |
| Default CSS `right: 12px` flash before `loadRail` | Acceptable; JS then places. |

## 3. Evidence (quoted)

**E1.** `hush-c/demo/index.html:2988`
`$("dev-log-close").addEventListener("click", closeDevLog);`

**E2.** `hush-c/demo/index.html:4852` `</script>` then `:4855`
`<div class="drawer" id="dev-log-drawer"`. Markup ids after script:
`dev-log-drawer`, `dev-log-close`, `dev-log-close2`, `dev-log-clear`,
`dev-log-body`. Only those five.

**E3.** `check_launch.sh:28-32` already documents the class of bug for
profile: `Drawers after </script> make boot listeners throw; splash
stays blank.` and requires `id="profile"` before `<script>`. Dev-log
was never added to that check.

**E4.** `UI_SPEC.md:554-559` “The rail stays where the human drops it
and is clamped on screen. `localStorage.hush-rail` stores
`{x,y,collapsed}` only.”

**E5.** `UI_SPEC.md:570-578` brand home + thread restore of pre-thread
`{x,y,collapsed}`.

**E6.** Current `placeRail` (`:4456-4458`) `GOOD = 360` poison band;
`:1146` inject `#tool-rail { left: 360px !important }`; `:4804-4815`
nanny `setInterval`.

**E7.** Pre-#88 `16dcdd431` `placeRail` clamp-only; `placeRailAtBrand`
`placeRail(8, y)`; `loadRail` restores `saved.x`/`saved.y`; CSS
`right: 12px`. Same commit already had the abort (listener ~2956,
markup after `</script>`).

**E8.** `hush-c/Makefile` regenerates `src/hush_ui_html.h` from
`demo/index.html` via `../scripts/embed-ui.sh demo`.

## 4. Architecture lock (for Phase 2)

1. **Abort first, then strip nanny.** F1 without F2 leaves an
   interactive rail pinned at 360. F2 without F1 leaves listeners dead.
2. **One writer.** `placeRail` clamps and sets `left`/`top`/`right:auto`.
   `saveRail` / `loadRail` read/write `{x,y,collapsed}` only. No nanny,
   no inject, no poison.
3. **Home is a named function**, not a magic X. `placeRailAtBrand()`
   computes y from `.brand` and places at x=8 with header pad (restore
   `16dcdd431`, which implemented §15).
4. **Do not merge leftover rail tips.** Current truth is this worktree
   from `main` `59f5ed59f`.

## 5. Remaining plan

See [`../plan/PLAN_RESTORE_LAUNCH_RAIL.md`](../plan/PLAN_RESTORE_LAUNCH_RAIL.md)
(updated as the Phase 1 synthesis gate).

## 6. Risks

1. Revert-only of #88–#96 would restore spec-shaped functions **and**
   the abort → original stuck-right + dead dblclick. Mitigate: F1+F2
   together on current HTML.
2. `make test` green without new greps is a lie. Mitigate: F7 first
   enough that a 360 leftover fails CI.
3. Editing a leftover `fix-tool-rail-*` tree diagnoses fiction.
   Mitigate: this worktree only.
