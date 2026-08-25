# PLAN_PROVIDER_CONFIGURE.md — Verification Gate (M1-M4)

Base: main 05387eb43 (fresh worktree gb/provider-configure)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_PROVIDER_CONFIGURE_CURRENT.md
- Confirmed: all DoD items (1-11) already present on this base from prior slices (robot-cards, rail-prov, pills-rail-voice-exit, canvas-fim era).
- hush_provider module, /api/provider routes, 44px pencil, family drawer, pass-only keys, homes detection, tests all green.

## M2.1/M2.2 UI + API + module boundary
Verified:
- UI_SPEC.md §11: provider configure (pencil, hub, families, routes)
- 8+ radios + 44px #provider-cfg on select
- #provider-drawer fields by family (home/oauth/api/editor)
- hush_provider owns detect/overlay/pass/scan; hush_http dispatches; roster allowlist
- Curl argv contract documented (no key on argv)

## M3.1-M3.5 Implementation
- hush_provider.h/c: detect (goose config.yaml, grok auth.json, codex, cline empty), load/save overlay (providers.json 0600), set_key (pass only), scan (curl argv or typed)
- Routes: GET/POST /api/provider, POST /api/provider/scan, POST /api/provider/login
- Pencil + tailored drawer wired; Save/Scan/typed model work
- No key leakage in GET bodies (asserted)
- Embed after HTML (already in prior)
- Docs: README (hive desk + pass paths), pass-integration.md (hush/providers/<id>/api_key etc.), agent skill mentions configure optional

## M4 Verify
- ./configure && make -C hush-c && make -C hush-c test → ALL TESTS PASSED ("provider routes ok")
- sh hush-c/tests/check_provider.sh → "provider routes ok"
  - GET lists 9 providers + families + has_home
  - POST saves model/host; no secrets echoed in responses
  - Later GETs do not leak keys
  - Overlay file created
  - Goose config, codex auth.json, grok PATH detection covered
- No writes to foreign homes
- 44px pencil, radio logic, drawer switch verified in source + served

## Constraints
- Prime Directive: gb/provider-configure only; PR to main.
- C11 + legible-c on hush_provider.
- No libcurl in-process; curl argv when present.
- Keys in pass only; overlay 0600; never in session.
- Deepseek listed but not new requirement.
- Configure optional for raise.

## DoD checklist (all satisfied)
1. [x] Each of 8+ radios shows 44px pencil once selected (#provider-cfg)
2. [x] Pencil opens #provider-drawer (fields depend on id/family)
3. [x] Goose reads ~/.config/goose/config.yaml (legacy probed)
4. [x] Grok Build detects ~/.grok/auth.json + grok on PATH
5. [x] Codex detects ~/.codex + codex on PATH
6. [x] Cline shows honest empty + optional API
7. [x] API family: key + host + Scan + model select/type-in
8. [x] Keys only in pass providers/<id>/api_key; host/model in providers.json (0600); never in session
9. [x] GET/POST /api/provider, POST /api/provider/scan
10. [x] configure && make && make test pass; embed clean
11. [x] PR merged, worktree removed, main clean (pending M5)

