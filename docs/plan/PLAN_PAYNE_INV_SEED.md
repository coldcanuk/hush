# PLAN: Payne in 4×3 inventory + Seed wizard (RDAP)

Branch: `gb/payne-inv-seed`
Worktree: `worktrees/payne-inv-seed`
Base: `main` `0bc5985c0`

Land via PR only. Isolation is Prime Directive, not `../gb-*-wt`.

## Scope

**Primary.** Live roster (Payne + raised robots like Happy) occupies
`#robot-inventory`. Compact grid **4×3**, centered, ≤ nav width.
Uniform Seed/Clear/Raise. Expanded inventory drawer. Seed is a
Payne-gated intent wizard that creates **real** cooperating agents.

**Non-goals.** Unbounded HRI essays; leftover rail PRs; Raylib;
restoring `#robot-list` as primary; C protocol rewrite; a new seed
C job if `/api/agent` + `/api/channel` suffice.

**Success.** Criteria in the goal: Payne visible without Seed; 4×3;
wizard+gate; `make test` catches 8-col compact and demo-only Seed;
PR to `main`.

**Constraints.** HTML/JS + `UI_SPEC.md` + tests. Embed via `make`.
C11 only if a new route is required (prefer existing APIs).

**Risks.** (1) Empty `invItems` vs hidden list — bind to
`robotModels()`. (2) 8-col overflow — 4×3 compact, expand in drawer.
(3) Seed without Payne LLM — refuse with quoted copy. (4) No C seeder
job — `POST /api/agent` with wizard prompt.

## Phases

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: worktree `worktrees/payne-inv-seed` /
      `gb/payne-inv-seed` from `0bc5985c0`.
- Verify: `pwd` contains `/hush/worktrees/` ; branch name.

### Phase 1 — Research (GATE)

- [x] Task 1 of M1.1: `docs/research/RESEARCH_PAYNE_INV_SEED.md`.
- [x] Task 2 of M1.1: this plan.
- Commit: `Milestone 1.1: research + frozen plan for Payne inventory and Seed`

### Phase 2 — Architecture (frozen)

1. One occupancy unit: compact 4×3 constants + expand flag/drawer 8×5.
2. Tiles from `session.payne` + `session.agents` (`syncInventoryFromRoster`).
3. Seed wizard drawer → prompt string + channel; `payneCanReply()` gate;
   `seedTeam()` via `/api/agent` + `/api/channel`.
4. `UI_SPEC.md` compact 4×3 (OBJECTIVE wins over 10×5 / 8×5).

- Commit: `Milestone 2.1: architecture lock roster-grid + Payne seed wizard`

### Phase 3 — Inventory

#### M3.1 Live roster + 4×3 + uniform buttons + expand

- Task 1: CSS compact 4-col, center, `inv-btn` equal grid; drop duplicate
  8-col rules.
- Task 2: `INV_COLS=4` `INV_ROWS=3`; `INV_EXPAND_COLS=8` `INV_EXPAND_ROWS=5`;
  `syncInventoryFromRoster` in `paint`; persist layout by slug;
  Payne 1×3, others 1×1; Clear resets layout not roster.
- Task 3: Expand drawer `#inv-expand-drawer` / `#robot-inventory-full`.
- Task 4: `UI_SPEC.md` §4 compact 4×3.
- Task 5: `make -C hush-c` (embed).
- Verify: `rg 'INV_COLS = 4'` and no compact `INV_COLS = 8`;
  `syncInventoryFromRoster` present; `id="inv-expand"`.
- Commit: `Milestone 3.1: live roster in 4x3 inventory, uniform buttons`

### Phase 4 — Seed wizard

#### M4.1 Payne-gated prompt → real agents

- Task 1: `#seed-drawer` markup (actions, skills, project, existing/new
  channel, preview, error, submit) before `<script>`.
- Task 2: `buildSeedPrompt`, `payneCanReply`, `seedTeam` using
  `/api/agent` (≥2) and channel create/manage; Seed click opens drawer
  not `seedInventoryDemo`.
- Task 3: embed.
- Verify: Seed listener is not `seedInventoryDemo`; `payneCanReply` and
  `seedTeam` exist; drawer ids present.
- Commit: `Milestone 4.1: Seed wizard gated on Payne LLM, creates real agents`

### Phase 5 — Tests + probe

#### M5.1 Contracts + make test + curl/live

- Task 1: `check_launch.sh` — compact 4×3, forbid `INV_COLS = 8` as
  default, uniform `inv-btn`, seed drawer, not demo Seed, Payne still
  in session JSON.
- Task 2: `check_agent.sh` — after vibe Payne present; Happy raise
  still works; with Payne on grok-build, seed-equivalent ≥2 agents +
  channel choice.
- Task 3: `make test` → `{SCRATCH}/make-test.txt`.
- Task 4: `{SCRATCH}/seed-verify.txt` (curl). Playwright `{SCRATCH}/inv-live.txt`
  if browser works.
- Commit: `Milestone 5.1: tests for 4x3 roster inventory and Seed wizard`

### Final — Land

- Push, `gh pr create --base main --head gb/payne-inv-seed`,
  `gh pr merge --auto --merge`, pull main, remove worktree.
