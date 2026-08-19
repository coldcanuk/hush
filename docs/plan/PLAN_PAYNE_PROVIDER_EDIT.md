# PLAN — Edit Sgt. Major Payne (provider + order only)

Worktree: `/opt/repo/hush/worktrees/payne-provider-edit`
Branch: `gb/payne-provider-edit`
Base: `main` `00f10480f`

Methodology: RDAP. Research gate:
[`../research/RESEARCH_PAYNE_PROVIDER_EDIT.md`](../research/RESEARCH_PAYNE_PROVIDER_EDIT.md).

## Scope

Click **Edit** on Sgt. Major Payne. Configure ranked SaaS / CLI
providers. Name, slug, standing orders stay locked. Payne stays
undeletable. No live API spawn. No fallback walk at job time.

## Audit of this plan

- Prime Directive: worktree inside `/opt/repo/hush/worktrees/`,
  land by PR only. User-template `../gb-*-wt` is forbidden.
- write-legible-c §14 on every C change. `fn ≤ 40`, depth ≤ 2,
  ≤ 4 params, named constants, prototypes at top.
- Hick / Fitts: do not shrink hive Close/Exit. Payne edit
  reuses the compact `#agent-drawer .actions` row.
- No feline words. Pass checkbox default-on on Raise. Never
  write `~/.grok` `~/.codex` `~/.config/goose`.
- Every task names its milestone, has a command or snippet,
  and a verify step.
- Research → plan-update gate is this file + the research
  commit (M1.1).

## Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: worktree
      `/opt/repo/hush/worktrees/payne-provider-edit` on
      `gb/payne-provider-edit` from `00f10480f`.
- Verify: `pwd | grep hush/worktrees` and
  `git branch --show-current` prints `gb/payne-provider-edit`.

### Phase 1 — Research (GATE)

- [x] Task 1 of M1.1: research file
      `docs/research/RESEARCH_PAYNE_PROVIDER_EDIT.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on the branch.
- Commit: `Milestone 1.1: research + plan for Payne provider edit`

### Phase 2 — Spec

- [x] Task 1 of M2.1: UI_SPEC §9 — Payne Edit exception:
      name/prompt locked; ranked providers 1…4; Delete stays
      disabled; session `payne.providers`.
- [x] Task 2 of M2.1: README one sentence that Payne's
      runtime order is editable.
- Verify: `rg "Edit Sgt Major Payne|payne.providers|ranked" UI_SPEC.md README.md`.
- Commit: `Milestone 2.1: spec Payne provider edit`

### Phase 3 — Persist + HTTP

- [x] Task 1 of M3.1: `hush_launch.h` — named
      `HUSH_LAUNCH_PAYNE_PROVIDERS_MAX = 4`, fields
      `payne_providers` / `npayne_providers`, prototype
      `hush_launch_set_payne_providers`.
- [x] Task 2 of M3.1: `hush_launch.c` — default `[goose]` on
      seed / missing vibe keys; put/take
      `npayne_providers` + `payne_provider_N`; session
      `payne.provider` + `payne.providers` array via a helper
      (do not grow `hush_launch_format_head` past 40 lines).
- [x] Task 3 of M3.1: `hush_http.c` — `POST /api/agent` with
      `slug == sgt-major-payne` updates the list; ignores
      name/prompt; delete still denied. New leaf
      `hush_http_update_payne`.
- Verify: `rg "payne_provider_|set_payne_providers|npayne_providers" hush-c`.
- Commit: `Milestone 3.1: persist Payne provider order`

### Phase 4 — Dispatch

- [x] Task 1 of M4.1: `hush_agent_lookup_robot` uses
      `launch->payne_providers[0]` when `npayne_providers > 0`,
      else Goose. Prompt stays `HUSH_LAUNCH_PAYNE_ABOUT`.
- Verify: `rg "payne_providers" hush-c/src/hush_agent.c`.
- Commit: `Milestone 4.1: Payne jobs use primary provider`

### Phase 5 — UI

- [x] Task 1 of M5.1: `robotModels` reads
      `session.payne.providers` (fallback `["goose"]`);
      `locked` stays true for name/prompt/delete, but Edit
      paints for Payne too.
- [x] Task 2 of M5.1: `openAgentDrawer` accepts Payne. Hide
      name, prompt, files, pass. Show order pills + radios.
      Save posts `slug` + `provider_N`. Delete stays disabled.
- [x] Task 3 of M5.1: card subtitle shows primary + `+N`.
- Verify: `rg "Edit Sgt Major Payne|payne-provider-pills|sgt-major-payne" hush-c/demo/index.html`.
- Commit: `Milestone 5.1: Payne Edit drawer for provider order`

### Phase 6 — Tests

- [x] Task 1 of M6.1: `test_launch.c` — set two providers,
      format session contains both, vibe.json has
      `payne_provider_0`, restore keeps order, name constant
      unchanged.
- [x] Task 2 of M6.1: `check_launch.sh` greps Edit on Payne
      path (`openAgentDrawer(bot)` for locked, order pills,
      `payne-provider`). POST update + GET session.
- [x] Task 3 of M6.1: roster still refuses Payne delete.
- Verify: later full suite.
- Commit: `Milestone 6.1: tests for Payne provider order`

### Phase 7 — Verify + land

- [x] Task 1 of M7.1: `./configure && make && make test`.
- [ ] Task 2 of M7.1: push, `gh pr create`,
      `gh pr merge --auto --merge`, wait MERGED.
- [ ] Task 3 of M7.1: on main checkout pull, remove worktree,
      delete `gb/payne-provider-edit`.
- Commit: none after land; cleanup on main checkout.

## Out of scope

Rename / rewrite Payne. Delete Payne. Context files on Payne.
Live spawn for non-`grok-build`. Fallback walk. Raised-robot
multi-provider lists. Hive Close/Exit size.
