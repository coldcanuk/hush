# PLAN: Lock Major name/prompt; enable/disable robots

**Branch:** `gb/robot-enable`
**Worktree:** `worktrees/robot-enable`
**Prime Directive:** PR to main only.

## Scope

Major’s name and standing orders are platform-locked. Every robot has an
Enable/Disable slider. Disabled icons are greyscale. Disabled robots do not
run mention jobs.

## Phase 0 — Isolation (COMPLETE)

`worktrees/robot-enable` / `gb/robot-enable`

## Phase 1 — Research (GATE)

This plan + `docs/research/RESEARCH_ROBOT_ENABLE.md`

## Phase 2 — Architecture

- `int enabled` + `int has_enabled` on agent in; `int enabled` on agent and
  `payne_enabled` on launch (default 1).
- HTTP `enabled` via `hush_json_bare_field` (JSON boolean).
- UI readonly name/prompt for Payne; switch row; greyscale class.

## Phase 3 — Implementation

M3.1 C persist + lock + skip disabled lookup + tests
M3.2 UI lock + slider + greyscale
M3.3 check_launch/UI_SPEC, make test, PR
