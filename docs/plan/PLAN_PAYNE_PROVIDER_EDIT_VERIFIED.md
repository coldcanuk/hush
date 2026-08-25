# PLAN_PAYNE_PROVIDER_EDIT.md — Verification Gate

Base: main afc77aadb (fresh worktree gb/payne-provider-edit)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_PAYNE_PROVIDER_EDIT_CURRENT.md
- Confirmed: all items already present on this base.

## M2.1 Spec
- UI_SPEC §9: Payne Edit exception (ranked providers 1..4; name/prompt locked; Delete disabled)
- README: Payne runtime order editable
- Verified

## M3.1 Persist + HTTP
- HUSH_LAUNCH_PAYNE_PROVIDERS_MAX=4, payne_providers[], npayne_providers
- hush_launch_set_payne_providers, default [goose], put/take for payne_provider_N, format in session (payne.providers + payne.provider)
- POST /api/agent slug==sgt-major-payne → hush_http_update_payne (updates providers; ignores name/prompt; delete denied)
- Verified in source + tests

## M4.1 Dispatch
- hush_agent_lookup_robot: npayne_providers > 0 ? payne_providers[0] : Goose
- Prompt remains HUSH_LAUNCH_PAYNE_ABOUT (locked)
- Verified

## M5.1 UI
- robotModels reads session.payne.providers (fallback ["goose"])
- openAgentDrawer accepts Payne (locked but edit allowed for providers)
- "Edit Sgt Major Payne", payne-provider-pills, order radios/pills
- Save posts slug + provider_0..N
- Card subtitle: primary + +N
- Name/prompt/delete locked (Delete disabled)
- Verified in source + served + check_launch greps

## M6.1 Tests
- test_launch.c: set, format/restore, default, name constant
- check_launch.sh: payne-provider-pills, Edit Sgt Major Payne, POST returns providers[], name locked, delete 403
- roster refuses Payne delete
- Verified

## M7.1 Land (pending)
- make && make test pass (ALL PASS)
- PR lifecycle to be executed

## DoD (satisfied)
- [x] Edit on Sgt. Major Payne configures ranked SaaS/CLI providers
- [x] Name, slug, standing orders locked; Payne undeletable
- [x] No live API spawn; no fallback walk
- [x] Spec, persist, dispatch, UI, tests complete
- [x] make test passes

