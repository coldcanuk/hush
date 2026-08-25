# RESEARCH — Persist vibe + home config; honest provider auth (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/vibe-restore-robot-auth`
Branch: `gb/vibe-restore-robot-auth`
Base: `main` `ce415223d` (post #76 payne-provider-edit)

## Base State

All DoD items from PLAN_VIBE_RESTORE.md (vibe.json persist, restore on cold boot/import, no nsec in file, make clean semantics, honest provider auth copy) are already present on this base:

**vibe.json persist:**
- HUSH_LAUNCH_VIBE_FILE "vibe.json"
- HUSH_LAUNCH_ENV_CONFIG "HUSH_CONFIG_DIR"
- hush_launch_save_vibe / restore_vibe (tmp+rename, 0600)
- Called on create_vibe, set visibility, add/remove channel/project/agent/member/profile, relay_prepare after identity
- Tests use HUSH_CONFIG_DIR; assert file exists, no nsec inside

**Boot / import recovery:**
- Cold boot + identity + existing file → has_vibe=1, ready=true
- Import nsec path also restores
- Session after ack has "nsec":""
- File survives make clean (only build products removed)

**Provider auth honesty:**
- Cline drawer: "Cline uses ClinePass or a bring-your-own provider key — not OAuth."
- Grok/Codex: OAuth CLI paths
- Goose: goose configure / home config
- Verified in HTML + check_launch greps

**Tests:**
- make -C hush-c test → ALL PASS
- check_launch.sh: HUSH_CONFIG_DIR, vibe.json exists, no nsec, ClinePass / bring-your-own grep
- test_launch.c: explicit vibe.json roundtrip under HUSH_CONFIG_DIR

**Docs / spec:**
- UI_SPEC §3 Resume from ~/.config/hush/vibe.json; §7 survives rebuild; §11 Cline auth
- README / pass-integration paths consistent

## Verification Evidence (executed this worktree)

- Build + make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (vibe.json, no nsec, Cline copy)
- Explicit greps:
  - launch.c/h: save_vibe, restore_vibe, HUSH_CONFIG_DIR, vibe.json
  - http.c: create_vibe + mutators call save
  - relay.c: restore_vibe in prepare
  - tests: HUSH_CONFIG_DIR, vibe.json assertions, ClinePass
  - HTML: Cline "ClinePass or bring-your-own"
- No new C required

## Differences from original PLAN base

- Current base is later. Vibe persist/restore, HUSH_CONFIG_DIR contract, honest Cline copy, boot recovery were implemented in provider/pass/launch slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of all prior verification slices) to close the plan per user directive.

## Conclusion

Implementation satisfies every Success Criteria / DoD.
No code changes needed.
H4 lock (vibe.json under XDG/HOME or HUSH_CONFIG_DIR, no secrets, restore after identity, honest drawer copy, make clean untouched) holds.

Proceed to VERIFIED.md + commits on gb/* + full PR lifecycle.

## Commands executed
- git worktree add -b gb/vibe-restore-robot-auth from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh
- rg/grep for vibe.json, restore_vibe, save_vibe, HUSH_CONFIG_DIR, ClinePass, bring-your-own
- Source + test + HTML inspection
