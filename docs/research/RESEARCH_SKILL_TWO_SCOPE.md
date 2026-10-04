# RESEARCH: Two skill buckets (system + robot)

**Date:** 2026-08-27
**Worktree:** `worktrees/skill-two-scope` / `gb/skill-two-scope`
**Methodology:** RDAP Phase 1
**Status:** Synthesis complete. Plan frozen in `docs/plan/PLAN_SKILL_TWO_SCOPE.md`.

## 1. Problem

Three product buckets (system / user / robot) do not match how skills are used.

- **System** = application-wide skills the human selects onto any robot.
- **Robot-specific** = one custom robot’s kit. Example: robot `bender` wears
  skill `Futurama`. That skill is not for anyone else.

“User skills” as a third pile does not work.

## 2. Evidence (current)

| Fact | Site |
|---|---|
| Three wire scopes | `hush_skill.h` `SCOPE_SYSTEM/USER/ROBOT` |
| Catalog JSON `"scopes":["system","user","robot"]` | `hush_skill_format_json` |
| Disk `~/.hush/skills/{system,user,robots/<robot-id>}` | `hush_home_skills_dir` |
| Forge allows `user` or `robot` only (system denied) | `hush_skill_forge` |
| Armory paints three labels | `paintSkillArmory` `["system","user","robot"]` |
| Forge radios “User-wide” / “This robot” | `#forge-drawer` |
| Robot skills hidden from other robots in UI only | `armorySkills` |
| Server `try_equip` does **not** check `skill.robot` | `hush_http_check_loadout` |
| Tests assert three scopes + `user:joke-book` | `test_skill.c`, `check_launch.sh` |

Shipped pack lives in `system/`. Hive-forged hive-wide files live in `user/`
so seed never overwrites them. IDs `user:<id>` may already be equipped.

## 3. Decision (locked)

**Two product buckets. Disk layout stays.**

| Product | Disk | ID | Who may wear it |
|---|---|---|---|
| System (application-wide) | `skills/system` (shipped) + `skills/user` (forged hive-wide) | `system:<robot-id>` / `user:<robot-id>` | Any role-legal robot |
| This robot | `skills/robots/<robot-id>/` | `robot:<robot-id>:<name>` | Only that robot id |

- After load, catalog `scope` for user-dir skills is **system** (id unchanged).
- JSON `"scopes":["system","robot"]`.
- UI armory: **System** and **This robot** only.
- Forge radios: **System (application-wide)** (`value=user`, writable hive-wide)
  and **This robot** (`value=robot`). Still cannot forge into the shipped pack
  (`scope=system` POST remains denied).
- Equip of `robot:<other>:<name>` on this robot id → `HUSH_ERR_DENIED` in
  `hush_http_check_loadout`.
- Non-goal: migrating existing `user:` ids; deleting `user/` on disk;
  per-slot item types; skill executor.

## 4. Risks

1. Equipped `user:` ids must keep working — do not rewrite ids.
2. Tests grep three-scope JSON — update to two.
3. Robot-only forge during Raise (no robot id) already fails PARSE; keep.

## 5. Remaining plan

See `docs/plan/PLAN_SKILL_TWO_SCOPE.md`.
