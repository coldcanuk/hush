# RESEARCH: conversation fidelity + robot pictures without Raylib hive UI

Worktree: `/opt/repo/hush/worktrees/conv-fidelity-pics`
Branch: `gb/conv-fidelity-pics`
Base: `main` `64ed68f5f`

Prime Directive isolation (`worktrees/<slug>` + PR), not `../gb-*-wt`.

## Why Raylib was dropped as the hive renderer

Not because inventory is a bad idea. Because **the product is an
embedded C11 relay** that serves one HTML UI (`HUSH_UI_HTML` in
`hush_ui_html.h`). A Raylib window would be a **second native GUI**:
new system dep (`libraylib`), a second event loop, no PWA/embed path,
and the Gemini worktree (`../hush-inventory-ui`) violated the Prime
Directive.

`pkg-config --exists raylib` is **NO_RAYLIB** on this host.
`examples/inventory-raylib/` is README-only (no `main.c`).

**Replacement lane (locked):** slice or sprite the icon panels and
serve tiles over HTTP from the relay (same as `/icon-192.png`). The
human picks a picture in the agent drawer. Occupancy grid stays CSS/JS.
Raylib stays an optional desktop prototype, never a `hush-c` dependency.

How the end user gets a robot pic **without Raylib:** picker UI →
`POST /api/agent` already accepts `picture` (path/id, 256 bytes) →
session JSON should echo it → inventory tile uses that URL.

## The three Gemini JPEGs (2816×1536, ~3.5MB each)

Inspected visually and with `identify`.

| File | What it actually is | Sliceable as a sprite sheet? | Spec “No feline”? |
|---|---|---|---|
| `icon_panel_mqbw72mqbw72mqbw.jpeg` “EQUIPMENT & AGENT ATLAS” | Nearest to a **4×8 icon grid** inside HUD chrome (title bar, circuit frame). Includes **one cat** (row 3, col 5). | **Usable with crop.** Inner cells are square-ish. HUD and the cat must not be picker tiles. | Cat cell must be skipped. |
| `icon_panel_robots_6zfxz56zfxz56zfx.jpeg` “MECHANICAL UNIT INVENTORY” | **Infographic**, not a grid: hero portraits, labels, QUANTITY/THREAT, mixed cell sizes. Several **cats** (Felix-unit). | **No.** `atlasRec` math would slice chrome and text. | Fails UI_SPEC. **Redesign.** |
| `Icon_Panel_Virus_csw6plcsw6plcsw6.jpeg` “BIOLOGICAL AGENT INVENTORY” | Infographic with HUD labels between columns. | **No.** | N/A (no cats) but still not a sheet. **Redesign.** |

Do **not** git-add the 3.5MB sources. Slice a small PNG atlas from the
equipment inner cells (skip cat) for the v1 picker.

### Redesign spec (robots + virus sheets) — for a later human/Gemini pass

If we redo those two panels, deliver **sprite sheets only**:

1. Canvas **1024×1024** PNG (not JPEG), sRGB, no title banner, no
   quantity/threat HUD, no circuit frame, no body-text labels.
2. **Uniform cells:** 8×8 grid, each cell **128×128** px, **8px**
   transparent (or solid `#0f1419`) gutter **inside** the cell so a
   112×112 glyph sits centered. Total: 64 icons per sheet.
3. Filename: `agent-robots-8x8.png`, `agent-bio-8x8.png`.
4. Each cell: one subject, front 3/4, cyberpunk-steampunk (brass +
   cyan neon), dark industrial void background. **No cats, no kittens,
   no Felix.** Allowed: humanoid chassis, canine/avian/insect/drone
   machines, geometric cores, phage-like devices — not cute felines.
5. No watermarks, no “Type Alpha” captions on the art (names live in
   JSON).
6. Equipment redo (optional): same 8×8 / 128px rules; drop the cat;
   keep sword/armor/drone/vial vocabulary.

Until that lands, v1 picker uses **sliced equipment cells minus the cat**.

## Conversation defects still in source (quoted)

**E1.** `applyMention` deletes `@` from the textarea and
`composerPills.push`. Submit does `pills.map(nostr:).join(" ") + leftover`
(`demo/index.html` ~2722–2799 and thread twin). That is the original
order-destroying path.

**E2.** `paintNote` always paints 👍/🎯 immediately. `thinkLabel` can say
“is thinking”; **“reacting” does not exist** in JS except a CSS comment.

**E3.** On-deck intro guard is one static `last_hex`/`last_root` pair
(`hush_agent.c` ~660). 1:n can re-intro.

**E4.** Channel `about` persists and injects `Channel topic:`, but
HTTP manage does not set it and the hive has no topic pills.

**E5.** Agent `picture` field exists on roster + POST body; session
agent JSON **omits** `picture`.

## Remaining plan

[`../plan/PLAN_CONV_FIDELITY_PICS.md`](../plan/PLAN_CONV_FIDELITY_PICS.md)
