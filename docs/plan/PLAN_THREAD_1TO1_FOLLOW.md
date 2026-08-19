# PLAN — 1:1 thread follow-up inherit

Worktree: `/opt/repo/hush/worktrees/thread-1to1-follow`
Branch: `gb/thread-1to1-follow`
Base: `main` `0335fa2d2`

Methodology: RDAP. Research gate:
`docs/research/RESEARCH_THREAD_1TO1_FOLLOW.md`.

## Scope

In a 1:1 thread pane, a follow-up without a new `@` still addresses
the sole member robot. 1:n stays mention-gated. Channel notes
without a `p` tag stay silent.

## Audit of this plan

- Prime Directive: worktree inside `/opt/repo/hush/worktrees/`,
  land by PR only.
- write-legible-c: no C change expected. If a test in C is added,
  apply §14.
- Hick / Fitts: do not shrink hive Close/Exit. Think chip already
  exists; we only feed it a job.
- No feline words. Pass checkbox default-on. Never write
  `~/.grok` `~/.codex` `~/.config/goose`.

## Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: worktree
      `/opt/repo/hush/worktrees/thread-1to1-follow` on
      `gb/thread-1to1-follow` from `0335fa2d2`.

### Phase 1 — Research (GATE)

- [x] Task 1 of M1.1: four-minds file
      `docs/research/RESEARCH_THREAD_1TO1_FOLLOW.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on the branch.
- Commit: `Milestone 1.1: research + plan for 1:1 follow-up`

### Phase 2 — Architecture

- [x] Task 1 of M2.1: UI_SPEC §13: 1:1 inherit exception.
      README one sentence if the thread paragraph exists.
- Verify: `rg "1:1 inherit|sole member" UI_SPEC.md`.
- Commit: `Milestone 2.1: spec 1:1 thread inherit`

### Phase 3 — Composer

- [ ] Task 1 of M3.1: thread submit attaches the sole robot when
      this send has no robot pills.
- [ ] Task 2 of M3.1: optimistic `#thread-think` after that send
      until live thinking or a new reply.
- Verify: `rg` shows inherit + 1:n ban still present.
- Commit: `Milestone 3.1: 1:1 follow-up mentions the sole robot`

### Phase 4 — Tests

- [ ] Task 1 of M4.1: `check_launch.sh` greps the inherit.
      Keep the 1:n `bots.filter` ban.
- Verify: `make -C hush-c test` later.
- Commit: `Milestone 4.1: launch check covers 1:1 inherit`

### Phase 5 — Verify + land

- [ ] Task 1 of M5.1: `./configure && make && make test`.
- [ ] Task 2 of M5.1: PR → merge → delete worktree.
- Commit: none after land; cleanup on main checkout.

## Out of scope

C intel inherit without a `p` tag. Channel composer inherit.
1:n auto-pills. Deepseek live client. Hive Close/Exit size.
