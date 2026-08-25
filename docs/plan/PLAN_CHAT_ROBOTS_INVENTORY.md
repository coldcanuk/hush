# PLAN: Chat UX Fixes (Progressive Acks, Mention Order, Logs vs Chat, Multi-Robot Deliberation) + Developer Logging + Channel Topics + Tool Rail + Inventory Robots UI (Diablo/Vein Grid)

**Branch:** gb/chat-robots-inventory-ui  
**Worktree:** worktrees/chat-robots-inventory-ui  
**Base:** main (post #84 pills)  
**Methodology:** RDAP (Research-Driven Adaptive Planning) — hybrid Double Diamond + Spiral + Agile atomic milestones.  
**Gate:** Phase 1 synthesis complete (see RESEARCH_CHAT_ROBOTS_INVENTORY.md). This plan is frozen post-audit.  
**Prime Directive:** Worktree only. Commits/pushes on gb/*. PR + auto-merge to land. Delete WT after.  
**C Standard:** C11 strict + write-legible-c on every .c/.h change.  
**UI:** All HTML/JS changes via demo/index.html then `sh ../scripts/embed-ui.sh demo`.  
**Verification:** make test + manual + check_* scripts per task.  
**DoD:** All explicit user issues (1-3 + sub) resolved to "natural conversation flow" + working inventory grid. 10/10 target.

## Scope
### Primary Goals
1. Conversation UX: progressive mention acks (thinking/reacting/emoji gradient), no "Mention received." or repeated intros in visible chat, single intro only, preserve @mention order/position in rendered content (intent not destroyed by pills).
2. Developer Logging: toggle (Settings, default OFF) + separate drawer/window/panel. Syslog format when enabled. Logs (mentions, intros, debug) only visible there.
3. Multi-robot co-mention: acks (emoji), then robots deliberate (via prompt + p-mentions back) to decide reply strategy (per, cooperate 1, split, full convo). Robots can mention peers.
4. Channels: topics (pills) + per-channel config/system prompt pointers already partially exist; ensure + expose in UI.
5. Tool rail: small human-friendly UX iterations (tooltips, affordances, Fitts, progressive disclosure).
6. Robots section: replace list cards with inventory-style grid (drag-drop, snap, occupancy, variable sizes, create/raise flow). Cyberpunk/Steampunk aesthetic. Support the provided Raylib model as reference; primary integration via web grid for embed compatibility. Optional Raylib prototype binary.

### Non-Goals (this slice)
- Full agent LLM backend rewrite or new runtimes.
- Mobile / legacy desktop ports.
- Complete visual theme overhaul (only inventory + rail + chat fixes).
- Production texture assets (use placeholders + user atlases for prototype).
- Raylib as hard dependency (optional / example).

### Success Criteria (measurable)
- @Mentions in same bubble keep original order/positions in UI (text or non-destructive overlay).
- "Mention received." never appears in main chat stream (only in dev log when enabled).
- Robot intros appear at most once per robot per session (on_deck guard).
- Progressive states visible for mentions (status updates before final emoji ack).
- Co-mentioned robots exchange mentions and produce coordinated or individual replies.
- New "Developer Logging" switch in Settings; when off, no log notes in chat; when on, dedicated panel streams syslog lines.
- Channel topic pills affect prompt (verified in job).
- Tool rail has at least 3 micro-UX improvements (e.g. better labels, drag hints, discoverability).
- Robots UI: openable inventory grid (min 4x3 or 10x5 cells), drag-drop agents of variable sizes (1x1,1x3,2x2), snap/collision, create flow, cyber/steampunk colors (neon + brass), renders names/icons.
- make test passes; check_agent.sh / check_launch.sh / manual thread tests pass.
- Worktree lifecycle + RDAP commits followed exactly. PR lands on main.

### Constraints
- C11 strict flags. legible-c §14 checklist on C.
- Embed UI model preserved (no breaking the single HTML served by relay).
- No direct writes to main.
- Small atomic Tasks. Commit after every Milestone on branch.
- Research gate (P1) already passed via synthesis.

### Top Risks + Mitigations
1. Mention text fidelity in prettyMentions/paint — mitigate by new preserve_mentions renderer + optional pill map.
2. Raylib integration complexity — mitigate: web grid primary; Raylib as docs/example + optional make target if raylib present.
3. Log separation without losing debuggability — dedicated panel + toggle; keep intel post_line for dev only.
4. Multi-robot deliberation loops — hygiene + prompt rules + tests.
5. Asset paths for inventory (~/Pictures vs repo) — copy to worktree/assets/ during P3/P4; document.

### Required Env
- Linux (Pop/Ubuntu/Debian primary). gcc, gmake, X11 headers optional.
- For Raylib prototype: libraylib + headers (optional; tasks will probe).
- User assets in ~/Pictures/hush_icons/ (for copy task).
- gh CLI for PR at end.

## Phase 0 – Environment & Isolation Setup (COMPLETE)
**Already executed in this worktree session:**
- git checkout main && git pull --ff-only
- git worktree add -b gb/chat-robots-inventory-ui worktrees/chat-robots-inventory-ui
- cd into it; verified pwd contains /worktrees/, branch=gb/..., clean, git worktree list.
- worktree skill loaded.
- Initial todo written with all explicit/implicit reqs.
- Multiple isolation confirms.

**Verification (re-runable):**
```bash
pwd | grep -q 'worktrees/chat-robots-inventory-ui' && git branch --show-current | grep -q 'gb/chat-robots-inventory-ui' && echo "P0 ISOLATED OK"
git worktree list
git status
```

**Milestone P0.M1:** Isolation complete. (No commit needed here; first research commit in P1.)

## Phase 1 – Research & Discovery (GATE)
**M1.1 Research current chat/mention/agent/intel paths (C + JS)**
- Task 1 of M1.1: rg + cat key paths for mention dispatch, post_line, on_deck, prettyMentions, paintNote, mentionedRobots, co_npubs.
  ```bash
  cd /opt/repo/hush/worktrees/chat-robots-inventory-ui
  rg -n --type c 'Mention received|on_deck|post_line|handle_mention' hush-c/src/hush_intel.c hush-c/src/hush_agent.c
  rg -n 'prettyMentions|paintNote|mentionedRobots' hush-c/demo/index.html
  ```
  **Verification:** Output contains lines 670 (intel), 640/655 (agent on_deck), 3925 (pretty), 3108 (mentioned), 1723 (paintRobots). Save excerpts to notes or this plan.
- Task 2 of M1.1: Inspect co-robot logic and topic injection.
  ```bash
  sed -n '860,920p;1130,1160p' hush-c/src/hush_agent.c
  rg -n 'Channel topic|about' hush-c/src/hush_agent.c hush-c/src/hush_launch.c
  ```
  **Verification:** co_npubs populated, prompt inj present, topic append at 933-946.
- Task 3 of M1.1: Map tool rail + settings + current robots render.
  ```bash
  grep -n 'tool-rail|paintRobots|robot-card|settings' hush-c/demo/index.html | head -20
  ```
  **Verification:** #tool-rail, paintRobots at ~1723, #robot-list, settings switches.
- **Milestone commit after M1.1:** (part of full P1 gate)

**M1.2 Research UI embed, build, assets, Raylib feasibility**
- Task 1 of M1.2: Review embed, Makefile, configure for UI + native.
  ```bash
  cat scripts/embed-ui.sh | head -30
  head -60 hush-c/Makefile
  grep -E 'raylib|X11|LDFLAGS' hush-c/Makefile configure || true
  ```
  **Verification:** Embed python -> C header. No raylib. X11 optional for win.
- Task 2 of M1.2: Probe user assets + current icons.
  ```bash
  ls -l ~/Pictures/hush_icons/ 2>/dev/null || echo "no pics (will copy in impl)"
  ls -R assets/
  ```
  **Verification:** 3 JPEGs listed or note "to copy".
- Task 3 of M1.2: Read provided Raylib example + Gemini spec (in query). Note data structs (Agent gridX/Y/w/h, texture*, atlasRec, isDragging), drag state machine, colors (COLOR_BG etc), DrawTexturePro.
  **Verification:** Notes in RESEARCH or this plan capture 10x5, snap logic, IsSpaceFree.
- **Milestone commit after M1.2**

**M1.3 Research channels, logging, multi-robot UX, tool rail pain points**
- Task 1 of M1.3: Channel policy + topic.
  ```bash
  rg -n 'robot_talk|about|topic' hush-c/include/hush_launch.h hush-c/src/hush_launch.c
  ```
- Task 2 of M1.3: Current log emission points + absence of dev toggle.
  ```bash
  rg -n 'post_line|syslog|developer|log panel' hush-c/src/ hush-c/demo/index.html || true
  ```
- Task 3 of M1.3: Existing group mention plan + thread UX.
  ```bash
  cat docs/plan/PLAN_GROUP_MENTION_SEAM.md | head -40
  ```
- **Milestone commit**

**M1.4 (optional depth) Probe integration options for inventory (web vs native)**
- Task 1 of M1.4: Experiment (read-only): can we add optional raylib target?
  ```bash
  echo 'int main(){return 0;}' > /tmp/rayprobe.c && (pkg-config --exists raylib && echo HAS_RAYLIB || echo NO_RAYLIB) || echo "pkg-config absent"
  ```
  **Verification:** Record result (likely NO for clean env).
- **Milestone commit**

**M1.5 SYNTHESIS GATE (MANDATORY LAST OF PHASE 1)**
- Task 1 of M1.5: Write (or already wrote) comprehensive RESEARCH_CHAT_ROBOTS_INVENTORY.md under docs/research/.
  (Already created with structure: problems, arch snapshot, evidence lines, constraints, risks, prior work.)
  ```bash
  ls -l docs/research/RESEARCH_CHAT_ROBOTS_INVENTORY.md
  wc -l docs/research/RESEARCH_CHAT_ROBOTS_INVENTORY.md
  ```
  **Verification:** File exists, >100 lines, contains key excerpts (670, 3925, co_npubs etc).
- Task 2 of M1.5: Produce this frozen PLAN_CHAT_ROBOTS_INVENTORY.md (Phases/Milestones/Tasks with every Task having: ref e.g. "Task 3 of M2.1", exact CLI or cat <<'EOF'..., verification step).
  (This file.)
  ```bash
  ls -l docs/plan/PLAN_CHAT_ROBOTS_INVENTORY.md
  head -30 docs/plan/PLAN_CHAT_ROBOTS_INVENTORY.md
  ```
  **Verification:** Plan present, contains "Phase 0", "M1.5", "Task X of M", "Verification:", worktree lifecycle, audit section.
- Task 3 of M1.5: Audit the plan (self + checklist).
  - [ ] Every Task refs Milestone.
  - [ ] Every Task has exact CLI/code + verif.
  - [ ] Research gate present (M1.5) and clear.
  - [ ] Full worktree/PR lifecycle documented.
  - [ ] Tasks small/atomic.
  - [ ] RDAP + PRIME + legible-c called out.
  - Fix gaps, then freeze.
  ```bash
  # Manual audit pass
  grep -c 'Task .* of M' docs/plan/PLAN_CHAT_ROBOTS_INVENTORY.md
  grep -c 'Verification:' docs/plan/PLAN_CHAT_ROBOTS_INVENTORY.md
  ```
  **Verification:** Counts >10 Tasks; "Milestone" mentions; no obvious missing sections.
- Task 4 of M1.5: Commit synthesis.
  ```bash
  git add docs/research/RESEARCH_CHAT_ROBOTS_INVENTORY.md docs/plan/PLAN_CHAT_ROBOTS_INVENTORY.md
  git commit -m "Milestone 1.5: Research synthesis + frozen RDAP plan for chat/robots inventory fixes"
  git log --oneline -1
  git push -u origin HEAD
  ```
  **Verification:** Commit message exact, on gb/ branch, pushed.

**End Phase 1. Do not implement changes before this commit.**

## Phase 2 – Define / Architecture
**M2.1 Update contracts, data models, decision log**
- Task 1 of M2.1: Update UI_SPEC.md with inventory grid contract, dev-log panel, mention preservation rules, progressive ack states, single-intro rule.
  Use cat > patch or edit.
  ```bash
  # After edit:
  git diff docs/plan/... || true
  ```
  **Verification:** UI_SPEC contains new sections for "Inventory Robots Grid", "Developer Logging", "Mention Fidelity".
- Task 2 of M2.1: Add structs/enums in C headers if needed (e.g. dev log ring, mention map). Decide: web grid primary for robots inventory; Raylib as optional example in docs/example-inventory/.
  **Verification:** Decision recorded in RESEARCH or plan addendum.
- Task 3 of M2.1: Architecture for progressive acks (reuse/extend thinking[] + new status events or client-side states before emoji).
  **Verification:** Doc updated.
- Commit: "Milestone 2.1: Architecture locked + UI_SPEC contracts"

**M2.2 Risk register update + test strategy**
- ... (atomic)

## Phase 3 – Backend Fixes (C Layer)
**M3.1 Developer Logging toggle + separate stream (C + HTTP + storage)**
- Task 1 of M3.1: Add flag to launch/session (HUSH_DEV_LOG=0 default).
  ```bash
  # edit hush_launch.h / .c + http
  ```
  **Verification:** ./configure && make && ./hush-relay --help or session JSON has "dev_log_enabled":false .
- Task 2 of M3.1: Route post_line / on_deck / debug to dev log only (when enabled). When disabled, suppress visible notes for these.
  **Verification:** check_agent.sh or manual post mention; no "Mention received" note unless dev log on.
- Commit after M3.1.

**M3.2 Single robot intro + remove repeated on_deck**
- Task 1 of M3.2: Guard in hush_agent_on_deck (per robot per thread or session).
  ```bash
  # edit + test
  ```
  **Verification:** Second mention to same robot does not emit new "At ease".

**M3.3 Preserve mention order/position (no destructive replace)**
- Task 1 of M3.3: Change prettyMentions or add preserveMentions that returns original text + positions map. Render @ as styled spans without altering source order.
  **Verification:** Post "@Happy tell joke. @Sgt was funny?" ; rendered text keeps order and positions (inspect DOM or screenshot equiv).

**M3.4 Multi-robot deliberation hooks + prompt hygiene**
- Task 1 of M3.4: Extend fill_prompt with deliberation instructions when n_co > 1 ("You were co-mentioned with ... Decide: reply individually? Cooperate on one note? Split work? Call peers via nostr:npub.").
  **Verification:** Co-mention note triggers prompts containing deliberation text.

**M3.5 Channel topics + per-chan system prompt surface**
- Expose editor in manage-chan; ensure injection (already mostly there).
- Commit per sub-milestone.

## Phase 4 – Frontend (HTML/JS) + Inventory Grid
**M4.1 Progressive ack UX + emoji-only ack**
- Task 1 of M4.1: In paint / thread, show transient "Happy is thinking..." then "reacting" before final robot-ack emoji pill. Use status/thinking + timeouts or server hints.
  **Verification:** Manual: mention -> sequence visible in UI.
- Task 2 of M4.1: Remove "Mention received." from normal paint path (gated by dev log).

**M4.2 Mention fidelity renderer**
- New render path that keeps original @text positions, overlays pills or uses contentEditable-like for display.

**M4.3 Developer Logging panel + Settings toggle**
- Add switch in #settings.
- New drawer/panel (like #thread-pane or #code-canvas) titled "Developer Log". Syslog lines: timestamp [robot] msg.
- When disabled: suppress in main stream.
- Wire to session flag.

**M4.4 Tool rail micro-UX iterations (at least 3)**
- E.g. grip affordance label, consistent 44px, better i-pop text, collapse remembers per vibe.
- Commit.

**M4.5 Inventory grid in Robots section (primary web impl)**
- Replace or augment #robot-list with grid (CSS grid or canvas 2d mimicking Raylib: 10 cols x 5 rows or 8x4).
- Agent items: variable w/h cells, drag (pointer events), snap to grid, IsSpaceFree collision.
- Theme: dark industrial bg, brass lines, neon borders (cyan/pink/green), glow on drag.
- Create/raise: "New Robot" opens form (reuse/extend agent-drawer), on save places in first free slot.
- Icons: use current or colored divs; support atlas slice if images loaded (for user JPEGs).
- Open "inventory" via rail or sidebar header.
- Variable sizes: 1x1 (logic worm), 1x3 (sword), 2x2 (helm), 2x3 (mecha).
- Names overlay.
- Persist slots? (via roster or new local state for prototype; later server).
- Verification: manual drag 2 agents, overlap rejected, snap works, create adds to grid.

**M4.6 (stretch) Optional Raylib prototype**
- mkdir -p examples/inventory-raylib ; cat > examples/inventory-raylib/main.c << 'RAYEOF' ... (adapted provided code + asset load).
- Simple Makefile for it (gcc `pkg-config --cflags --libs raylib`).
- Task: copy assets if present.
  ```bash
  mkdir -p assets/inventory
  cp ~/Pictures/hush_icons/*.jpeg assets/inventory/ 2>/dev/null || echo "skipped copy (no pics)"
  ```
  **Verification:** If raylib present: cd examples/... && make && ./inventory-demo runs and shows grid with 4 agents.

## Phase 5 – Integration, Polish, Tests
**M5.1 Wire everything + embed rebuild**
- After HTML/JS/C changes: always
  ```bash
  sh ../scripts/embed-ui.sh demo
  make clean
  make
  ```
- Update tests (check_agent, check_launch) for new flags/behaviors.
- Commit per M.

**M5.2 Manual scenario tests**
- @Happy tell joke -> progressive + emoji only (no text log).
- @Happy @Sgt ... -> both ack emoji, then deliberation notes with mentions.
- Dev log off (default): clean chat.
- Toggle on: logs appear in panel.
- Inventory: create 3 robots of diff sizes, drag reorder without overlap.
- Channel topic pill -> appears in prompt (log or dev).
- Tool rail: usable, improved.

**M5.3 make test + full checks**
  ```bash
  make test
  sh tests/check_agent.sh
  sh tests/check_launch.sh
  ...
  ```
  **Verification:** ALL PASSED.

## Final Phase – Verification, Polish, Integration & Cleanup
**M F.1 Full audit + polish**
- Fix any gaps from execution.
- Update README / docs if needed (no root PLAN drops).
- Add entries to existing research if required.

**M F.2 Git lifecycle close**
- Commit "Complete: chat flow + inventory robots UI – ready for merge"
- git push -u origin HEAD
- gh pr create --base main --head gb/chat-robots-inventory-ui --title "Chat UX + Inventory Robots (progressive acks, mention fidelity, dev logs, deliberation, grid)" --body "..."
- gh pr merge --auto --merge
- Wait merge (monitor).
- Then on main:
  ```bash
  cd /opt/repo/hush
  git checkout main
  git pull --ff-only origin main
  git worktree remove worktrees/chat-robots-inventory-ui
  git branch -d gb/chat-robots-inventory-ui 2>/dev/null || true
  git push origin --delete gb/chat-robots-inventory-ui 2>/dev/null || true
  git worktree list
  git status
  ```
- Confirm clean on main.

**M F.3 Confirm Finish**
- All success criteria met.
- "Grok Build complete." or equiv stated.
- State: "RDAP complete. All Phases/Milestones/Tasks done per plan."

## Audit Checklist (pre-execution, re-check during)
- [x] Research gate M1.5 present with synthesis commit.
- [ ] Every Task has "Task N of M X.Y", exact commands or cat <<'EOF', verification.
- [ ] Worktree/PR commands explicit.
- [ ] Small atomic (prefer 1-file or 1-behavior per task).
- [ ] legible-c / C11 called for C tasks.
- [ ] No direct main.
- [ ] After M: commit + push.
- [ ] Final cleanup included.

## How to Execute
Follow strictly. Update todo after each major step. Use `shell` tool for commands, `edit`/`write` for changes, `load_skill legible-c` when touching C. Rebuild embed after UI edits. Commit only after milestone verification.

**This plan is now frozen. Begin Phase 2+ only after M1.5 commit in worktree.**

(End of PLAN)