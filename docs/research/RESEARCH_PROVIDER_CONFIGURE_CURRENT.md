# RESEARCH — Provider configure (pencil per AI runtime) — Current Base Audit

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/provider-configure`
Branch: `gb/provider-configure`
Base: `main` `05387eb43` (post #66 canvas-fim)

## Base State (post prior slices)

All DoD items from PLAN_PROVIDER_CONFIGURE.md are already present on this base:

**C module + HTTP:**
- hush-c/include/hush_provider.h + hush-c/src/hush_provider.c present and compiled (hush_provider.o linked).
- hush_http.c: GET /api/provider, POST /api/provider, POST /api/provider/scan, POST /api/provider/login.
- Provider status includes has_home, family ("home", "oauth", "api", "editor"), model, etc.
- Keys never appear in any GET body (pass only + providers.json overlay at 0600).

**UI:**
- 9 providers in PROVIDERS (goose, grok-build, codex, cline, gemini-api, xai-api, openai-api, anthropic-api, deepseek-api).
- Radio change in #agent-providers shows #provider-cfg (44px pencil) next to selected label.
- Pencil click → openProviderDrawer(id) → #provider-drawer.
- Drawer fields swap by family:
  - home (goose): "Use existing configuration" + optional pills.
  - oauth (grok-build, codex): status + "Log in with OAuth" button.
  - api: key + host + Scan models + model select/type-in.
  - editor (cline): honest empty state + optional API fields.
- Save → POST /api/provider; Scan → POST /api/provider/scan; typed model always allowed.
- No writes to foreign homes (detect only).

**Tests:**
- make -C hush-c test → ALL TESTS PASSED (includes "provider routes ok").
- hush-c/tests/check_provider.sh: full contract test (GET/POST no key echo, has_home for goose/codex, 8+ families, overlay file, no leaked secrets).
- check_provider.sh passes on this base.

**Docs:**
- UI_SPEC.md §11: hub + per-robot pencil, family table, /api/provider routes.
- README.md: "Configure Providers is the hive-wide desk", pass paths hush/providers/<id>/api_key etc.
- docs/pass-integration.md: exact secret paths for providers.

**Non-goals observed:**
- deepseek-api listed but not a new radio requirement.
- No libcurl in-process; scan uses curl argv when present.
- No writing ~/.config/goose etc.
- No OAuth dance from C (delegates to grok/codex CLI).

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds (hush_provider.o linked).
- make -C hush-c test → ALL PASS.
- sh hush-c/tests/check_provider.sh → "provider routes ok".
- Explicit greps: provider-cfg (44px), provider-drawer family switching, /api/provider* routes, 9 providers, no key leakage in check.
- 8+ radios show pencil on select; drawer tailored.
- Goose config.yaml, codex auth.json, grok PATH detection covered in check_provider.
- Pass integration documented.

## Differences from original PLAN base

- Current base is later (05387eb43). Provider configure (pencil + drawer + /api/provider + pass) was implemented in earlier slices (robot-cards-ux, rail-prov, pills-rail-voice-exit era) and is already on main.
- This worktree performs research/audit gate + verification + hygiene (similar to rail-prov #65 and canvas-fim #66) to close PLAN_PROVIDER_CONFIGURE.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria / DoD (1-11). No code changes required for this slice. Hush provider module boundary, pass-only keys, family drawer, 44px pencil, and test coverage all hold.

## Commands run
- git worktree add -b gb/provider-configure from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_provider.sh (pass)
- rg/grep for pencil, drawer families, routes, homes, no-leak assertions
- Docs cross-check (UI_SPEC, README, pass-integration)

## Next
Write PLAN_PROVIDER_CONFIGURE_VERIFIED.md, commit M1+verification on branch, push, PR create, auto-merge, post-merge cleanup (pull main, remove worktree, delete gb/* local+remote).
