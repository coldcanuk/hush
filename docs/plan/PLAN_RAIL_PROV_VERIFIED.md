# PLAN_RAIL_PROV.md — Verification Gate (M1-M4)

Base: main 0a54fe76a (fresh worktree gb/rail-prov)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_RAIL_PROV_CURRENT.md
- Confirmed: all DoD items already present on this base from prior slices.
- No new C/.h. No curl. No per-robot secrets.

## M2.1 Spec + README
Verified present:
- README.md: "Configure Providers is the hive-wide desk"
- UI_SPEC.md §11: hub semantics, hive-global vs per-robot
- UI_SPEC.md §15: rail order with #providers-btn + #prov-info i, two separate .rail-grid for Min/Max and Close/Exit
- rg hits for "providers-hub|Configure Providers|#profile-info"

## M3.1 Rail markup + hub
Verified in hush-c/demo/index.html (and compiled hush_ui_html.h):
- #profile-info, #settings-info, #call-info, #prov-info, #providers-btn, #providers-hub
- openProvidersHub(), openProviderDrawer(id) (accepts explicit id)
- paintProvidersHub() walks all 9 PROVIDERS, shows used-by via robotModels()
- Row click and ✎ both call openProviderDrawer(id)
- #provider-drawer still reachable via left-nav pencil (#provider-cfg on selected radio)
- #raise-agent still present
- 6x .rail-grid (Min/Max grid, Close/Exit grid separate)
- No class="create"

## M4.1 Tests
- make -C hush-c clean && make -C hush-c && make -C hush-c test → ALL TESTS PASSED
- sh hush-c/tests/check_pwa.sh → "PWA routes ok" (all required greps)
- Explicit greps in compiled header confirm all DoD ids (count==1 each)
- Rail grids: 6, raise:1+1, create:0

## Constraints
- Prime Directive: only gb/rail-prov worktree.
- No new C.
- Fitts 44px only on install/rail-toggle.
- Drawer strings preserved ("Raise a robot", "Invite human").
- Hub earlier in DOM than provider-drawer.

## DoD checklist (all satisfied)
- [x] Served HTML has #profile-info, #settings-info, #call-info, #providers-btn, #prov-info, #providers-hub
- [x] Two .rail-grid: Min/Max then Close/Exit
- [x] Hub lists every PROVIDERS id; row/✎ opens existing drawer
- [x] Raise pencil still works
- [x] make -C hush-c test → ALL TESTS PASSED
- [x] Landed via PR (pending M5.1)

