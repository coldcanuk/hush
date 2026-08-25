# RESEARCH — Thread chat UX + free tool rail (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/thread-chat-rail-ux`
Branch: `gb/thread-chat-rail-ux`
Base: `main` `a492dac00` (post #70 oauth-mention-rail)

## Base State

All DoD items from PLAN_THREAD_CHAT_RAIL_UX.md are already present on this base:

**Grok effort token:**
- `#define HUSH_AGENT_GROK_EFFORT "low"`
- Used in hush_agent_exec_grok as --reasoning-effort low
- check_agent.sh greps for HUSH_AGENT_GROK_EFFORT "low"

**Dockless rail + brand home:**
- No `#rail-docks` (removed)
- placeRailAtBrand() exists
- Double-click #rail-toggle collapses and homes to brand left of hush/vibe name
- Open thread forces collapsed + brand home; close restores pre-thread pose
- Persist only {x,y,collapsed} via localStorage "hush-rail"
- Free drag + pointermove + clamp

**Resizable 1:1 / 1:n thread pane:**
- #thread-resize handle
- Floating hive panel, composer with pills + mention box
- paintThreadStream, openThreadPane, thread composer mentions
- reply_to threading
- Escape close, persist size

**Tests + checks:**
- make -C hush-c test → ALL PASS (agent mention reply ok)
- check_launch.sh: requires placeRailAtBrand, dblclick, thread-resize; forbids rail-docks
- check_pwa.sh passes
- UI_SPEC §13 (resizable thread, 1:1/1:n, effort low), §15 (free drag, no docks, brand home, thread parks rail)
- README updated (free-drag hamburger, no docks)

## Verification Evidence (executed this worktree)

- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (required greps)
- sh hush-c/tests/check_pwa.sh → PWA routes ok
- Explicit:
  - HUSH_AGENT_GROK_EFFORT "low" + --reasoning-effort
  - placeRailAtBrand, dblclick, thread-resize, thread-pills, thread-mention present
  - hush-rail free drag present
  - rail-docks / railAnchor count == 0
- No new C required for this verification slice

## Differences from original PLAN base

- Current base is later. Thread chat UX (resizable pane, 1:1/1:n, composer pills, reply_to), dockless rail with brand-home double-click, effort="low" were implemented in prior slices (thread-ux, oauth-mention-rail, agent work) and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the established pattern) to close PLAN_THREAD_CHAT_RAIL_UX.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria item.
No code changes needed.
Proceed to VERIFIED.md + commits on gb/* + full PR lifecycle per PRIME_DIRECTIVE.

## Commands executed
- git worktree add -b gb/thread-chat-rail-ux from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh + check_pwa.sh
- rg/grep for effort low, placeRailAtBrand, dblclick, thread-*, rail-docks absence, hush-rail
- Source + served verification
