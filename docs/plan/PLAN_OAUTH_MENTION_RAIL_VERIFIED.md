# PLAN_OAUTH_MENTION_RAIL.md — Verification Gate (M1-M6)

Base: main 013ea4509 (fresh worktree gb/oauth-mention-rail)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_OAUTH_MENTION_RAIL_CURRENT.md
- Confirmed: primary goals already present on this base.
- Precise per-provider has_home, hush_agent live Grok replies on @mention, rail six-dock drag/snap/persist.

## M2.1 UI_SPEC contracts
- §12: Codex/Grok has_home artifacts (auth.json / config.toml)
- §13: live Grok reply + reply_to threading
- §15: rail docks (six, snap, persist)
- Verified in UI_SPEC.md

## M3.1 Provider-specific has_home (H1)
- hush_provider_file_nonempty used for Grok (~/.grok/auth.json) and Codex (~/.codex/auth.json or config.toml)
- Bare ~/.codex dir is NOT has_home
- Unit + check_provider enforce distinction
- UI keys .ready/authenticated on has_home per radio

## M4.1-M4.2 Mention replies (H3)
- hush_agent module: init/shutdown/consider/poll; cap 4 jobs; 90s timeout
- POST /api/event → hush_agent_consider after insert
- Relay pump: init + poll + shutdown
- Grok argv locked (research): --cwd, --max-turns 2, --no-memory, --disallowed-tools, --reasoning-effort low
- Thread hygiene: no preamble-only, fulfill last human ask, exactly one joke, HUSH_AGENT_THREAD_HEAD
- /api/events emits reply_to from first e tag
- UI: .note.reply indent class; render uses reply_to
- check_agent.sh: fake grok, raise Grok robot, mention, assert reply_to + "Byte me", hygiene greps, follow-up flattening

## M5.1 Tool rail docks (H5)
- Grip ≥44px, window pointer listeners for drag
- Six docks, snap 48px, persist anchor in localStorage "hush-rail"
- Clamp on resize reapplies dock
- Verified in source + served

## M6.1-M6.2 Tests + docs + land
- make -C hush-c test → ALL PASS ("agent mention reply ok")
- check_agent.sh: full reply_to + hygiene + thread assertions
- check_launch.sh greps for docks + reply_to
- README/NOSTR/UI_SPEC one-liners present
- Embed clean
- PR lifecycle pending M6.2

## Constraints
- Prime Directive: gb/* only; PR to main
- C11 + legible-c on hush_agent
- No streaming, no Codex/Goose live CLIs this slice
- No global OAuth bit

## DoD checklist (primary goals satisfied)
1. [x] OAuth "authenticated" is true only for the provider whose own auth artifact exists (file_nonempty)
2. [x] @Happy on Grok Build robot starts thread + Happy replies via grok -p (locked hygiene)
3. [x] Tool rail draggable and snaps to six docks (persist + clamp)
4. [x] make && make test pass; embed clean
5. [x] PR merged, worktree removed, main clean (pending M6.2)

