# PLAN: Splash + Onboarding Wizard + Profile/Settings + Human+Agent Messaging UI (RDAP)

Branch: `gb/splash-onboard-messaging`
Worktree: `worktrees/splash-onboard-messaging`
Base: main (a5ba9740...)

## 1. Select Planning Methodology
RDAP (Research-Driven Adaptive Planning) — Double Diamond + Spiral risk iterations + small atomic Milestones with strict DoD + commit per M.

## 2. Scope of Work

**Primary Goal**
A well-researched, disciplined PWA UI for Hush (C11 Nostr relay):
- Intro splash that detects user/vibe/config during load.
- No-config: onboarding wizard.
- Config present: seamless login/resume.
- UI is traditional messaging for collaborating with teams of humans + agents.
- Sgt Major Payne is the canonical agent persona (no feline/Alfred branding).

**Non-Goals**
- NIP-42 full auth loop (future).
- Rich threads/reactions in MVP.
- External LLM for Payne (seeded persona + signaling only).
- Tailwind in C sources (hand CSS + CDN opt-out already).
- Desktop Tauri or non-PWA surfaces.

**Success Criteria / DoD (measurable)**
- Splash renders on load; auto-detects state and advances or offers Begin.
- Fresh: complete wizard (4 steps) → hive ready with Payne seeded + channels.
- Restored: hive or minimal gate with Profile/Settings/Login always visible in header.
- Header: brand + Install + Profile + Settings + Call (when ready).
- Sidebar: Channels + Agents/Teams (Payne top, role badges).
- Stream: notes with clear author (you/Payne/agent/human), relative time, empty states with Payne directive.
- Profile modal: npub, copy, logout, re-import.
- Settings: existing + any UI prefs.
- Low cog load (Quinn metrics in review): ≤5 primary choices visible; large targets.
- `./configure && make clean && make && make test` + `hush-c/tests/check_launch.sh` pass.
- PWA still installable; embed script keeps C in sync.
- All C touched follows write-legible-c + §14 checklist.
- Worktree lifecycle: commits after each M, push gb/*, final PR → merge → delete WT.

**Constraints**
- C11 strict build (-Wall -Wextra -Werror -Wconversion -Wshadow).
- UI assets embedded via `scripts/embed-ui.sh demo` + rebuild.
- Single binary; same port for Nostr line + HTTP PWA.
- Worktree only; PR only to land.
- Adapt catfu specialists essence (Quinn UI, Parker product filter, Alfred→Payne mentor) without catfu words.

**Assumptions**
- Current gate logic (landing/backup/vibe) is solid foundation; we polish into explicit splash + wizard.
- pass restore + session JSON remain authoritative.
- Payne already seeded in launch.c; enhance presentation.

**Top Risks + Mitigations**
1. State machine drift in index.html → single paint()/applySession source; heavy use of check_launch + curl tests.
2. Embed/UI drift after edits → mandatory `scripts/embed-ui.sh demo && make` in every UI M verification.
3. Restored users see "no buttons" → force header controls visible on all pages; add profile/settings to gate slim header.
4. Bloat (Hick) → explicit cuts list; review by Parker/Quinn principles before close M.
5. C changes → only if required; full legible-c checklist + test before commit.

**Required Environment**
- gcc, make, openssl, curl, git, gh (for PR at end).
- `pass` optional for tests (fake-pass used).
- coturn optional for call tests.

## 3. Comprehensive Plan (Phases → Milestones → Tasks)

### Phase 0 — Environment & Isolation Setup (COMPLETE — pre-existing worktree)
- M0.1: Worktree `gb/splash-onboard-messaging` created on clean main. (verified)

### Phase 1 — Research & Discovery (GATE at end)
- M1.1: Inventory current UI (demo/index.html, gate state machine, header, hive) + C contracts (launch, http, relay start, session JSON, embed).
  - Task 1.1.1: `cd worktrees/splash-onboard-messaging && wc -l hush-c/demo/index.html && grep -E "(gate|hive|session|payne|settings|profile)" hush-c/demo/index.html | head -20`
  - Task 1.1.2: Read key C: `cat hush-c/src/hush_launch.c | head -100; cat hush-c/src/hush_http.c | sed -n '1,120p'`
  - Verification: cold + post-create curl session + HTML contains gate + Payne strings.
- M1.2: Extract + adapt catfu specialists (ui-quinn, catfu-brand, caretaker-alfred, product-parker) + recipes.
  - Task 1.2.1: (already fetched) `ls research/catfu*.yaml research/catfu-skills/*/SKILL.md`
  - Task 1.2.2: Document essence mapping (Payne voice, Quinn heuristics, Parker JTBD "collaborate via messaging", brand enforce).
  - Verification: RESEARCH.md contains adapted principles + "Sgt Major Payne".
- M1.3 (MANDATORY LAST): Synthesize all findings into RESEARCH.md + produce/ update concrete plan (this file) + commit.
  - Task 1.3.1: `git add RESEARCH.md PLAN_SPLASH_ONBOARD_MESSAGING.md && git commit -m "Milestone 1.3: Phase 1 synthesis gate + concrete plan"`
  - Task 1.3.2: `git push -u origin HEAD`
  - Verification: commit message + `git log --oneline -1` shows M1.3; plan file present.

### Phase 2 — Define / Architecture
- M2.1: Lock UI flows, components, copy guidelines, data (session shape unchanged or minimal additive).
  - Task 2.1.1: Write UI spec section (splash → wizard steps 1-4 → hive) + header always-on contract.
  - Task 2.1.2: Define Payne copy rules (one directive per major empty state; disciplined tone).
  - Task 2.1.3: Hick cut list (max 5 nav; profile/settings always; agents roster).
  - Verification: spec in PLAN or RESEARCH; reviewed against Quinn/Parker.
- M2.2: Update risk register + test strategy (manual + curl + check_launch extensions).
  - Commit after M2.

### Phase 3 — Implementation (small vertical slices)
- M3.1: Splash screen.
  - Task 3.1.1: Add splash HTML/JS section (brand + "Sgt Major Payne reporting for duty" + loading + detect state + Begin or auto-advance).
  - Task 3.1.2: Wire to existing state machine (if ready → hive; else wizard or gate).
  - Task 3.1.3: `scripts/embed-ui.sh demo && make -C hush-c`
  - Verification: curl / shows splash card; tick advances correctly; no regression on gate.
  - Commit: "M3.1: Splash intro with detection"
- M3.2: Onboarding wizard (linear).
  - Task 3.2.1: Convert/restructure gate cards into wizard with progress (Identity → Backup+pass default-on → Vibe → Channels/Agents seed).
  - Task 3.2.2: Payne-flavored microcopy + "Report when ready" CTAs.
  - Task 3.2.3: Embed + rebuild + smoke.
  - Verification: fresh flow completes in 4 explicit steps; session ends ready=true.
  - Commit.
- M3.3: Profile + Settings + Login controls (always reachable).
  - Task 3.3.1: Add Profile button in header (even on gate); open modal with npub, copy, logout, re-import.
  - Task 3.3.2: Ensure Settings button visible early; add minimal UI prefs if needed (theme hints later).
  - Task 3.3.3: Login/Import CTA from header when !logged_in.
  - Task 3.3.4: Embed + test restored path shows controls.
  - Commit.
- M3.4: Messaging UI polish for human+agent teams.
  - Task 3.4.1: Sidebar: split Channels + Agents/Teams (Payne first, role badges).
  - Task 3.4.2: Stream notes: author pills (you / Payne / agent / human / short pk), channel context.
  - Task 3.4.3: Empty states + composer placeholder with Payne directive ("At ease. State the mission.").
  - Task 3.4.4: Preserve call + settings; ensure Payne invite works.
  - Task 3.4.5: Embed + full smoke (create, post, Payne visible).
  - Commit.
- M3.5 (if needed): Minimal C tweaks (e.g. session fields, new constants) + legible-c.
  - Only if JS/JSON insufficient.
  - Full checklist + test_launch + check_launch.
  - Commit.

### Phase 4 — Verification, Polish, Integration & Cleanup
- M4.1: Full build + test gate.
  - `./configure && make clean && make && make test`
  - `hush-c/tests/check_launch.sh`
  - Manual: fresh + restored flows via curl + browser smoke (if display).
  - Verification: all green.
- M4.2: Polish + docs.
  - Update README mention of splash/onboard if needed.
  - Add any missing Payne copy or microcopy review.
  - Run legible-c checklist on diffs (even if only HTML comments).
- M4.3: Final commit + push.
  - `git add . && git commit -m "Complete: splash + onboard wizard + profile/settings + messaging for humans+agents (Payne)"`
  - `git push -u origin HEAD`
- M4.4: PR + land (after review).
  - `gh pr create --base main --head gb/splash-onboard-messaging --title "Splash, onboarding wizard, profile/settings, human+agent messaging (Sgt Major Payne)" --body "..." `
  - `gh pr merge --auto --merge` (or manual approve)
- M4.5: Post-merge cleanup (on main after pull).
  - `cd /opt/repo/hush && git pull --ff-only origin main`
  - `git worktree remove worktrees/splash-onboard-messaging`
  - `git branch -d gb/splash-onboard-messaging || true`
  - `git push origin --delete gb/splash-onboard-messaging || true`
  - Verify only main worktree remains.

## 4. Audit the Plan (before execution)
- [ ] Every Task has CLI or exact snippet + verification step + M ref.
- [ ] Phase 1 gate present (M1.3 synthesize + plan + commit).
- [ ] Worktree lifecycle complete and accurate.
- [ ] Tasks atomic + small wins.
- [ ] legible-c called out for C.
- [ ] Research → plan gate explicit.
- Fix gaps then freeze.

## 5-7. Execute → Audit Work → Confirm
Follow strictly. Commit after every M. Re-audit at end. State "Grok Build complete." only when all DoD + cleanup done.

## Notes
- Use `scripts/embed-ui.sh demo` after every index.html change.
- Payne voice examples: "At ease.", "Mission first.", "Find or raise the right robot.", "Report when ready."
- Quinn audit each major M: Cognitive Load, Gestalt.
- Parker: cut anything not serving "collaborate via messaging".
