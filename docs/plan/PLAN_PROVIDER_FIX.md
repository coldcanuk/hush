# PLAN: deepseek turn failure + provider selector UI

Follows RDAP. Research gate: `docs/research/RESEARCH_PROVIDER_FIX.md`.
Worktree `worktrees/provider-fix` on `gb/provider-fix`; commit per
milestone; land via PR only. `make test` (clean rebuilds included) green
at every milestone.

## Milestones

### M1.1 — Research + plan (this gate)

### M1.2 — `hush_provider_missing_reason()`
- New exported helper in `hush_provider.c`/`.h`: fills the human reason
  an unready provider is skipped/failed ("no model selected", "no API
  token stored", "not logged in", "runtime not installed", or "").
- Verify: clean `make test` green.

### M1.3 — Dispatch fallback + failure transparency
- `agent_dispatch.c`: when a job starts on a fallback (first-choice
  provider unready), post a one-line notice "<Primary> is not ready
  (<reason>); using <Fallback>."
- Append the same reason to `note_failure` when non-empty.
- Verify: clean `make test` green; check_failover.sh still passes.

### M2.1 — Provider selector restyle
- `demo/index.html`: `#agent-providers` three columns; drop the
  ready/picked fills (plain border, no background tint); add a status
  line per label: API → "api token present"/"no api token", local →
  "runtime installed"/"not installed", others →
  "authenticated"/"not authenticated".
- Verify: `make test` green (check_collaboration_ui.cjs exercises the
  checkboxes).

### M2.2 — Drawer model hint
- Provider drawer: when an API provider saves with a host but no model,
  set the status line to "No model selected — turns skip this provider
  until you scan or type one."
- Verify: `make test` green.

### M3 — Suite + docs + land
- Full clean suite; CHANGELOG entry; push, PR, auto-merge, cleanup.
- Support step (outside the repo): set `model` for deepseek-api in the
  operator's `~/.hush/config/providers.json` and report it.
