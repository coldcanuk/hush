---
name: forge-skill
description: Create a System (application-wide) skill or a This robot skill.
role: any
---

# Forge a Hush skill

A Hush skill is a `SKILL.md` the hive equips onto a robot like an inventory
item. Forging writes a new file. Equipping points a robot at an existing one.
Pruning unequips; the file stays on disk.

## Two product buckets

| Product | Who may wear it | Disk |
|---|---|---|
| System (application-wide) | Any role-legal robot | shipped `skills/system/`; hive-forged `skills/user/` |
| This robot | Only that robot (Bender ↔ Futurama) | `skills/robots/<robot-slug>/` |

Hive-forged System skills keep ids `user:<slug>` so equipped loadouts stay
valid. Catalog JSON reports them as `"scope":"system"`.

## Layout (`~/.hush/`)

| Disk | Path |
|---|---|
| system | `~/.hush/skills/system/<slug>/SKILL.md` |
| user | `~/.hush/skills/user/<slug>/SKILL.md` |
| robot | `~/.hush/skills/robots/<robot-slug>/<slug>/SKILL.md` |

## File shape

```
---
name: short-slug
description: One-line summary.
---

# Title

What the robot should do when this skill is equipped.
```

`name` is lowercase `a-z0-9-`. `description` is one line. Body is Markdown.

## Equip and prune

Open a robot’s Edit inventory (`i`). Cycle gems like Diablo II amulets,
then drop one onto an empty loadout socket. Lift a worn gem to prune it.
Equipped ids persist on the robot (`skill_0` … `skill_7`). A This robot
skill cannot be equipped on a different slug.

## Forge (this skill)

1. Choose scope: `user` (System, application-wide) or `robot` (This robot).
2. Name, one-line summary, body.
3. `POST /api/skill` `{name, summary, body, scope, robot?}`.
4. The relay writes `SKILL.md` under the matching path.
5. Equip the new id onto the robot.

Do not forge into `system` from the UI. Do not write nsecs, API keys, or
`pass` secrets into a skill.

## Verify

```
ls ~/.hush/skills/user/<slug>/SKILL.md
curl -s http://127.0.0.1:10555/api/skills | grep <slug>
```
