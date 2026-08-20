# PLAN: /trouble — Rail launch state, Tab canvas, Providers audit+robots, Min/Max, Exit redesign, Titlebar, Call AV controls (RDAP)

Branch: `gb/trouble-rail-canvas-prov`  
Worktree: `worktrees/trouble-rail-canvas-prov`  
Base: main (post #51 rail-prov)

## 1. Select Planning Methodology
RDAP (Research-Driven Adaptive Planning) as specified. Hybrid Double Diamond + Spiral + small atomic Milestones. Research gate mandatory at end of Phase 1.

## 2. Identify the Scope of Work
**Primary Goal**  
Fix all reported symptoms + implement the provider robots +/– + call AV enhancements while preserving existing behavior and following PRIME_DIRECTIVE + legible standards.

**Non-Goals**  
- Full native SIP/pjproject integration (research only; WebRTC+ coturn sufficient for stated requirements).  
- Rewrite of entire UI or new C modules beyond minimal.  
- Direct writes to main.

**Success Criteria / Overall DoD**  
- All 8+ explicit items from query verified working.  
- make && make test clean.  
- Worktree PR merged, worktree removed, main clean.  
- Evidence of fixes in commits + RESEARCH/PLAN.

**Constraints**  
- C11 + write-legible-c for any .c/.h.  
- Worktree + gb/* + PR land only.  
- No behavior change except requested.  
- Embed UI after index.html changes.

**Assumptions**  
- Current WebRTC + coturn meets "add audio/video controls to newly created channel".  
- "Tab completion" = trap Tab when canvas open for FIM or sensible default.  
- Providers hub is the global surface; robots list uses provider id from roster.

**Top Risks**  
1. X11 client matching for min/max — add status.  
2. Embed step after HTML edits.  
3. Robot assignment consistency (global config vs roster) — keep roster as source of truth, hub mutates via API.

## 3. Generate the Comprehensive Plan
Standard RDAP structure.

### Phase 0 – Environment & Isolation (DONE)
- M0.1: worktree + first commit (executed).

### Phase 1 – Research & Discovery (IN PROGRESS → gate)
- M1.1: Re-inspect all symptoms + build/test in worktree (executed; all E# re-verified, make test PASS).
- M1.2: Re-read prior RESEARCH + Cline/AV assessment (executed).
- M1.3: Synthesize RESEARCH_TROUBLE.md + this PLAN + commit gate (current).

### Phase 2 – Define / Architecture
- M2.1: Lock provider family tailoring table + robot pill contract + rail launch state machine + canvas key contract. (Minimal doc update.)

### Phase 3 – Implementation
Small atomic Milestones. Commit after each.

**M3.1 Rail launch minimized+docked + min/max feedback**  
Task 3.1.1: Edit index.html loadRail to force collapsed + placeRailAtBrand on first ready (no prior saved).  
Task 3.1.2: Add simple status toast or console for rail min/max api result.  
Verify: `grep -n 'placeRailAtBrand\|collapsed' hush-c/demo/index.html`; rebuild + manual (or test script).

**M3.2 Providers: fix configured for API + tailored drawer + global +/– robots**  
Task 3.2.1: In hush_provider.c relax mark_configured for family=="api" to `has_key || (use_home && has_home)`. Add comment. (legible-c).  
Task 3.2.2: Enhance fillProviderDrawer / providerStatusLine + hub rows to show key-only for pure API.  
Task 3.2.3: Add + / – buttons in providers-hub for assigning robots (pills, consistent with payne-provider-pills). Wire to update roster via existing /api/agent or new minimal.  
Task 3.2.4: Ensure "Configure Providers" rail button, left nav robot edit, and raise all surface the same hub + pencil.  
Verify: make test (provider routes), grep configured logic.

**M3.3 Canvas Tab trap**  
Task 3.3.1: Add document keydown (or canvas wrapper) that when #code-canvas.show and !activePrediction, preventDefault on Tab and do insert 4 spaces or focus composer (per "tab completion").  
Verify: grep keydown on canvas-edit + document.

**M3.4 Exit menu redesign (sleek)**  
Task 3.4.1: Reduce .leave-actions .btn min-height to 32px, remove width:100% or constrain, tighten gap/padding. Keep accessibility.  
Verify: CSS diff + visual note in RESEARCH.

**M3.5 Titlebar / frameless improvements**  
Task 3.5.1: Add extra Chromium flags notes in launch (e.g. --disable-features=... if helpful) or document "window-controls-overlay" in manifest + UI_SPEC.  
Task 3.5.2: Ensure undecorate always attempted.  
Verify: manifest + relay.c comments.

**M3.6 Call button + per-channel AV controls**  
Task 3.6.1: Make sure every channel row always shows voice button (when whisper or always). Add per-channel global mute + volume hints in tiles.  
Task 3.6.2: Document that 1:1 and 1:n already supported via openRobotCall / openChannelVoice.  
Verify: rg chan-voice + addTile.

Each M3.x:  
- Exact edit via sed or planned tool.  
- `./configure && make clean && make && make test`  
- `git add . && git commit -m "Milestone 3.x: <concise>"`

### Final Phase – Verification, Polish, Integration & Cleanup
- M4.1: Full make test + manual checklist against all query items + pre-delivery (for C if touched).  
- M4.2: Update README/UI_SPEC if needed, changelog note.  
- M4.3: `git add . && git commit -m "Complete: trouble fixes – ready for merge"`  
- M4.4: `git push -u origin gb/trouble-rail-canvas-prov`  
- M4.5: gh pr create --base main --head gb/... --title "trouble: rail/canvas/providers/exit fixes" --body "..."  
- M4.6: After review/auto-merge: back to main, pull, worktree remove, branch delete push.

## 4. Audit the Plan
- Every Task references Milestone.  
- Commands + verification explicit.  
- Research → gate present (M1.3).  
- Worktree lifecycle complete.  
- Small atomic wins.  
- write-legible-c called out for C.  
- Covers every listed symptom.

## 5–7. Execute, Audit, Confirm
Follow strictly in subsequent turns. Commit per M. End with clean main + "Execution complete."

