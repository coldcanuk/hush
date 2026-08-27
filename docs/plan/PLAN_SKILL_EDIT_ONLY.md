# PLAN: Skills only in Raise / Edit (not hive nav)

**Branch:** `gb/skill-edit-only`
**Correction to:** PR #115 hive Skill stash on the sidebar.

## Decision

The hive nav is the overworld. Skills belong on the robot Raise/Edit panel,
the same way Diablo II inventory is `i`, not a HUD belt.

## DoD

- No `#hive-armory` / `#hive-skill-cycle` on the main nav.
- Cycle + paper-doll sockets remain in `#agent-drawer`.
- `i` toggles Edit for the selected or hovered robot tile (ignored while typing).
- `make test` + launch greps forbid the hive stash.
