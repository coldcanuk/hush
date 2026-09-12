# PLAN: split hush_agent.c (tranche 1) — process + thread clusters

Follows RDAP. Research gate: `docs/research/RESEARCH_AGENT_SPLIT.md`.
Worktree `worktrees/agent-split` on `gb/agent-split`; commit per milestone;
land via PR only. Behavior-preserving; `make test` (clean rebuilds included)
green at every milestone.

## Milestones

### M5.1 — Research + plan (this gate)

### M5.2 — Internal header + shared helpers
- New `hush-c/include/hush_agent_internal.h`: all 12 types verbatim plus
  `hush_agent_copy`, `hush_agent_trim`, `hush_agent_prepare_cwd`,
  `hush_agent_event_channel`, `hush_agent_event_root`,
  `hush_agent_human_name`.
- `hush_agent.c`: drop `static` from those helpers; delete their top-block
  prototypes; include the internal header.
- Verify: clean `make test` green.

### M5.3 — `agent_process.c`
- Move lines 1919-2323 verbatim; keep internals static; export the entries
  the core calls (compile loop). Constants move with their owners.
- Verify: clean `make test` green (check_agent/check_provider/check_codex
  exercise the exec paths).

### M5.4 — `agent_thread.c`
- Move lines 1517-1749 verbatim; export `hush_agent_fill_thread` (and any
  other core-called entries).
- Verify: clean `make test` green (check_agent memory/context flows).

### M5.5 — Docs + land
- Update research (final map + remaining clusters), CHANGELOG. Push, PR,
  auto-merge, cleanup, clean `make test` on main.
