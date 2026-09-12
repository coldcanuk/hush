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

## Tranche 2 (worktree `agent-split2`, branch `gb/agent-split2`)

Follows the same RDAP gate; research doc section (d) documents the final
state. Commit per milestone; land via PR only.

### M6.1 — `agent_dispatch.c`
- Move the contiguous tail (job lifecycle, dispatch/follow flow, election
  and planning waves) plus the follow table into `agent_dispatch.c`.
- The follow table moves with its only users (follow_find/follow_take);
  the core calls `hush_agent_follow_init()` once. Shared prompt strings
  and cross-module entry points move to `hush_agent_internal.h`.
- Verify: clean `make test` green.

### M6.2 — `agent_text.c`
- Move the text utilities (whitespace/npub scanning, line snipping,
  snippet completion) and the mention-rewrite/alias/scrub cluster.
- Verify: clean `make test` green (check_agent group-scenario prompt
  assertions exercise the rewrite path).

### M6.3 — `agent_prompt.c`
- Move fill_prompt/fill_rules, the fill_job family, instruction/guidance
  appends, and the peer/last-rule prompt rules. Export fill_job and
  add_guidance; export the core helpers they need (key_matches,
  pick_provider, make_token).
- Verify: clean `make test` green.

### M6.4 — `agent_fixup.c`
- Move start_fixup/take_fixup, the token mint, token lookup, and the
  fixup prompt fill, plus the token sequence and fixup strings. Export
  grok_ready and find_slot; find_token borrows the job table through the
  accessor.
- Verify: clean `make test` green.

### M6.5 — Docs + land
- Update research (final map), CHANGELOG, retargeted test greps. Push, PR,
  auto-merge, cleanup, clean `make test` on main.
