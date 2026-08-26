# PLAN: Equal-size inventory icons, right-click edit, ~/.hush, skill equip/forge

**Branch:** `gb/robot-inv-edit`
**Worktree:** `worktrees/robot-inv-edit`
**Base:** `main`
**Methodology:** RDAP. Phase 1 synthesis: `docs/research/RESEARCH_ROBOT_INV_EDIT.md`.
**Prime Directive:** Worktree only. PR to land. No local merge to main.
**C:** C11 strict + write-legible-c on every `.c`/`.h`.
**UI:** `hush-c/demo/index.html` then `sh ../scripts/embed-ui.sh demo`.

## Scope

### Primary goal
Robots Inventory tiles are equal-size icons. Right-click (macOS: meta+click)
opens a one-item `edit` menu. Edit dialogue: name, avatar, system prompt,
whisper-gated voice, equip/prune/forge skills from system/user/robot scopes.
Payne display name is `Major`. Install/first-run creates `~/.hush/{config,agents,…}`.

### Non-goals
Variable-size tiles; Raylib; live LLM forge; skill executor rewrite; `pass`
migration; replacing Payne ranked providers; auto-converting every historical
`~/.config/hush` file.

### Success / DoD
Matches the goal acceptance criteria: equal tiles, hover name, no-avatar
border, `edit` menu, dialogue fields, three-scope loadout + forge, Major,
`~/.hush` tree, tests, two live launches, PR.

### Constraints
Worktree path contains `/hush/worktrees/`. Tests never write the real user
home. `HUSH_CONFIG_DIR` / `HUSH_HOME` override. OBJECTIVE wins over UI_SPEC
Payne name lock.

## Phase 0 — Isolation (COMPLETE)

Worktree `worktrees/robot-inv-edit` on `gb/robot-inv-edit`.

## Phase 1 — Research (GATE)

Research document + this frozen plan.

## Phase 2 — Architecture (locked)

1. `hush_home` owns `~/.hush` layout and ensure.
2. `hush_skill` owns catalog, forge writer, voice id table.
3. Roster/launch persist picture, voice, equipped skill ids; Payne name/prompt
   stored on `hush_launch_t`, default Major.
4. HTTP: `GET/POST /api/skills` + `POST /api/agent` update.
5. UI: 1×1 tiles, `#inv-menu`, agent drawer fields, armory/loadout, forge drawer.

## Phase 3 — Implementation milestones

M3.1 Home + Major + persist extras + C tests.
M3.2 Inventory tiles + context menu.
M3.3 Edit dialogue persist (name/avatar/prompt/voice).
M3.4 Skill catalog, equip/prune, forge-skill, tests.
M3.5 Embed, check_launch/check_agent, two launches, `make test`, PR.

Each milestone: `git add . && git commit`.

## Verification

Per goal plan: C tests + HTML/API greps + two live `GET /` and `/api/session`
+ `make test`. Playwright optional.
