# PLAN: Two skill buckets (system + robot)

**Branch:** `gb/skill-two-scope`
**Worktree:** `worktrees/skill-two-scope`
**Gate:** `docs/research/RESEARCH_SKILL_TWO_SCOPE.md`
**Land:** PR to main only (Prime Directive). Worktree path is `worktrees/<slug>`.

## Scope

### Primary goal
Armory and forge show **System** (application-wide) and **This robot** only.
Robot-specific skills (Bender / Futurama) cannot be equipped on another robot.

### Non-goals
Rewriting `user:` ids; deleting `user/` directory; skill executor; Payne identity.

### Success / DoD
- `GET /api/skills` `"scopes":["system","robot"]`.
- User-dir skills have JSON `scope":"system"` and keep `user:` ids.
- Armory labels are System and This robot. No “user” chip group.
- Forge: System (application-wide) / This robot.
- Equipping `robot:happy:x` on another slug is denied.
- `make test` + `check_launch.sh`. PR merged.

### Constraints
C11 + write-legible-c. Embed UI. Tests never write real `~/.hush` except via `HUSH_HOME`.

### Top risks
Id rewrite; test greps; Raise forge without slug.

## Phase 0 — Isolation (COMPLETE)

`worktrees/skill-two-scope` on `gb/skill-two-scope`.

**Verification:** `pwd | grep /hush/worktrees/skill-two-scope`

## Phase 1 — Research (GATE)

This file + RESEARCH.

**Milestone 1.1:** Commit research + frozen plan.

```
git add docs/research/RESEARCH_SKILL_TWO_SCOPE.md docs/plan/PLAN_SKILL_TWO_SCOPE.md
git commit -m "Milestone 1.1: freeze two-scope skill research and plan"
```

## Phase 2 — Architecture (locked in research §3)

Catalog maps user-dir → product scope `system`. UI two buckets. HTTP denies
cross-robot robot-skills. Forge copy + radios.

## Phase 3 — Implementation

### Milestone 3.1 — Catalog JSON two scopes + user-dir as system

`hush_skill.c` `format_json` scopes array. After `read_one` from user dir,
`slot->scope` = system. Keep id `user:slug`.

**Verification:** `test_skill` JSON scopes `system,robot`; `user:joke-book` still
in catalog; forged user skill `"scope":"system"`.

**Commit:** `Milestone 3.1: catalog presents system+robot only`

### Milestone 3.2 — Deny robot-skill on the wrong slug

`hush_http_check_loadout` takes slug; if `skill.robot` nonempty and ≠ slug,
`HUSH_ERR_DENIED`.

**Verification:** unit or launch POST happy’s robot skill onto another slug fails.

**Commit:** `Milestone 3.2: robot skills stay on their robot`

### Milestone 3.3 — Armory + forge UI

`paintSkillArmory` System / This robot. Forge radios. Help copy. CSS user gems
share system brass.

**Verification:** `check_launch` greps; no User-wide label.

**Commit:** `Milestone 3.3: armory and forge use two buckets`

### Milestone 3.4 — Tests, forge-skill text, UI_SPEC

`test_skill.c`, `check_launch.sh`, `skills/system/forge-skill/SKILL.md`,
`hush_skill.c` forge body, `UI_SPEC.md`.

**Commit:** `Milestone 3.4: tests and copy for two skill buckets`

## Final Phase

`make test`. Two live `GET /api/skills` greps. PR, merge, remove worktree.

```
git add . && git commit -m "Complete: two skill buckets – ready for merge"
git push -u origin HEAD
gh pr create --base main --head gb/skill-two-scope --title "…" --body "…"
gh pr merge --merge
```

## Audit (pre-exec)

- Research gate M1.1 present.
- Worktree is `worktrees/` + PR land (not local merge).
- Tasks atomic with verify + commit.
