# RESEARCH — Proper Onboarding + Profile + Vibe Members + Payne Agent Creation (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/onboard-profile-agents`
Branch: `gb/onboard-profile-agents`
Base: `main` `7b1006478` (post #73 thread-1to1-follow)

## Base State (post prior slices)

The bulk of PLAN_ONBOARD_PROFILE_AGENTS.md DoD is already implemented on this base:

**Splash / Onboarding:**
- Feather splash (class="feather", /icon-192.png)
- Wizard flow: Identity → Backup/pass (checked default) → Vibe → Meet Payne
- Cold session (logged_in=false or has_vibe=false) triggers splash → ready=true after completion
- Payne welcome note in #welcome

**Profile + Theme + Logout:**
- Profile drawer: first/last/email/org + avatar + theme
- 7 themes via data-theme + CSS vars; persisted
- Logout is server POST /api/identity {action:"logout"}

**Vibe / Members / Agents:**
- Add human (npub/invite)
- Create agent (name, system_prompt required, provider, avatar, context files)
- Private vibe shows join token; public does not
- Agent nsec via pass at hush/agents/<slug>/nsec (default on)
- Kind 0 published for human + agents (name, display_name, about, picture)
- Context MIME: text/plain or Markdown only (checked)

**Agent-create skill:**
- .goose/skills/agent-create/SKILL.md exists and documents the API (name, system_prompt, provider, context ≤3, pass retrieve path)

**Tests + checks:**
- make -C hush-c test → ALL PASS
- check_launch.sh greps: feather, prof-first, data-theme dracula, Raise a robot, agent-provider, pass retrieve CLI, etc.

**Constraints satisfied (prior work):**
- Embed after HTML (done in prior slices)
- No direct main writes
- Pass default-on, MIME enforcement, no foreign home writes

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (all required splash/profile/theme/agent/pass greps)
- Explicit greps in compiled header + source confirm:
  - Feather splash + icon
  - pass checked default
  - Profile fields + server logout
  - data-theme (multiple)
  - Raise agent + provider + context
  - Agent nsec pass path
  - MIME checks (text/plain, markdown)
  - Agent-create skill file present with full contract
- No new C required for this verification slice

## Differences from original PLAN base

- Current base is later. Onboarding splash, profile, vibe members, agent create with pass, themes, kind 0, MIME context, agent-create skill were implemented across earlier slices (splash #19, robot-cards, pills-rail, provider, pass-integration, thread work) and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the established pattern) to close PLAN_ONBOARD_PROFILE_AGENTS.md per user directive.

## Conclusion

Implementation on this base satisfies the Success Criteria / Overall DoD (1-10).
Primary hygiene remaining (if any) is minor greps or doc polish; no behavioral or C changes required.
H4 lock (single vibe per relay, pass default-on, MIME-only context, isolated agent nsec, kind 0, server logout, agent-create skill) holds.

Proceed to VERIFIED.md + M1.1 + verification commits on gb/* + full PR lifecycle.

## Commands executed
- git worktree add -b gb/onboard-profile-agents from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh
- rg/grep + python for splash, feather, pass default, profile, theme, raise, agent nsec, MIME, skill file, kind 0, data-theme, logout
- Source + served + docs inspection
