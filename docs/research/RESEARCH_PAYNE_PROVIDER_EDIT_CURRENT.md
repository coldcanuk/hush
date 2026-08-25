# RESEARCH — Edit Sgt. Major Payne (provider + order only) — Current Base Audit

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/payne-provider-edit`
Branch: `gb/payne-provider-edit`
Base: `main` `afc77aadb` (post #75 one-joke-snip)

## Base State

The implementation for PLAN_PAYNE_PROVIDER_EDIT.md is already present on this base from prior provider/robot-cards/agent slices:

**Persist + session:**
- HUSH_LAUNCH_PAYNE_PROVIDERS_MAX = 4
- payne_providers[] + npayne_providers in hush_launch_t
- hush_launch_set_payne_providers, default [goose], put/take for payne_provider_N, format in session JSON as payne.providers array + payne.provider (primary)

**HTTP:**
- POST /api/agent with slug=="sgt-major-payne" routes to hush_http_update_payne
- Updates providers list via set_payne_providers; ignores name/prompt; delete still denied

**Dispatch:**
- hush_agent_lookup_robot: if npayne_providers > 0 use payne_providers[0], else Goose
- Prompt remains HUSH_LAUNCH_PAYNE_ABOUT (locked)

**UI:**
- robotModels includes Payne with providers array (fallback ["goose"])
- openAgentDrawer accepts Payne (locked=true but edit allowed for providers)
- "Edit Sgt Major Payne" title, payne-provider-pills visible, order radios/pills
- Save posts slug + provider_0..N
- Card subtitle shows primary + +N
- Name/prompt/delete locked (Delete disabled with message)

**Tests:**
- test_launch.c: set_payne_providers, format/restore, default one provider, name constant
- check_launch.sh: greps payne-provider-pills, Edit Sgt Major Payne, POST update returns providers array, name locked, delete denied (403)
- roster still refuses Payne delete

**Spec:**
- UI_SPEC §9 documents Payne Edit exception (ranked providers, name/prompt locked, undeletable)
- README mentions Payne runtime order editable

**Constraints:**
- No live spawn, no fallback walk, no rename/delete/context on Payne
- Worktree/PR discipline followed in prior

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (Payne edit greps)
- Explicit greps:
  - launch.h/c: PAYNE_PROVIDERS_MAX, payne_providers, npayne, set/put/take/format
  - http.c: update_payne, sgt-major-payne path
  - agent.c: payne_providers[0] dispatch
  - html: "Edit Sgt Major Payne", payne-provider-pills, open for Payne, order save
  - tests: provider array, locked name, delete denied, default goose
- No new C required

## Differences from original PLAN base

- Current base is later. Payne provider order (4 ranked, edit via drawer, persist in launch/vibe, dispatch primary, UI pills) was implemented in provider/robot-cards/agent slices and is already on main.
- This worktree performs research/audit gate + verification + hygiene (matching rail-prov #65, canvas-fim #66, provider-* #67/68, oauth-*/thread-* #69-73, onboard/splash #74, one-joke #75) to close PLAN_PAYNE_PROVIDER_EDIT.md per user directive.

## Conclusion

Implementation satisfies every item in the plan (spec, persist, dispatch, UI, tests).
No code changes needed.
H4 lock (name/prompt locked, undeletable, ranked providers only, no live spawn) holds.
Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/payne-provider-edit from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh
- rg/grep for payne_providers, update_payne, sgt-major-payne, provider-pills, Edit Sgt Major Payne, locked name, delete denied
- Source + test + UI_SPEC inspection
