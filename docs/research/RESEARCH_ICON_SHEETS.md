# RESEARCH: six 8×8 PNG robot-picture sheets

Worktree: `/opt/repo/hush/worktrees/icon-sheets`
Branch: `gb/icon-sheets`
Base: `main` `b583af502`

Prime Directive isolation (`worktrees/<slug>` + PR), not `../gb-*-wt`.

## Sources (`~/Pictures/hush_icons/`)

`identify` — all six are **1024×1024** PNG, 8-bit sRGB:

| File | Bytes | Visual |
|---|---|---|
| `icon_panel_dogs.png` | 916976 | 8×8 animal-class grid (mech…royal × ranks) |
| `icon_panel_cats.png` | 1030196 | Same layout, cats |
| `icon_panel_sheep.png` | 1026403 | Same layout, sheep |
| `icon_panel_virus.png` | 746734 | Phage / injector objects, uniform cells |
| `icon_panel_robots.png` | 756958 | Chassis, drones, cores — **not** an infographic |
| `icon_panel_angevin.png` | 818187 | 12th-c. court/army units |

Total PNG payload ≈ 5.30 MiB. These **are** sprite sheets (no HUD, no title bar).
Unlike the old JPEGs, `atlasRec` math is valid:

`sx = (index % 8) * 128; sy = (index / 8) * 128` — 64 cells/sheet, 384 total.

JSON `inner_pad_px` is **16**; README/`icons.h` say **8**. Cell origin is still the
128px grid. Picker CSS uses full 128px cells (pad is inside the art). Do not
invent a third grid.

`hush_icon_atlas.json` workstation `path` fields are **source only**. Runtime
URLs must be hive HTTP paths, not `/home/chuck/Pictures/...`.

## Feline

`UI_SPEC.md` line 13: “No feline.” OBJECTIVE ships `icon_panel_cats.png`.
**Spec change:** picture picker may show the cats sheet. Quinn “no feline” no
longer applies to robot pictures.

## Current hive

Picker is exclusive `ATLAS_N = 31` + `/agent-atlas.png` (512×256 crop).
`picture` ids are `atlas:N`. Inventory CSS sprites that crop only.

## Architecture (frozen)

1. Copy the six PNGs into `hush-c/demo/icons/` (and optional
   `examples/inventory-raylib/assets/`).
2. Link PNG bytes into `hush-relay` as separate objects (not into
   `hush_ui_html.h` — that rebuilds on every HTML edit).
3. Serve `/icons/icon_panel_<sheet>.png` (200, `image/png`).
4. Picker: six sheet tabs, 8×8 cells each. Id: `panel:<sheet>:<index>`
   (sheet ∈ dogs,cats,sheep,virus,robots,angevin; index 0..63).
5. Inventory tiles honor `panel:` ids with 8×8 CSS sprite math (1024 source).
6. Legacy `atlas:N` still paints if `/agent-atlas.png` remains; picker does
   not offer it.
7. SuperGrok `icons.c` / `example_raylib.c` live under `examples/` only.
   `icon_sheet_path` uses `assets/<filename>` (cwd = example dir). No raylib
   in `hush-c/Makefile`.

Remaining plan: [`../plan/PLAN_ICON_SHEETS.md`](../plan/PLAN_ICON_SHEETS.md)
