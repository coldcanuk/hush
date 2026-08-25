# RESEARCH — OAuth has_home, @mention Grok replies, rail docks (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/oauth-mention-rail`
Branch: `gb/oauth-mention-rail`
Base: `main` `013ea4509` (post #69 oauth-mention-groups)

## Base State

All primary DoD items from PLAN_OAUTH_MENTION_RAIL.md are already present on this base:

**OAuth has_home (precise per-provider):**
- hush_provider_file_nonempty used for Grok: ~/.grok/auth.json
- For Codex: ~/.codex/auth.json OR ~/.codex/config.toml (dir-only is NOT has_home)
- has_home drives .ready / authenticated state per radio
- check_provider.sh and unit tests enforce the distinction

**Mention replies (Grok Build live replies via @):**
- hush_agent module (hush_agent.h/c): init/shutdown/consider/poll
- hush_agent_consider called from POST /api/event after insert
- Pump in hush_relay.c: hush_agent_init + poll + shutdown
- Grok argv locked: --cwd, --max-turns 2, --no-memory, --disallowed-tools, --reasoning-effort low
- Thread transcript hygiene (no preamble-only, fulfill last ask, one joke, etc.)
- /api/events emits reply_to from first e tag
- UI: .note.reply indent, render uses reply_to; check_agent.sh exercises fake grok + reply_to + hygiene

**Tool rail docks:**
- Drag + pointermove listeners
- Six docks, snap 48px, persist via localStorage "hush-rail"
- Grip ≥44px, clamp on resize
- localStorage anchor handling

**Tests:**
- make -C hush-c test → ALL PASS (includes "agent mention reply ok")
- check_agent.sh: fake grok, nonempty auth, raise Grok robot, mention, assert reply_to + "Byte me", hygiene greps, follow-up flattening
- check_launch.sh greps for docks + reply_to (per prior)
- check_pwa.sh passes

**Docs:**
- UI_SPEC §12 (has_home artifacts), §13 (live Grok reply + reply_to), §15 (rail docks)
- RESEARCH + PLAN exist from prior

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- Explicit:
  - has_home uses file_nonempty for auth.json/config.toml (not bare dir)
  - hush_agent in relay pump (init/poll/shutdown)
  - reply_to emitted in /api/events
  - rail drag + localStorage + pointermove + dock logic present
  - check_agent.sh contains reply_to, Byte me, grok argv hygiene, thread head, no-preamble, fulfill last
- No new C required for verification gate (implementation already landed in prior slices)

## Differences from original PLAN base

- Current base 013ea4509 is later. OAuth precise has_home, hush_agent live Grok replies on mention, rail six-dock drag/snap/persist were implemented across earlier slices (provider work, agent/mention, rail UX) and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of rail-prov #65, canvas-fim #66, provider-configure #67, provider-pass-audit #68, oauth-mention-groups #69) to close PLAN_OAUTH_MENTION_RAIL.md per user directive.

## Conclusion

Implementation satisfies the primary goals:
1. OAuth authenticated = has_home only for the provider whose own auth artifact exists (file_nonempty).
2. @mention on Grok Build robot starts thread + Happy replies via grok -p with locked hygiene argv.
3. Tool rail draggable and snaps to six docks, persisted.

H4 lock (per-provider home detect, separate hush_agent jobs, reply_to threading, rail localStorage docks) holds.
No code changes required.
Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/oauth-mention-rail from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS, agent mention reply ok)
- rg/grep for file_nonempty, has_home per-provider, hush_agent_*, reply_to, rail drag/dock/localStorage/hush-rail
- Source inspection of hush_provider (precise), hush_agent, hush_relay pump, hush_http (reply_to), html (rail + reply indent)
- check_agent.sh hygiene + reply assertions present
