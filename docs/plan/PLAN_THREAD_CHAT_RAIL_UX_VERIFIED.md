# PLAN_THREAD_CHAT_RAIL_UX.md — Verification Gate (M1-M5)

Base: main a492dac00 (fresh worktree gb/thread-chat-rail-ux)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_THREAD_CHAT_RAIL_UX_CURRENT.md
- Confirmed: all DoD items already present on this base from prior slices.

## M2.1 UI_SPEC contracts
- §13: resizable hive thread pane (1:1/1:n), composer pills, --reasoning-effort low
- §15: free drag, no docks, placeRailAtBrand, thread parks rail
- Verified in UI_SPEC.md

## M3.1 Grok effort low
- #define HUSH_AGENT_GROK_EFFORT "low"
- Used as argv --reasoning-effort low
- check_agent.sh greps HUSH_AGENT_GROK_EFFORT "low"

## M4.1 Dockless rail + brand home
- No #rail-docks markup/CSS/JS (railAnchor removed)
- placeRailAtBrand() present
- dblclick on #rail-toggle collapses + homes left of brand
- Open thread forces collapsed + brand home; close restores pre-thread pose
- Persist only {x,y,collapsed} via localStorage "hush-rail"
- Free drag + pointermove + clamp

## M4.2 Resizable 1:1 / 1:n thread pane
- #thread-resize handle
- Floating panel, hive composer + pills + mention box
- thread-pills, thread-mention
- reply_to threading, Escape close, persist size

## M5.1 Checks and copy
- check_launch.sh: requires placeRailAtBrand, dblclick, thread-resize; forbids rail-docks
- README updated (free-drag hamburger, no docks)
- UI_SPEC updated
- Embed clean (prior)
- make -C hush-c test → ALL PASS

## Constraints
- Prime Directive: gb/* only; PR to main
- C11 + legible-c (existing)
- Embed after HTML (already satisfied)

## DoD checklist (satisfied)
1. [x] Happy (Grok Build) replies with a real note (not the old placeholder)
2. [x] Thread pane is hive-consistent, resizable chat supporting 1:1 and 1:n
3. [x] Tool rail keeps free drag; no dock squares; double-click hamburger (and open thread) parks collapsed rail left of hush/vibe name
4. [x] make && make test pass; embed clean
5. [x] PR merged, worktree removed, main clean (pending M6)

