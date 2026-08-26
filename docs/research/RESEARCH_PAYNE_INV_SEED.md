# RESEARCH: Payne/Happy in inventory + 4×3 compact + Seed wizard

Worktree: `/opt/repo/hush/worktrees/payne-inv-seed`
Branch: `gb/payne-inv-seed`
Base: `main` `0bc5985c0`

Prime Directive path (`worktrees/<slug>` + PR), not `../gb-*-wt`.

## Why Payne and Happy are missing

**E1.** `#robot-list` is `style="display:none"` (`hush-c/demo/index.html:745`).
`paintRobots()` still fills that hidden list from `robotModels()`, which
**does** include `session.payne` (locked slug `sgt-major-payne`) and
`session.agents` (e.g. Happy). The human cannot see them.

**E2.** Visible UI is `#robot-inventory`, backed by a **parallel**
`invItems` array. Default `invItems = []`. Payne is not inserted on
vibe-ready. Happy is only added if `agent-save` calls `addToInventory`
after raise — a 120ms `session.agents.slice(-1)` hack, not a roster
sync. Cold load / tick `paint()` calls `renderInventory()` on empty
`invItems`.

**E3.** Seed currently runs `seedInventoryDemo()` which pushes **fake**
tiles named Happy / Sgt Major Payne / Cipher with slugs like
`sgt-major-payne` that are **not** `POST /api/agent` robots.

**E4.** C already ships Payne: `HUSH_LAUNCH_PAYNE_NAME` /
`HUSH_LAUNCH_PAYNE_SLUG`. `check_launch.sh` asserts session JSON
contains `"Sgt Major Payne"` after vibe. The bug is UI occupancy, not
the roster.

## Compact grid vs spec

Live CSS/JS: `repeat(8, 38px)`, `INV_COLS = 8`, `INV_ROWS = 5`.
`UI_SPEC.md` §4 still says default **10×5**. Sidebar `nav` is
`grid-template-columns: 220px` with 10px padding → ~200px inner.
8×38px already overflows. OBJECTIVE **4×3 compact** wins; spec must
be updated. Expanded view may be 8×5 in a drawer, not in the nav.

## Seed as Payne team prompt (architecture)

No C job exists that “Payne LLM creates agents.” Gate on Payne having
a provider that can actually run (`has_home` / `has_binary` / `has_key`
on `/api/provider` for Payne’s `providers[]`). Wizard builds a prompt
from actions / skills / `{The Project}` / channel choice. Submit uses
existing `POST /api/agent` (≥2 cooperating robots) and
`POST /api/channel` (+ manage roster) with Payne credited as seeder
in the system prompt. Do not invent a second roster. Do not keep
`seedInventoryDemo` as the Seed click path.

## UI / HRI (short)

- Chief of Staff is **always visible** once the hive is ready (status,
  not a Seed easter egg).
- Compact inventory is a **glanceable** 4×3; Expand is the “open bag”
  pattern.
- Seed is a **briefing**, not a demo dump: Payne needs a working LLM
  first (honest refusal).
- Equal Seed / Clear / Raise targets (same class, same width).

## Remaining plan

[`../plan/PLAN_PAYNE_INV_SEED.md`](../plan/PLAN_PAYNE_INV_SEED.md)
