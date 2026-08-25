# RESEARCH — Splash + Onboarding Wizard + Profile/Settings + Human+Agent Messaging UI (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/splash-onboard-messaging`
Branch: `gb/splash-onboard-messaging`
Base: `main` `e9abfd7b4` (post #74 onboard-profile-agents)

## Base State

The plan's primary goals are largely satisfied by the current implementation (splash/onboard from prior slices + profile + messaging UI + Payne persona):

**Splash / Onboarding detection:**
- Feather splash present (class="feather", /icon-192.png)
- Auto-detect state on load (logged_in / has_vibe / ready)
- Onboard wizard language and flow elements present (Identity, Backup, Vibe, Payne)
- Fresh path completes to ready=true with Payne seeded + channels
- Restored path enters hive

**Header always-on controls:**
- Profile + Settings + Install + brand visible
- Profile modal with npub, copy, logout
- Settings accessible

**Sidebar + Messaging UI:**
- Channels + Agents/Teams (Payne top, role badges)
- Stream notes with author (you/Payne/agent/human)
- Empty states with Payne directive ("At ease...", mission language)
- Low cog load hints (44px targets, limited primary choices)

**Profile/Settings:**
- Profile drawer/modal with npub, logout, re-import
- Settings visible; themes persisted

**Tests:**
- make -C hush-c test → ALL PASS
- check_launch.sh passes (feather, profile, data-theme, raise, pass CLI, splash elements)

**Constraints:**
- Worktree + PR followed in prior
- Embed after HTML (satisfied)
- C11 strict, no new C expected here

## Verification Evidence (executed this worktree)

- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok
- Explicit greps in compiled header + source:
  - Feather splash + icon
  - Onboard/wizard language + Payne messaging
  - Header controls (profile/settings)
  - Sidebar channels + agents
  - Stream author + empty Payne directive
  - Profile modal npub/logout
  - Settings + data-theme
  - Low cog / large targets (44px)
- No new C required

## Differences from original PLAN base

- Current base is later. Splash intro, onboarding wizard, Payne messaging UI, header-always profile/settings, sidebar agents/teams, stream authors, empty directives were implemented across earlier slices (splash #19, onboard, profile, robot-cards, thread/rail, messaging polish) and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern) to close PLAN_SPLASH_ONBOARD_MESSAGING.md per user directive.

## Conclusion

Implementation satisfies the Success Criteria / DoD.
No code changes needed.
Proceed to VERIFIED.md + commits on gb/* + full PR lifecycle.

## Commands executed
- git worktree add -b gb/splash-onboard-messaging from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh
- rg/grep + python for splash, feather, onboard, Payne, header, sidebar, stream, profile, settings, empty states, 44px
- Source + served + docs inspection
