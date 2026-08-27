# PLAN: Skill equip watermarks, chaperon wall, SkillUI-C, cloneable templates

**Branch:** `gb/skill-equip`  
**Worktree:** `worktrees/skill-equip`  
**Gate:** `docs/research/RESEARCH_SKILL_EQUIP.md`  
**Land:** PR to main only.

## Remaining phases (frozen)

M3.1 Equip cycle + role partition + watermark refuse + catalog JSON fields. Done.  
M3.2 ≥20 chaperon skills + named-source Hush packs + Reverse Engineering category. Done.  
M3.3 C SkillUI extract (fixture HTML → colors/fonts/spacing + skill package). Done.  
M3.4 Locked cloneable robots; Major clone denied; enable/disable unchanged. Done.  
M3.5 Tests, two launches, embed UI, PR.

## Verification

`test_skill.c` + `test_skillui.c` + `test_launch.c` + `check_launch.sh`. Two live relays. `make test`.
