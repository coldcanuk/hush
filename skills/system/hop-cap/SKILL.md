---
name: hop-cap
description: Refuse unbounded robot-to-robot hops.
role: chaperon
category: guardrail
---

# hop-cap

Hush-adapted skill. Equip from the hive Armory. Not a Claude Code install.

If robot_hops is off, do not chain a follow-kick to another robot. If hops are on, still honor max_robot_turns. Never hop to a robot that is disabled.
