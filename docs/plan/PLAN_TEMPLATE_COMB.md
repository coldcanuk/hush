# PLAN: Iterate template/skill quality to panel ≥8 (frozen)

**Branch:** `gb/template-comb`  
**Worktree:** `worktrees/template-comb`  
**Gate:** `docs/research/RESEARCH_TEMPLATE_COMB.md`  
**Land:** PR to main only.

Pre-iteration: C1 7.2, C5 7.3, C6 6.8 must rise. C2/C3/C4 already ≥8.

## Remaining

M3.1 Fold bring-back, add intro-once; Marshal 8-slot rails; Major default hive-patterns; forge-skill role any.  
M3.2 Rewrite thin SKILL.md bodies (actionability) via `scripts/write-skill-pack.sh`.  
M3.3 Tests: names/icons, Marshal 8 ids try_equip as chaperon, intro-once present, bring-back absent, Major clone deny. `make test`, two launches, PR.

## Do not

New locked army. npm/jadx. Delete Marshal. Change Enable/Disable or Major clone-deny.
