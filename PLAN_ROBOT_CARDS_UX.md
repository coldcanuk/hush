# PLAN: Robot cards expand/collapse + Raise New Robot form (RDAP)

Branch: `gb/robot-cards-ux`
Worktree: `worktrees/robot-cards-ux`
Base: main `c65c65b83`

## 1. Methodology

RDAP — research first, synthesis gate, atomic milestones, commit after
every milestone on the worktree branch. Land on `main` only via PR.

## 2. Scope

### Primary Goal

1. Sidebar lists every robot as its own expand/collapse card (`+`/`-`).
   Payne first, then raised agents.
2. Raise a robot drawer is modernized: pill-commit name, required
   system prompt (replaces standing orders), max 3 plaintext/Markdown
   context files, required AI provider, red delete at the bottom.

### Non-Goals

- Spawning Goose/Codex/Cline/API processes.
- Live LLM inference.
- Deleting Payne.
- Removing leftover `pass` secrets.
- Avatar, theme, channel, or project changes.

### Success Criteria

1. `#robot-list` renders Payne + `session.agents[]` as independent cards.
2. Each card has a 44px `+`/`-` that expands/collapses details.
3. Name: input + `+` → pill + pencil; empty name auto-generates.
4. System Prompt required: textarea + `+` → pill + pencil + `-`.
5. Context: max 3, plaintext/Markdown, `+` file browser, `-` remove.
6. Provider required from the locked eight-name list.
7. Red delete at form bottom; works for existing non-Payne robots.
8. Server rejects missing provider, missing prompt, 4th file, bad MIME.
9. `make test` + `check_launch.sh` pass. HTML embedded.

### Constraints

- C11, write-legible-c §14 on every C commit.
- Worktree only; PR only.
- JSON parser is string-field only → indexed context keys.
- Embed: `./scripts/embed-ui.sh hush-c/demo` from worktree root.

### Assumptions

- Provider is a roster label, not a runtime.
- Independent card expand (not forced accordion).
- Delete drops the in-memory roster slot only.

### Top Risks

1. Session JSON growth → preview-truncate prompt to 160 chars.
2. HTTP/roster line budgets → small helpers only.
3. Delete-on-create affordance → always visible, disabled until slug.

## 3. Plan

### Phase 0 — Isolation (done)

Worktree `worktrees/robot-cards-ux` on `gb/robot-cards-ux`.

### Phase 1 — Research (this file + RESEARCH.md)

- [x] Inspect sidebar, drawer, roster, HTTP, tests.
- [x] Synthesize RESEARCH.md and freeze this plan.

Commit: `Milestone 1.1: research + frozen plan for robot cards UX`

### Phase 2 — Architecture

Locked in RESEARCH.md. No extra code in this phase beyond constants
and UI_SPEC updates that name the new contract.

- M2.1 Update `UI_SPEC.md` raise-robot + sidebar robots + caps
  (context max 3, provider required, cards).
- Commit: `Milestone 2.1: lock robot-card and raise-form contract`

### Phase 3 — Roster + HTTP

- M3.1 Provider allowlist, `HUSH_ROSTER_CONTEXT_MAX = 3`,
  `provider` on agent + in, `hush_roster_is_provider`, require
  prompt + provider on add, format `provider` + prompt preview.
- M3.2 `hush_roster_remove_agent` by slug (not Payne).
- M3.3 HTTP: parse provider, up to 3 indexed context triples,
  `action=delete`. Launch wrappers.
- M3.4 Tests in `test_roster.c` + `check_launch.sh`.
- Commits per milestone.

### Phase 4 — UI

- M4.1 CSS: robot cards, pills, icon +/- / pencil, danger button.
- M4.2 `#robot-list` + `paintRobots()` expand/collapse.
- M4.3 Raise drawer: name pill, system prompt pill, 3-file list,
  provider radios, red delete.
- M4.4 Wire save/delete/edit; autogen name; client MIME + caps.
- M4.5 Embed UI. Extend `check_launch.sh` HTML greps.
- Commits per milestone.

### Phase 5 — Verify + land

- Full `./configure && make clean && make && make test`.
- write-legible-c §14 on C diff.
- Docs: agent-create skill.
- Final commit, push, PR, auto-merge, delete worktree.

## 4. Audit of this plan

- Research → plan-update gate: Phase 1 last task (this file).
- Worktree lifecycle: Phase 0 start + Phase 5 PR (not local merge).
- Tasks are atomic and name verification.
- C changes stay in roster/http/launch; UI in `index.html`.
