---
name: goose-init
description: "Bootstrap or repair .goose/ layout for Hush (Goose-only agent)."
---
# goose-init

Ensure:
- .goose/GOOSE.md exists with prime directives
- .goose/skills/<name>/SKILL.md for core (worktree, c-build, c-test, legible-c, relay, publish, goose-init)
- AGENTS.md is the short Hush Goose version
- No .agents/ .claude/ .codex/
- Update README/CONTRIBUTING if they mention other agents
