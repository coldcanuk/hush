# PLAN — Rail help + Configure Providers hub

Methodology: **RDAP** (Research-Driven Adaptive Planning).
Research lock: `docs/research/RESEARCH_RAIL_PROV.md` (H4).
Worktree: `/opt/repo/hush/worktrees/rail-prov`
Branch: `gb/rail-prov`
Base: `main` `ffd45116a`

## Scope

**In.** Rail mock-up with `i` on Profile / Settings / Call /
Configure Providers. New `#providers-hub` listing all nine
runtimes + used-by robots. Reuse `#provider-drawer` via
`openProviderDrawer(id)`. Spec + README + tests.

**Out.** Per-robot secret overrides. New C / new HTTP. Removing
the Raise pencil. Live hive restart. Changing Close/Exit.
Raising content bounds. libcurl.

## Primary objective

The rail matches the operator mock-up. Configure Providers is
the formal hive-wide desk. Per-robot pick stays on left-nav
Edit. Credentials stay hive-global.

## Success / DoD

- Served HTML has `#profile-info`, `#settings-info`, `#call-info`,
  `#providers-btn`, `#prov-info`, `#providers-hub`.
- Two `.rail-grid`s: Min/Max then Close/Exit.
- Hub lists every `PROVIDERS` id; row / ✎ opens the existing
  drawer for that id.
- Raise pencil still works.
- `make -C hush-c test` → ALL TESTS PASSED.
- Landed via PR, not a local merge to `main`.

## Constraints

- Prime Directive: worktree `gb/rail-prov` only; PR to `main`.
- No new `.c` / `.h` this slice.
- Fitts 44 px remains on `#install` and `#rail-toggle` only.
- Secrets never echo. `GET /api/provider` is status only.
- Keep drawer strings `Raise a robot` and `Invite human`.

## Assumptions

- A3/A4: no per-robot secrets; no new endpoint.
- A6: live hive still needs an operator restart after land.

## Risks

1. Stacked drawers. Hub earlier in DOM than `#provider-drawer`.
2. Payne used-by missed. Walk `robotModels()`.
3. Rail overflow. Reuse `.rail-info-row` inside `.rail-grid`.

## Phase 0 — Isolation

### M0.1 Worktree

- [x] Task 1 of M0.1: `git worktree add -b gb/rail-prov worktrees/rail-prov` from clean main `ffd45116a`.
- Verify: `git branch --show-current` → `gb/rail-prov`.

## Phase 1 — Research

### M1.1 Research lock

- [x] Task 1 of M1.1: write `docs/research/RESEARCH_RAIL_PROV.md`.
- [x] Task 2 of M1.1: write this plan; commit both.

```
git add docs/research/RESEARCH_RAIL_PROV.md docs/plan/PLAN_RAIL_PROV.md
git commit -m "Milestone 1.1: research lock rail help + providers hub"
```

Verify: files exist under `docs/`.

## Phase 2 — Define

### M2.1 Spec + README

- [x] Task 1 of M2.1: update `UI_SPEC.md` §11 (hub + per-robot
      split) and §15 (rail order + new help copy). Version
      `gb/rail-prov`.
- [x] Task 2 of M2.1: update `README.md` rail sentence to name
      Configure Providers as the hive-wide desk.
- Commit: `Milestone 2.1: spec rail help and providers hub`.
- Verify: `rg -n "providers-hub|Configure Providers|#profile-info" UI_SPEC.md README.md`.

## Phase 3 — Implement

### M3.1 Rail markup + hub drawer

- [x] Task 1 of M3.1: wrap Profile / Settings / Call with
      `.rail-info-row` + `i` + popovers. Insert Configure
      Providers next to Add Channel. Split Min/Max vs Close/Exit.
- [x] Task 2 of M3.1: add `#providers-hub` before
      `#provider-drawer`. List nine runtimes + used-by. Click / ✎
      calls `openProviderDrawer(id)`. `#providers-btn` opens the
      hub. `openProviderDrawer` takes an optional id.
- Commit: `Milestone 3.1: rail help and providers hub UI`.
- Verify: `rg -n "id=\"providers-hub\"|id=\"providers-btn\"|id=\"profile-info\"|openProviderDrawer" hush-c/demo/index.html`.

## Phase 4 — Verify

### M4.1 Tests

- [x] Task 1 of M4.1: `check_pwa.sh` greps `#providers-btn`,
      `#providers-hub`, `#profile-info`, `#settings-info`,
      `#call-info`, `#prov-info`.
- [x] Task 2 of M4.1: `make -C hush-c test`.
- Commit: `Milestone 4.1: providers hub checks`.

## Phase 5 — Land

### M5.1 PR

- [ ] Task 1 of M5.1: push `gb/rail-prov`, `gh pr create --base main`.
- [ ] Task 2 of M5.1: merge (auto if allowed, else `--merge`).
- [ ] Task 3 of M5.1: pull main, remove worktree, delete branch.

## Audit

Every task has a command or snippet and a verify. Research gate is
M1.1. Worktree lifecycle is M0.1 + M5.1. Tasks are atomic.

Frozen for execution after M1.1 commit.
