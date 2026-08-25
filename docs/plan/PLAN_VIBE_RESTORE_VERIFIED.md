# PLAN_VIBE_RESTORE.md — Verification Gate

Base: main ce415223d (fresh worktree gb/vibe-restore-robot-auth)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_VIBE_RESTORE_ROBOT_AUTH_CURRENT.md
- Confirmed: all DoD items already present on this base from prior slices.

## DoD checklist (satisfied)
1. Create vibe writes $XDG_CONFIG_HOME/hush/vibe.json (or $HOME/.config/hush) 0600. Tests use HUSH_CONFIG_DIR.
2. Cold boot + identity + existing file → has_vibe=1, ready=true. UI skips "Name your vibe".
3. Import of nsec on a process that already has the file → same recovery.
4. File has no nsec. Session after ack still "nsec":"".
5. make clean still only deletes build products.
6. Cline drawer does not claim OAuth ("ClinePass or bring-your-own"). Grok/Codex/Goose copy correct.
7. ./configure && make && make test pass. Embed if needed (prior).
8. PR merged, worktree removed, main clean (pending M8).

## Verification executed
- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (vibe.json exists, no nsec, Cline copy)
- sh hush-c/tests/test_launch (via suite) roundtrips under HUSH_CONFIG_DIR
- Explicit greps:
  - launch: save_vibe, restore_vibe, HUSH_CONFIG_DIR, vibe.json
  - http: create + mutators call save
  - relay: restore in prepare
  - HTML: Cline "ClinePass or bring-your-own"
  - tests: HUSH_CONFIG_DIR, vibe.json assertions, no nsec, Cline grep
- No new C; embed hygiene from prior

## Constraints
- Prime Directive: gb/* only; PR to main.
- String-field JSON only.
- Reuse existing home dir rule (HUSH_CONFIG_DIR or XDG/HOME).

