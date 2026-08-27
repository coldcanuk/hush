# PLAN: Apply MAP skill remap (frozen after research gate)

**Branch:** `gb/skill-pack-map`  
**Worktree:** `worktrees/skill-pack-map`  
**Gate:** `docs/research/RESEARCH_SKILL_PACK_MAP.md`  
**Land:** PR to main only.

MAP = T3 (posterior ≈ 0.70): cluster-fold + Hush-rename vendor ids. Keep 20 chaperon skills. No new locked templates.

## Remaining phases

M3.1 Rewrite `skills/system/` per MAP table; delete folded/old vendor dirs; Coach/Auditor skill ids.  
M3.2 Point `test_skill` / `check_launch` / `test_roster` / `hush_launch` at new ids; assert old dumps absent; Hush-adapted greps; role wall + watermarks.  
M3.3 `make test`, two launches, embed UI, PR.

## Verification

Catalog JSON scopes + watermarks. ≥20 chaperon. Named sources represented. `mobile-trace` names Android and iOS. Old slugs absent. Clone Coach / deny Major.
