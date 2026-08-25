# inventory-raylib (optional prototype)

Decision (M2): Primary robots inventory is a pure web/CSS/JS grid
embedded in the existing SPA (demo/index.html served by C relay).
This preserves the single-binary legible C11 model, no new
system dependencies for normal builds, and full compatibility with
X11/undecorate/PWA flow.

This directory holds a standalone Raylib reference implementation
matching the user-provided Gemini spec + example code:
- 10x5 (or configurable) grid
- Drag & drop + snap + collision (IsSpaceFree)
- Variable size agents (1x1,1x3,2x2,2x3...)
- Cyberpunk meets Steampunk theme (COLOR_BG, neon, brass)
- Texture atlases (copy user JPEGs or use colored rects)
- Agent struct with gridX/Y, w/h, texture*, atlasRec, rect, isDragging

Build only if raylib is installed:
  gcc main.c -o inventory `pkg-config --cflags --libs raylib`

Assets (optional):
  cp ~/Pictures/hush_icons/*.jpeg assets/   # then slice in code

See also: PLAN_CHAT_ROBOTS_INVENTORY.md (M4.5 web primary, M4.6 stretch)
and RESEARCH_CHAT_ROBOTS_INVENTORY.md.

Do NOT add raylib to hush-c/ Makefile or configure as a hard dep.
