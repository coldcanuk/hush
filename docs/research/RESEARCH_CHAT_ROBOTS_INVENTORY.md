# RESEARCH: Chat Flow, Multi-Robot Deliberation, Mention Fidelity, Developer Logging, Tool Rail, and Inventory-Style Robots UI

**Date:** 2026-08-25 (worktree gb/chat-robots-inventory-ui)
**Methodology:** RDAP Phase 1 – Research & Discovery
**Authoritative for this feature slice.** Supersedes partial notes in PLAN_GROUP_MENTION_SEAM, PLAN_ROBOT_CARDS_UX, PLAN_THREAD_CHAT_RAIL_UX, UI_SPEC, and earlier research.
**Status:** Synthesis complete; plan frozen after this gate.

## 1. Problem Statement (verbatim user feedback)
- Conversation "5/10". Instant emoji ack too fast; needs "thinking", "reacting", gradient to success.
- "Mention received." and repeated "At ease. I am on deck..." are logs, not chat. They pollute visible thread.
- Mentions order destroyed: `@Happy tell joke. @Sgt was it funny?` becomes pills that lose sentence position/context.
- Multi-robot: when co-mentioned, robots must ack (emoji), then deliberate among themselves (via mentions) to decide strategy (individual/cooperate/split/full convo). They can mention peers back.
- Developer Logging: separate toggle (Settings, default OFF) + window/panel. Syslog format when enabled. Logs (mentions, intros, debug) only visible there.
- Channels: topics (pills) + per-channel config/system prompt pointers already partially exist; ensure + expose in UI.
- Tool rail UX: needs human-friendly micro-tweaks.
- Robots section: NOT list of cards. Must be Diablo/Vein-style inventory grid: create/raise, drag-drop to slots, grid snap, occupancy, variable sizes (1x1,1x3,2x2,2x3), cyberpunk+steampunk aesthetic, neon/brass, texture atlases.
- Example Raylib code + Gemini instructions provided for inventory.

## 2. Current Architecture Snapshot (from code inspection + execution)
- **Core:** legible C11 Nostr relay (hush-relay). Single binary serves embedded SPA (hush-c/demo/index.html ~4.2k LOC) + REST/JSON + Nostr line protocol.
- **UI delivery:** `scripts/embed-ui.sh demo` -> `src/hush_ui_html.h` (C string + icons). Rebuilt on make when HTML changes. `hush_http.c` serves it.
- **Chat model:** Notes (kind 1), threads via e/h tags. Robots dispatched on p-mentions via `hush_intel_consider` + `hush_agent_consider`.
- **Mentions & Acks (post M1/M3 seams):**
  - Server collects p-tags into `ev.mentions[]` JSON (hush_http.c:523+).
  - UI: `mentionedRobots(ev)` prefers `ev.mentions`, falls back to content scan.
  - Acks rendered AFTER note body as `.robot-acks` pills (👍 🎯 🏆) using `mentionedRobots`.
  - `prettyMentions(text)` (index.html:3925): `replace(/nostr:(npub1...)/gi, ...)` -> global @name. Called before `textContent`. Destroys original positions and @ order.
  - `paintNote` uses `splitFences(prettyMentions(...))` + textContent chunks. Order/mention intent lost.
- **"Mention received." and intros:**
  - `hush_intel.c:670`: `hush_intel_post_line(store, ev, hex, "Mention received.");` → inserts visible note (kind 1) authored by robot hex.
  - `hush_agent_on_deck` (agent.c:640): always emits "At ease. %s — %s" on first hold/release.
  - `hush_roster.c` and launch also seed Payne intros.
  - These become normal chat events visible in stream/thread.
- **Co-mention / group awareness (from PLAN_GROUP_MENTION_SEAM work):**
  - `hush_agent_job_t` has `co_npubs[4]`, `n_co_robots`.
  - `hush_agent_fill_job` (agent.c:868): walks parent p-tags, skips self/human, populates co for robots.
  - Prompt injection: " Other robots mentioned: nostr:..." (897).
  - Finish path (1142): adds p-tags for co if LLM echoed nostr:npub.
  - Still independent dispatch per robot; no server-side group leader yet. Deliberation via LLM prompt + p-mentions back.
- **Channel topics / system prompts:**
  - `hush_launch.h`: `channel_t.about[128]`, `robot_reply`, `robot_talk`.
  - `hush_launch_channel_about` exposed.
  - Injection in `hush_agent.c:933`: if about, append " Channel topic: <ab>" to job->prompt (after base prompt).
  - Topics are pills in UI (per prior plans); stored as channel about. Used as "quick pointers for the LLM".
- **Logging / visibility:**
  - No dedicated "Developer Logging" toggle or separate stream.
  - Intel uses `post_line` for acks/denies (visible notes).
  - Status/thinking exposed in /session JSON for "thinking" dots.
  - No syslog formatting or out-of-band log window.
- **Tool rail:**
  - Floating `#tool-rail` (draggable via #rail-grip, collapse, popovers with i buttons).
  - Buttons: Profile, Settings, Call, Invite, Add Channel, Configure Providers, New Robot, New Project, Minimize/Maximize, Close/Exit.
  - UX is functional but per user: needs "small tweaks that make it human friendly".
- **Robots section (current):**
  - Sidebar `#robot-list` (nav): `paintRobots()` renders `.robot-card` articles (expand/collapse with +/-).
  - Cards show name, slug/npub short, provider, prompt preview, context count, actions (edit/delete).
  - Matches PLAN_ROBOT_CARDS_UX (expand, pills for prompt/context, provider radios).
  - NOT spatial inventory. No grid, no drag-drop, no slots, no variable size items, no Diablo/Vein aesthetics.
- **Build / native:**
  - Makefile (hush-c/): gcc C11 strict, optional X11 (hush_win.c for undecorate/min/max), no Raylib/SDL.
  - configure detects gcc/gmake/openssl/X11, writes config.mk.
  - UI is web (Chromium --app or PWA). Native drawing limited to X11 hints + canvas FIM (hush_canvas.c for grok fill).
  - Assets: only hush-relay icons (png hicolor). User-provided JPEG atlases live in ~/Pictures/hush_icons/ (icon_panel_* for equipment/virus/robots).
- **Raylib feasibility:**
  - Not present. Adding would require:
    - pkg-config / system dep (libraylib-dev or build from src).
    - New binary or window (separate from embedded web UI?).
    - Texture loading (JPEG via raylib or stb), atlas slicing (grid positions).
    - Event loop integration (current is poll + http + X11).
    - Cross platform (Linux primary; FreeBSD/OpenBSD pkgs).
  - High risk for "legible C11" core + single binary model.
  - Alternative paths: pure HTML/CSS/JS grid inventory (easier embed, same aesthetic via CSS vars + canvas or div grid), or raylib subproc window launched from rail.
  - Gemini spec: 1024x768 window, 10x5 grid, CELL_SIZE=64, drag state machine (press/down/release), IsSpaceFree/UpdateGridOccupancy, DrawTexturePro + neon borders + names, cyber/steampunk colors.

## 3. Evidence (key excerpts, file:line)
- prettyMentions destroys order: demo/index.html:3925 (`replace` global, then textContent).
- paintNote + acks: 3722 (pretty), 3108 (mentionedRobots from ev.mentions), 228 (CSS .robot-acks).
- Mention ack emission: hush_intel.c:670 (post_line), 615 (is_mention_only guard).
- on_deck: agent.c:655 ("At ease..."), 668 (insert_note), roster.c:593, launch.c:1103.
- Co-robots: agent.c:98 (struct), 870 (fill), 897 (prompt inj), 1142 (p-tags on reply).
- Topic injection: agent.c:933 ("Channel topic: "), launch.c:925 (about accessor), 1380 (persist).
- Robots render: demo/index.html:1723 (paintRobots), 707 (#robot-list), 391 (.robots CSS).
- Tool rail: 571 (#tool-rail), drag/collapse code later in file.
- Embed: scripts/embed-ui.sh (python reads demo/ -> C header).
- No raylib: hush-c/Makefile, configure (no ray checks), rg inventory only in docs.
- Session/thinking: http.c:479 (thinking JSON), agent status.

## 4. Constraints & Assumptions
- **Hard:** C11 -Wall -Wextra -Werror -Wconversion -Wshadow. write-legible-c for all .c/.h. Worktree + PR only (PRIME_DIRECTIVE). RDAP (research gate at P1 end, atomic M with commits, exact CLI/verif in every Task).
- **UI:** Must keep embed model. Changes to index.html require embed rebuild + make.
- **Mentions fidelity:** Preserve original text positions for human intent. Pills for display only (or non-destructive mapping).
- **Robots inventory:** User wants Diablo/Vein "inventory selection". Must support create/raise -> inventory slots. Raylib example is reference; feasibility decision in P2.
- **Multi-robot:** Leverage/extend existing co_npub seam. Add deliberation prompt/hygiene. Acks via emoji only for "Mention received".
- **Logging:** New "Developer Logging" (default disabled). Separate window/panel. Syslog fmt when enabled. "Mention received", intros, debug go there.
- **Assets for inventory:** Copy user JPEGs into worktree/assets/ for build (per Gemini). But respect repo layout (assets/icons/ exists for relay pngs).
- **Non-goals (this slice):** Full LLM rewrite, new provider runtime, desktop/mobile legacy, complete visual theme pass beyond inventory + rail + chat fixes.
- **Risks (high):**
  1. Mention text fidelity + pill UX without breaking composer/thread.
  2. Raylib dep vs current web-first architecture (may choose JS grid for integration speed + legibility).
  3. Progressive states ("thinking/reacting") must map to existing status/thinking without new polling.
  4. Separate log window in embedded web (use drawer/canvas-like panel; or native if raylib chosen).
  5. Co-mention deliberation may need prompt hygiene + tests to avoid loops.

## 5. Prior Work Leveraged
- Group mention seam (co_npubs, prompt inj, p-tag roundtrip).
- Robot cards UX (expand, pills, provider, context max 3).
- Thread UX, rail, close/exit, provider config.
- Channel groups + policy.
- All RDAP plans/research in docs/.

## 6. Synthesis Gate Output
This doc + the accompanying PLAN_CHAT_ROBOTS_INVENTORY.md (with full RDAP Phases/Milestones/Tasks) constitute the gate.
All subsequent implementation follows the frozen plan.
Verification of research: cross-checked via rg/shell/cat on key paths (intel, agent, http, launch, roster, demo/index.html, Makefile, configure, embed).

## 7. Next (per plan)
- P2 Architecture (data models for dev-log, mention preservation, inventory state, channel topics; decide Raylib vs HTML grid; update UI_SPEC).
- Then implementation milestones (small, commit each on branch).
- Final: tests, polish, PR per PRIME_DIRECTIVE.

**End of RESEARCH synthesis.** Commit this + plan before code changes.