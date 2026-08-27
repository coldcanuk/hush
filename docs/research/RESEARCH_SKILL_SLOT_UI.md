# RESEARCH: Diablo II skill cycle + drag-slot onto robots

**Date:** 2026-08-27
**Worktree:** `worktrees/skill-slot-ui` / `gb/skill-slot-ui`
**Methodology:** RDAP Phase 1 — Research & Discovery
**Status:** Synthesis complete. Plan frozen in `docs/plan/PLAN_SKILL_SLOT_UI.md`.

## 1. Problem (user, verbatim)

The hive compiled and launched. The skill UI still sucked. The required analog is
Diablo II jewelry and potions:

1. Cycle through available skills the way you page potions / amulets.
2. Drag and slot a skill onto **the robot of your choice**, the way you load an
   amulet or ring onto a character paper doll.

That is not what shipped.

## 2. What shipped (evidence)

| Surface | Fact | Site |
|---|---|---|
| Agent drawer copy | “Cycle the armory like a Diablo II amulet, then wear it.” | `demo/index.html` `#agent-skills` |
| Cycle row | `◀` / `#skill-cycle` / `▶` paints **one chip** | `paintSkillCycle` |
| Armory | Dumps **every** unequipped skill as a wrapping chip wall, grouped by scope | `paintSkillBoard` |
| Loadout | Wraps equipped chips. Empty sockets are not painted. | `#skill-loadout` |
| Interaction | Click chip toggles equip/prune. **No pointer hold. No drag. No drop target.** | `makeSkillChip` |
| Robot tiles | `pointerdown` starts **inventory move** (`beginInvDrag`). Tiles are not skill sockets. | `renderInventory` |
| Persist | Equip lives in `equippedSkills` until Save Robot. Hive drop does not exist. | `attachLoadout` |
| Caps | 8 slots, char/complex watermarks, role wall — C already enforces | `hush_skill_try_equip` |
| Tests | HTML greps for `skill-armory`, `skill-loadout`, `skill-cycle`, `skillCycleIdx` | `check_launch.sh` |

The cycle row is a label, not a stash. The chip wall violates Hick (Quinn: ≤5
primary choices) and is the opposite of D2 item identity. Dropping a skill on a
robot is impossible.

## 3. Diablo II analog (locked)

Sources: Diablo Wiki paper doll; D2 inventory positions (`IN_MOUSE`, `E_NECK`,
`E_RING`, `IN_BELT`); existing hive inventory pointer drag.

| D2 | Hush |
|---|---|
| Left-click picks the item onto the cursor (`IN_MOUSE`) | `skillHeld` + ghost gem follows the pointer |
| Second click on a valid slot places it | Click/drop on a robot tile or a loadout socket |
| Inventory grid / belt pages items | Skill stash carousel (◀ current gem ▶, wheel, keys) |
| Character paper doll (amulet, 2 rings, …) | Robot paper doll: **8 sockets** around the portrait |
| Drop onto the mercenary / character | Drop onto **any** Robots Inventory tile |
| Right-click auto-equip | Drop on a robot with no slot index → first empty socket |
| Escape / click empty cell cancels | Escape / click empty stash returns the gem |
| Item identity is a square, not a tag list | `.skill-gem` 1×1 brass/cyan/violet square |

All skills are 1×1 (rings / amulets / potions). No Tetris. Occupancy is the 8
sockets + watermarks, already in C.

## 4. Why click-chips failed

1. Recognition died: 30+ system skills as text pills. No item.
2. Hick: the armory presents the whole catalog at once.
3. Fitts: chips are small, wrap, and sit inside a modal drawer. The robots you
   want to dress are **outside** that drawer.
4. No `IN_MOUSE` state, so there is nothing to slot.
5. Cycle still calls `makeSkillChip`, so ◀ ▶ is the same failed widget.

## 5. Architecture (chosen)

**Two surfaces, one catalog, one persist path.**

### A. Hive skill stash (new, always on the hive nav)

Sits under Robots Inventory. Compact belt:

- Title: “Skill stash”
- Help: “Cycle a skill, then drag it onto a robot.”
- `#hive-skill-prev` / `#hive-skill-cycle` / `#hive-skill-next`
- Current skill is a large `.skill-gem` (name + scope + summary tooltip)
- Neighbors optional and dim; never a catalog dump
- Catalog from `GET /api/skills` (same as the editor)

Drop target: `.inv-item` tiles. Held skill + `pointerdown` on a tile **must not**
start `beginInvDrag`. Drop `POST /api/agent` with `skill_0…` + `nskills` only
(name/prompt omitted so `hush_roster_apply_update` leaves them). Payne uses the
Payne slug path (no `action:update`). Locked templates refuse (clone first).
Payne is identity-locked but **skill-editable** (existing `update_payne_profile`).

### B. Editor paper doll (replace the chip wall)

Keep test ids:

- `#skill-cycle`, `#skill-cycle-prev`, `#skill-cycle-next`
- `#skill-armory` becomes the stash stage (gem + neighbors), **not** a chip dump
- `#skill-loadout` becomes 8 `.skill-slot` sockets (paper doll)

Layout of the doll:

```
 [0] [1] [2]
 [3] PIC [4]
 [5] [6] [7]
```

Empty socket = brass ring. Occupied = `.skill-gem`. Drag gem onto a numbered
socket (swap if occupied). Drag a worn gem off a socket to hold it (prune if
returned to stash). Click-to-toggle chips are gone.

Save Robot still posts `attachLoadout`. Hive drops persist immediately so
dressing a robot does not require opening the editor.

### Pointer model (D2, not HTML5 DnD)

Reuse the inventory pointer style (HTML5 `draggable` is a Non-goal; it fights
the existing `pointerdown` grid).

```
skillHeld = null | { skill, fromSlot? }
```

- Pointerdown on a stash gem → pick up (`IN_MOUSE`).
- Window `pointermove` → ghost follows.
- Pointerup / click on `.inv-item` or `.skill-slot` → place.
- Escape / drop on empty stash → cancel or prune.
- `beginInvDrag` returns immediately when `skillHeld` is set.

### Client watermark / role

Mirror `hush_skill_try_equip` before POST: role wall, 8 slots, chars, complexity.
On `HUSH_ERR_DENIED` / `HUSH_ERR_FULL`, Payne copy and keep the gem on the cursor.

## 6. Non-goals

- New C skill executor / progressive disclosure of SKILL.md bodies.
- Changing watermarks, catalog cap, or role enum.
- HTML5 DnD, Raylib, variable-size skill gems.
- Replacing the robots inventory move-drag (only gated while a skill is held).
- Live LLM forge.
- Per-slot types (amulet-only vs ring-only). All 8 sockets accept any role-legal skill.

## 7. Constraints

- Prime Directive: this worktree only; land via PR.
- UI via `hush-c/demo/index.html` then `scripts/embed-ui.sh demo`.
- C11 + write-legible-c if any `.c`/`.h` change (forge-skill copy in `hush_skill.c`).
- Keep `check_launch.sh` ids: `skill-armory`, `skill-loadout`, `skill-cycle`,
  `skill-cycle-prev`, `skillCycleIdx`, `attachLoadout`, `body.skill_0`, `nskills`.
- Quinn: Cognitive Load ≤ 3; Hick ≤5 visible choices on the stash (prev, gem, next).
- Payne Voice: “Cycle a skill. Wear it on a robot.”

## 8. Risks

1. **Inventory pointer clash.** Mitigation: `beginInvDrag` no-ops when `skillHeld`.
2. **Locked templates silently ignore skill POSTs** (`hush_roster_update_agent`).
   Mitigation: client refuses drop; copy “Clone it to wear new skills.”
3. **Payne `locked:true` in `robotModels()`** is identity, not skills. Do not reuse
   `skillLocked` for hive Payne drops.
4. **Drawer-only cycle** would miss “robot of our choice.” Hive stash is mandatory.
5. **Chip greps.** Do not delete required ids; restyle them.

## 9. Synthesis / updated remaining plan

Ship a hive stash carousel + cursor-held gem + drop onto robot tiles, and replace
the editor chip wall with an 8-socket paper doll around the portrait. Persist
hive drops immediately through the existing `skill_N` POST. No catalog dump.
Keep C watermarks/role wall. Tests grow HTML greps for hold/drop/slots.

See `docs/plan/PLAN_SKILL_SLOT_UI.md`.
