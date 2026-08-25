# RESEARCH — Rail help + Configure Providers hub (Current Base Audit)

Methodology: **RDAP** — verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/rail-prov`
Branch: `gb/rail-prov`
Base: `main` `0a54fe76a` (post #64 thread-ux, #61 rail-win, #58 pills-rail-voice-exit, etc.)

## Base State (clean main after prior merges)

- All DoD elements already present in `hush-c/demo/index.html`:
  - `#profile-info`, `#settings-info`, `#call-info`, `#prov-info`, `#providers-btn`, `#providers-hub`
  - `.rail-grid` blocks: 6 total (Min/Max and Close/Exit are separate grids)
  - `#raise-agent`, `#provider-cfg` (pencil) still present
  - No `class="create"`
- `openProvidersHub()` + `openProviderDrawer(id)` implemented and wired.
- `paintProvidersHub()` walks `PROVIDERS` (9 runtimes), `providerMap`, `robotModels()` for used-by.
- `check_pwa.sh` already contains greps for all new ids + `openProviderDrawer`.
- `make -C hush-c test` → ALL TESTS PASSED (launch/exit/provider/agent/fixup/complete/turn).
- README.md and UI_SPEC.md already document:
  - "Configure Providers is the hive-wide desk"
  - Rail order with `#providers-btn` + `#prov-info`
  - Hub semantics (hive-global creds, per-robot pick via left-nav Edit)
- No new C/.h required; no curl; no per-robot secret paths added.
- Two `.rail-grid`s for Min/Max vs Close/Exit confirmed in source.
- Hub earlier in DOM than `#provider-drawer` (stacking mitigation per prior E9).

## Verification Evidence (this worktree)

- Build: `make -C hush-c` succeeds after `scripts/embed-ui.sh`.
- Tests: `make -C hush-c test` → "ALL TESTS PASSED".
- PWA check: `sh hush-c/tests/check_pwa.sh` → "PWA routes ok".
- Served content (header): all required `id=` strings present (python grep count == 1 each).
- Rail grids: 6; raise pencil present; class=create count == 0.
- Functions: `openProvidersHub`, `openProviderDrawer`, `paintProvidersHub`, `robotsUsingProvider`.
- Docs: README + UI_SPEC already match plan DoD.

## Differences from original PLAN base (ffd45116a)

- Current base is later (0a54fe76a). Prior slices (thread-ux, rail-win, pills-rail-voice-exit, robot-cards, server-ack-notes) already landed equivalent or superset UI.
- No implementation changes required in this slice; only research/audit + verification gate + hygiene (if any) + PR to close the plan.

## Conclusion (H4 lock still holds)

The implementation on this base satisfies every Success/DoD item in PLAN_RAIL_PROV.md.
No code changes needed. Proceed to verification commit + PR lifecycle per M5.1.

## Command log (executed in worktree)

- git worktree add -b gb/rail-prov ...
- make -C hush-c clean && sh scripts/embed-ui.sh hush-c/demo && make -C hush-c
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_pwa.sh (PWA routes ok)
- explicit greps against compiled hush_ui_html.h for DoD ids
- rg for README/UI_SPEC coverage

## Next (per plan)

- Write PLAN_RAIL_PROV_VERIFIED.md
- Commit M1.1 research gate + verification
- Push gb/rail-prov
- gh pr create --base main --head gb/rail-prov
- gh pr merge --auto --merge
- Post-merge: cd /opt/repo/hush; git pull --ff-only; git worktree remove ...; git branch -d ...; git push origin --delete ...
