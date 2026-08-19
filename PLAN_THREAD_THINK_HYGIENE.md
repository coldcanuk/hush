# PLAN: thinking chip, thread pane, Grok hygiene, relay-live (RDAP)

Branch: `gb/thread-think-hygiene`
Worktree: `worktrees/thread-think-hygiene`
Base: `main` `7095430f1`

## 1. Methodology

RDAP. Four-minds gate is recorded in `RESEARCH_THREAD_THINK_HYGIENE.md`.
Commit after every Milestone on this branch. Land only via PR.
Never write `main`.

## 2. Scope

See research. Primary goal, non-goals, DoD, constraints, and risks
are locked there.

**Primary Goal**

1. A mentioned robot shows a small thinking chip until its reply lands.
2. A Thread button opens a dialogue of only the human + that robot;
   [x] returns to the channel; the same button reopens it.
3. Grok replies are one short note (the joke), not thought / GEMINI
   STATUS / npub / host telemetry.
4. Clicking `relay live` opens a details window with stored, projects,
   and sockets, closed by [x].

**Non-Goals**

Streaming, Codex/Goose live CLIs, nested NIP-10 trees, tray, editing
`/home/chuck/AGENTS.md`, Gemini API, OAuth/rail (already on main).

**Success Criteria**

Research architecture lock + `./configure && make && make test` +
PR merge + worktree removed.

**Constraints**

- C11, write-legible-c §14, `fn ≤ 40`, depth ≤ 2, ≤ 4 params.
- Prime Directive worktree. Embed UI after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo`.
- Hush never writes `~/.grok`. Agent cwd is under `HUSH_CONFIG_DIR`
  or `$TMPDIR`.

**Assumptions**

- `--cwd` to an empty dir stops AGENTS.md scan (E9).
- `--disallowed-tools` names match `~/.grok/README.md` table (E11).
- Fake grok in tests ignores extra flags.

**Environment**

`./configure && make && make test`. `gh` for the PR.

**Top risks**

1. AGENTS.md still loads → `--cwd` empty is the wall.
2. Tool-id drift → grep the README table; tests use fake grok.
3. `e` tag on the child not the root → lock e=root in HTTP + agent.
4. Status JSON overflow → named larger buffer.
5. Thinking flicker → chip keys only on `/api/status.thinking`.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `7095430f1`, worktree
      `/opt/repo/hush/worktrees/thread-think-hygiene` on
      `gb/thread-think-hygiene`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/thread-think-hygiene`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: four-minds evidence + Bayes in
      `RESEARCH_THREAD_THINK_HYGIENE.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on `gb/thread-think-hygiene`.
- Commit: `Milestone 1.1: research + frozen plan for thread, think, hygiene`

### Phase 2 — Architecture

#### M2.1 UI_SPEC contracts

- Task 1 of M2.1: Update `UI_SPEC.md` §13 (thinking chip, thread pane,
  Grok hygiene, `reply_to` on POST). Add §19 relay-live drawer.
  Routes: `GET /api/status` grows `thinking[]`; `POST /api/event`
  accepts `reply_to`.
- Verify: `rg -n "thinking|thread-pane|relay-drawer|disallowed-tools" UI_SPEC.md`.
- Commit: `Milestone 2.1: UI_SPEC for think, thread pane, hygiene, relay live`

### Phase 3 — Grok hygiene + thinking API

#### M3.1 Isolate grok + export jobs

- Task 1 of M3.1: `hush_agent_exec_grok` argv per research lock
  (`--cwd`, `--max-turns 1`, `--reasoning-effort none`,
  `--no-subagents`, `--disable-web-search`, `--disallowed-tools`,
  `--rules`). Raise `HUSH_AGENT_ARGV_MAX`. Create empty cwd under
  `HUSH_CONFIG_DIR/agent-cwd` or `$TMPDIR/hush-agent-cwd`.
- Task 2 of M3.1: Hygiene appended to the system-prompt override.
  Address the human by `profile.first_name` when set. `e` tag is the
  existing root if the parent already has `e`, else the parent id.
- Task 3 of M3.1: `hush_agent_status(char *out, size_t outsz)` writes
  `[{"name":"...","parent":"..."}]`. `/api/status` includes
  `"thinking":<array>`. Grow the status buffer; named constant.
- Task 4 of M3.1: `POST /api/event` reads optional `reply_to` and
  stores `e` before mentions. Do not re-dispatch a human self-p.
- Verify: `make -C hush-c` compiles. §14 checklist on touched C.
  `rg -n --cwd|--max-turns|hush_agent_status|reply_to` in
  `hush_agent.c` / `hush_http.c`.
- Commit: `Milestone 3.1: isolate grok, thinking status, reply_to POST`

### Phase 4 — UI

#### M4.1 Thinking chip + thread pane + relay drawer

- Task 1 of M4.1: CSS `.think` (8px pulse + name), `.thread-btn`,
  `#thread-pane`, `#relay-drawer`. Markup for both drawers with [x].
- Task 2 of M4.1: `#stream` shows roots only. Root with replies or a
  live job gets Thread · N. Pane lists human + that robot. Composer
  in the pane posts `reply_to` + `mention_0`.
- Task 3 of M4.1: `tick` reads `status.thinking` and paints the chip
  on the matching root. `#stats` is a button; opens `#relay-drawer`
  with stored / projects / sockets / port / version. `#relay-close`.
- Task 4 of M4.1: `./scripts/embed-ui.sh hush-c/demo`.
- Verify: `rg -n "thread-pane|thread-btn|relay-drawer|thinking" hush-c/demo/index.html`.
- Commit: `Milestone 4.1: thinking chip, thread pane, relay-live drawer`

### Phase 5 — Tests + docs

#### M5.1 Checks

- Task 1 of M5.1: `check_agent.sh` still expects `reply_to` + joke.
  Add: POST with `reply_to` stores `e`; `/api/status` contains
  `"thinking"`. Optional: fake grok that sleeps, poll thinking, then
  joke.
- Task 2 of M5.1: `check_launch.sh` greps `thread-pane`, `thread-btn`,
  `relay-drawer`, `relay-close`, `think`.
- Task 3 of M5.1: README / NOSTR one-liners: isolated grok, thread
  pane, thinking chip, relay-live drawer.
- Verify: `./configure && make && make test`.
- Commit: `Milestone 5.1: tests and docs for thread, think, hygiene`

### Phase 6 — Land

#### M6.1 PR + cleanup

- Task 1 of M6.1: push, `gh pr create --base main --head gb/thread-think-hygiene`.
- Task 2 of M6.1: `gh pr merge --auto --merge`. Wait MERGED.
- Task 3 of M6.1: on main checkout, `git pull --ff-only`,
  `git worktree remove worktrees/thread-think-hygiene`, delete branch.
- Verify: `git worktree list` is only main; `git status` clean.
- Commit: none on main.

## 4. Plan audit

- Every Task names its Milestone, has a command or file, and a verify.
- Phase 1 research → plan-update gate is this commit.
- Worktree path is inside `/opt/repo/hush/worktrees/`.
- Land is PR, not local merge.
- Tasks are atomic enough for one commit per Milestone.

Frozen for execution.
