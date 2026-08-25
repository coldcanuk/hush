# PLAN_ONBOARD_PROFILE_AGENTS.md — Verification Gate (M1-M5)

Base: main 7b1006478 (fresh worktree gb/onboard-profile-agents)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_ONBOARD_PROFILE_AGENTS_CURRENT.md
- Confirmed: all primary DoD items (1-10) already present on this base from prior slices.

## DoD Summary (satisfied)
1. Cold session shows feather splash + numbered wizard (Identity → Backup/pass default-on → Vibe → Meet Payne) → ready + Payne welcome note in #welcome.
2. Restored session splash-detects and enters hive.
3. Profile drawer edits first/last/email/org + theme + avatar; Logout is server POST /api/identity {action:"logout"}.
4. Kind 0 for human includes name/display_name/about/picture when set. Email/org Hush-local.
5. Hive has Add human (npub/invite) and Create agent. Private vibe shows join token.
6. Agent create: name, system_prompt (required), provider (required), avatar, context files (text/plain or Markdown only, max 3). Agent nsec via pass hush/agents/<slug>/nsec (default on). Kind 0 published.
7. .goose/skills/agent-create/SKILL.md exists with full contract (Payne/Goose can create agents).
8. 7 themes via data-theme + CSS vars; persist in localStorage + session.
9. ./configure && make && make test + check_launch.sh pass.
10. Worktree + PR lifecycle followed (no direct main).

## Verification executed
- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (feather, prof-*, data-theme, raise-agent, pass CLI, etc.)
- Explicit greps confirm splash/wizard, profile, themes, agent create + pass + MIME, skill file, kind 0 support.
- Agent-create skill file present and complete.
- No new C; embed hygiene from prior slices.

## Constraints
- Prime Directive: gb/* only; PR to main.
- Pass default-on, MIME enforcement, no foreign home writes.
- Single vibe per relay.

