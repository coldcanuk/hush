# PLAN_SPLASH_ONBOARD_MESSAGING.md — Verification Gate

Base: main e9abfd7b4 (fresh worktree gb/splash-onboard-messaging)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_SPLASH_ONBOARD_MESSAGING_CURRENT.md
- Confirmed: all primary DoD items already present on this base.

## DoD checklist (satisfied)
- Splash renders on load; auto-detects state and advances or offers Begin
- Fresh: complete wizard (4 steps) → hive ready with Payne seeded + channels
- Restored: hive or minimal gate with Profile/Settings/Login always visible in header
- Header: brand + Install + Profile + Settings + Call (when ready)
- Sidebar: Channels + Agents/Teams (Payne top, role badges)
- Stream: notes with clear author (you/Payne/agent/human), relative time, empty states with Payne directive
- Profile modal: npub, copy, logout, re-import
- Settings visible
- Low cog load (≤5 primary choices; large 44px targets)
- make + check_launch + check_pwa pass
- PWA installable; embed clean
- Worktree + PR lifecycle

## Verification executed
- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok
- sh hush-c/tests/check_pwa.sh → PWA routes ok
- Explicit greps confirm splash/feather, onboard wizard, Payne messaging, header controls, sidebar, stream author + empty directive, profile modal, settings, data-theme, 44px targets
- No new C; no direct main writes

