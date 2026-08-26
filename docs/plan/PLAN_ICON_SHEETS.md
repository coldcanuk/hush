# PLAN: six-sheet robot picture picker

Branch: `gb/icon-sheets`
Worktree: `worktrees/icon-sheets`
Base: `main` `b583af502`
Land via PR only.

## Scope

Replace the 31-tile equipment crop picker with the six 8×8 PNG sheets.
Serve them over HTTP. Store `panel:<sheet>:<index>` on the agent `picture`
field. Cats sheet is in. Raylib is examples-only.

## Architecture (frozen in RESEARCH_ICON_SHEETS.md)

PNG objects linked separately from `hush_ui_html.h`. URLs
`/icons/icon_panel_{dogs,cats,sheep,virus,robots,angevin}.png`.

## Phases

### Phase 0 COMPLETE
Worktree `worktrees/icon-sheets`.

### Phase 1 GATE
This file + research. Commit Milestone 1.1.

### Phase 2 / 3 Sheets + picker
Copy PNGs; embed/link; HTTP routes; picker tabs; inventory sprite; UI_SPEC;
embed HTML; tests.

### Phase 4 Verify
`make test`; curl PNG magic; picture id round-trip; optional Playwright.

### Final
PR to main; remove worktree.
