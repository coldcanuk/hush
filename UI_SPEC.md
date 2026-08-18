# Hush UI Spec — Splash + Wizard + Messaging (Sgt Major Payne)
Version: 2026-08-18 (RDAP M2.1)
Authoritative for this feature. Quinn + Parker + Payne adapted from catfu specialists (no feline).

## Core Principles (Quinn)
- Cognitive Load Index ≤ 3/10. Gestalt Clarity ≥ 85/100.
- Hick's Law: ≤5 primary visible choices at any time.
- Fitts: large targets (min 44px tap, 10px padding).
- Recognition > recall: icons + labels + consistent pills.
- Proximity: related actions grouped (Channels together; Agents together).
- Error prevention: confirm logout; nsec never auto-persisted to browser.

## Payne Voice (adapted Alfred mentor → disciplined human)
- "At ease." "Mission first." "Report when ready." "Find or raise the right robot."
- One directive per major screen/empty state.
- Precise, respectful, no fluff. "Carry on."

## Product JTBD (Parker)
- "As a human or agent lead, I collaborate with mixed teams using familiar messaging (channels, notes, presence) to plan, assign, and ship."
- MVP cuts: no rich threads, no external LLM for Payne (signaling + persona only).

## Flows (locked)
1. **Splash (detect)**: 300-800ms brand + "Sgt Major Payne reporting." + spinner. 
   - Poll /api/session once or twice.
   - If ready → auto to hive (or "Enter hive" CTA).
   - Else → wizard or slim gate with header controls.
   - Always header: brand | Install | Profile | Settings | (Call if ready).

2. **Onboarding Wizard (no config)**: linear 4 steps, progress dots or 1/4 2/4...
   - Step 1: Identity — Create new (primary) or Import existing nsec.
   - Step 2: Backup — Reveal nsec (masked), Copy, checkbox **checked** "Checked to save ... to Unix Password Manager. Retrieve with: pass show hush/identity/nsec".
   - Step 3: Vibe — Name the relay (default "local hive"), about, public/private toggle.
   - Step 4: Team seed — Confirm starter channels + "Sgt Major Payne" agent. "Stand up the hive".
   - Payne microcopy on each: "State your name and rank." etc. End with "Carry on."

3. **Resume / Login (config present)**: From splash or header Login → same identity/import + backup if needed. Auto-advance when ready.

4. **Hive (messaging)**:
   - Header (always): brand (hush) + vibe name + Install + Profile + Settings + Call.
   - Sidebar (left, 220px+): 
     - Channels (lbl + list of #slug, active highlight).
     - Create channel input + Add.
     - Agents/Teams (lbl): Sgt Major Payne (prominent, badge "agent", npub short), other humans/agents.
     - Create project (for later).
   - Main:
     - Room header: #channel | note count | visibility.
     - Stream: notes as cards. Meta: who (pill: you / Payne / agent / human / pk8) · ago.
     - Empty: "The hive is quiet. State the mission for #" + channel + ". — Payne"
     - Composer: input + Send (Payne placeholder "Write orders...").
   - Drawers: Settings (existing + future UI), Conference (mesh + Payne invite).

5. **Profile (always reachable)**:
   - Modal: your npub (copyable), human name/about (from kind0 if any), "Logout" (clears session client-side + server if needed), "Use different key" (import).
   - Never shows nsec after ack.

6. **Settings**: existing STUN/TURN + vibe vis + future (theme, Payne directives on/off).

## Data / Session (no breaking change)
Use existing /api/session shape. Client may add local "page" state only. Server remains source of truth for logged_in/backup_acked/has_vibe/ready/channels/pay ne/vibe.

## Visual Language (existing + disciplined polish)
- Dark: --bg #09090b, --surface #18181b, --accent #34d399 (emerald disciplined).
- Sans + mono for keys.
- Pills for authors: small rounded, color by role (you=accent, Payne=accent-dim, agent=muted).
- No more than 5 nav items.

## Hick Cuts (explicit)
- Primary: Channels list, Agents list, Composer, Header actions (4-5).
- Advanced: Settings drawer, Call stage.
- No landing page bloat; no 3rd rail items.

## Verification per M
- Quinn quick audit (load, gestalt) in commit msg or separate note.
- Parker: "does this serve the messaging collab JTBD?"
- After every UI edit: scripts/embed-ui.sh demo && make -C hush-c (or top make)
- Smoke: curl session, check_launch.sh, manual fresh/restored.

## Payne Directives (examples to place)
- Splash: "Sgt Major Payne reporting for duty."
- Empty stream: "At ease. Write the first order."
- After vibe: "Hive standing. Carry on."
- Profile: "Your identity is your orders. Guard it."

## Non-negotiables (enforce)
- Checkbox default checked + exact retrieve text.
- Profile/Settings visible before ready.
- Payne always in agent list when vibe present.
- No catfu, Griffe, Scout, Brave, feline words.
- Embed after HTML change.
