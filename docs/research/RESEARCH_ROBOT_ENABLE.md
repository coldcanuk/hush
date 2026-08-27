# RESEARCH: Lock Major identity; enable/disable robots

**Date:** 2026-08-26
**Worktree:** `worktrees/robot-enable` / `gb/robot-enable`
**Status:** Synthesis complete.

## Problem

1. Major is the platform robot. Name and system prompt must not be editable.
2. Every robot (including Major) needs Enable/Disable in the edit dialogue.
3. Disabled robots show greyscale inventory icons.
4. Disabled robots must not answer mentions.

## Current (evidence)

- Payne profile update copies `in->name` / `in->prompt` (`hush_launch_update_payne_profile`).
- UI `openAgentDrawer` fills editable name/prompt for Payne; save POSTs them.
- `check_launch.sh` currently expects `name":"Major Two"` after a posted rename.
- No `enabled` field on `hush_roster_agent_t` or Payne session JSON.
- Inventory `.inv-item` has no greyscale class.
- Settings already has `.switch` / `.slider` CSS.
- `hush_agent_lookup_robot` always returns Payne/raised robots; no off switch.

## Decisions

- Lock Major name/prompt in UI (readonly) and ignore those fields in C.
- `enabled` int, default 1. Persist `payne_enabled` / `agent_enabled_N` in vibe.json.
- Session JSON `"enabled":true|false` on `payne` and each agent.
- UI switch `#agent-enabled` on every edit (including Major). Raise starts enabled.
- `.inv-item.disabled { filter: grayscale(1); opacity: 0.55; }`
- Lookup returns 0 for disabled robots so jobs never start.

## Non-goals

Deleting Major; changing ranked providers; skill/voice work; Raylib.
