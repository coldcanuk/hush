# PLAN: Diablo II skill cycle + drag-slot onto robots

**Branch:** `gb/skill-slot-ui`
**Worktree:** `worktrees/skill-slot-ui`
**Base:** `main` @ 14517cbad
**Methodology:** RDAP. Phase 1 synthesis: `docs/research/RESEARCH_SKILL_SLOT_UI.md`.
**Prime Directive:** Worktree only. PR to land. No local merge to main.
**C:** C11 strict + write-legible-c on any `.c`/`.h`.
**UI:** `hush-c/demo/index.html` then `sh ../scripts/embed-ui.sh demo`.

## Scope

### Primary goal
Cycle skills as stash gems. Hold one on the cursor. Slot it onto a chosen robot
(inventory tile = character; editor loadout = 8 paper-doll sockets).

### Non-goals
Skill executor rewrite; watermark/role changes; HTML5 DnD; Raylib; per-slot
item types; chip-wall preservation.

### Success / DoD
- Hive stash visible under Robots Inventory; ◀ ▶ / wheel cycles one gem.
- Pointer hold + ghost. Drop on a robot tile persists that robot’s loadout.
- Editor `#skill-loadout` is 8 sockets, not wrapping chips. `#skill-armory` is
  the stash stage, not a catalog dump.
- Locked templates refuse; Payne can wear skills; role/watermark still refuse.
- `make test` + `check_launch.sh` greps for hold/drop/slots. Two live GETs.
- PR to main.

### Constraints
Worktree path contains `/hush/worktrees/`. Keep existing skill element ids.
Tests never write the real user home.

### Assumptions
Backend `POST /api/agent` `skill_0…` + `nskills` is sufficient. No new route.

### Top risks
Inventory pointer clash; Payne vs locked-template; missing hive surface.

## Phase 0 — Isolation (COMPLETE)

```
cd /opt/repo/hush
git checkout main && git pull --ff-only origin main
git worktree add -b gb/skill-slot-ui worktrees/skill-slot-ui
cd worktrees/skill-slot-ui
```

**Verification:** `pwd | grep /hush/worktrees/skill-slot-ui` && branch `gb/skill-slot-ui`.

## Phase 1 — Research (GATE)

This document + `docs/research/RESEARCH_SKILL_SLOT_UI.md`.

**Milestone 1.1:** Synthesize research and freeze this plan. Commit.

```
git add docs/research/RESEARCH_SKILL_SLOT_UI.md docs/plan/PLAN_SKILL_SLOT_UI.md
git commit -m "Milestone 1.1: freeze Diablo II skill-slot research and plan"
```

**Verification:** files exist; plan names hive stash + paper doll + cursor hold.

## Phase 2 — Architecture (locked in research §5)

- `skillHeld` cursor state, ghost `#skill-ghost`.
- Hive `#hive-skill-cycle` belt.
- Editor 8 `.skill-slot` sockets + restyled `#skill-armory`.
- Persist: existing `skill_N` POST. Hive drop is immediate.

No new C API unless forge-skill copy is updated.

## Phase 3 — Implementation

### Milestone 3.1 — CSS + hive stash markup + editor doll markup

Edit `hush-c/demo/index.html`:

- CSS: `.skill-gem`, `.skill-slot`, `.skill-doll`, `.skill-ghost`,
  `.inv-item.skill-drop`, `.skill-belt`.
- Hive nav under `#robot-inventory`: stash belt ids `hive-skill-*`.
- Editor: keep cycle ids; restyle board as doll + stash stage.
- Ghost node `#skill-ghost`.

**Verification:** `grep -q 'id="hive-skill-cycle"' hush-c/demo/index.html`

**Commit:** `Milestone 3.1: skill gem, stash belt, and paper-doll markup`

### Milestone 3.2 — Cycle + cursor hold + drop on robots

JS: catalog paint for hive + editor; `pickUpSkill` / `placeSkill` /
`wearSkillOnRobot`; gate `beginInvDrag`; window pointermove/up; Escape.

**Verification:** `grep -q 'skillHeld' hush-c/demo/index.html` and
`grep -q 'wearSkillOnRobot' hush-c/demo/index.html`

**Commit:** `Milestone 3.2: cycle gems and drop them onto robots`

### Milestone 3.3 — Editor sockets, prune, copy, locked/role/watermark

8 sockets; swap; prune-to-stash; refuse locked/role/full with Payne copy.
Remove `makeSkillChip` click-toggle as the primary path.

**Verification:** `grep -q 'skill-slot' hush-c/demo/index.html` and no
armory `forEach` chip dump of all scopes.

**Commit:** `Milestone 3.3: paper-doll sockets and refuse rules`

### Milestone 3.4 — Copy, UI_SPEC, forge-skill text, tests

- `UI_SPEC.md` hive stash + doll.
- `skills/system/forge-skill/SKILL.md` + `hush_skill.c` forge body: cycle/drag,
  not “click a chip”.
- `check_launch.sh` greps: `hive-skill-cycle`, `skillHeld`, `skill-slot`,
  `wearSkillOnRobot`, `skill-ghost`.

**Commit:** `Milestone 3.4: spec, forge copy, launch greps`

### Milestone 3.5 — Embed, build, test, two launches

```
cd hush-c
sh ../scripts/embed-ui.sh demo
cd ..
./configure && make
make test
```

Two live relays: `GET /` contains hive stash + skill-slot; `GET /api/skills` still
lists scopes.

**Commit:** `Milestone 3.5: embed UI and verify skill-slot`

## Final Phase — PR

```
git push -u origin HEAD
gh pr create --base main --head gb/skill-slot-ui --title "…" --body "…"
gh pr merge --auto --merge
```

After merge: pull main, remove worktree, delete branch.

## Audit (pre-execution)

- Every milestone has commands, verification, commit.
- Research gate is M1.1.
- Worktree lifecycle matches AGENTS.md (PR, not local merge).
- Tasks are UI-atomic; C only for forge copy.
