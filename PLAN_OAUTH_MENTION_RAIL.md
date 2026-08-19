# PLAN: OAuth has_home, @mention Grok replies, rail docks (RDAP)

Branch: `gb/oauth-mention-rail`
Worktree: `worktrees/oauth-mention-rail`
Base: `main` `e30cb1bef`

## 1. Methodology

RDAP. Four-minds gate is recorded in `RESEARCH_OAUTH_MENTION_RAIL.md`.
Commit after every Milestone on this branch. Land only via PR.

## 2. Scope

See research. Primary goal, non-goals, DoD, constraints, and risks are locked there.

**Primary Goal**

1. OAuth “authenticated” is true only for the provider whose own auth artifact exists.
2. `@Happy` on a Grok Build robot starts a thread and Happy replies via `grok -p`.
3. The tool rail can be dragged and snapped to six docks.

**Non-Goals**

Streaming, Codex/Goose live CLIs, NIP-10 pane, tray, a global OAuth bit.

**Success Criteria**

Research architecture lock + `./configure && make && make test` + PR merge + worktree removed.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `e30cb1bef`, worktree
      `/opt/repo/hush/worktrees/oauth-mention-rail` on `gb/oauth-mention-rail`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is `gb/oauth-mention-rail`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: four-minds evidence + Bayes in
      `RESEARCH_OAUTH_MENTION_RAIL.md`.
- [x] Task 2 of M1.1: this plan.
- Verify: both files exist on `gb/oauth-mention-rail`.
- Commit: `Milestone 1.1: research + frozen plan for oauth, mention, rail`

### Phase 2 — Architecture

#### M2.1 UI_SPEC contracts

- Task 1 of M2.1: §12 Codex/Grok `has_home` artifacts. §13 live Grok reply
  + `reply_to`. §15 rail docks.
- Verify: `rg -n "auth.json|reply_to|rail-docks" UI_SPEC.md`.
- Commit: `Milestone 2.1: UI_SPEC for provider home, replies, rail docks`

### Phase 3 — OAuth has_home (H1)

#### M3.1 Provider-specific home

Files: `hush-c/src/hush_provider.c`, `hush-c/tests/test_provider.c`,
`hush-c/tests/check_provider.sh`.

- Task 1 of M3.1: `hush_provider_file_nonempty`. Grok = nonempty
  `~/.grok/auth.json`. Codex = nonempty `~/.codex/auth.json` or
  `~/.codex/config.toml`.
- Task 2 of M3.1: unit tests in isolated HOME (dir-only Codex is not
  `has_home`; nonempty auth is).
- Task 3 of M3.1: `check_provider.sh` asserts grok-build/codex `has_home`
  false on a bare temp HOME.
- Verify: `make -C hush-c tests/test_provider && ./hush-c/tests/test_provider`.
- Commit: `Milestone 3.1: Codex/Grok has_home is the auth artifact`

### Phase 4 — Mention replies (H3)

#### M4.1 hush_agent + HTTP hook

Files: `hush-c/include/hush_agent.h`, `hush-c/src/hush_agent.c`,
`hush-c/src/hush_http.c`, `hush-c/src/hush_relay.c`.

- Task 1 of M4.1: new module (write-legible-c §15). `consider` / `poll` /
  init / shutdown. Cap 4 jobs. 90s timeout. Grok argv locked in research.
- Task 2 of M4.1: `POST /api/event` calls `hush_agent_consider` after insert.
  Pump calls `hush_agent_poll`. Cleanup calls shutdown.
- Task 3 of M4.1: `/api/events` emits `reply_to` from the first `e` tag.
- Verify: `make -C hush-c` compiles. §14 checklist on new/changed C.
- Commit: `Milestone 4.1: mention dispatches Grok Build replies`

#### M4.2 UI thread indent

File: `hush-c/demo/index.html` then embed.

- Task 1 of M4.2: `.note.reply` indent. `render` uses `reply_to`.
- Verify: `rg -n "reply_to|note reply" hush-c/demo/index.html`.
- Commit: `Milestone 4.2: indent mention threads in the stream`

### Phase 5 — Tool rail (H5)

#### M5.1 Drag + docks

File: `hush-c/demo/index.html` then embed.

- Task 1 of M5.1: grip ≥44px, window pointer listeners, six docks, snap
  48px, persist `anchor`, clamp, resize reapplies dock.
- Verify: `rg -n "rail-docks|railAnchor|hush-rail" hush-c/demo/index.html`.
- Commit: `Milestone 5.1: tool rail docks and a Fitts grip`

### Phase 6 — Tests, docs, land

#### M6.1 Tests + docs

- Task 1 of M6.1: `hush-c/tests/check_agent.sh` — fake `grok`, nonempty
  auth.json, raise Grok robot, mention, wait for `reply_to`.
- Task 2 of M6.1: `check_launch.sh` greps for docks + `reply_to`.
- Task 3 of M6.1: README / NOSTR one-liners. Embed UI.
- Verify: `./configure && make && make test`.
- Commit: `Milestone 6.1: tests and docs for oauth, replies, rail docks`

#### M6.2 PR + merge + cleanup

- Task 1 of M6.2: push, `gh pr create --base main --head gb/oauth-mention-rail`.
- Task 2 of M6.2: `gh pr merge --auto --merge`. Wait MERGED.
- Task 3 of M6.2: pull main, remove worktree, delete branch.
- Verify: `git worktree list` has only main; `git status` clean.

## 4. Audit (frozen)

- Every implementation Milestone names files, commands, and a verify.
- Phase 1 research gate is this commit.
- Worktree lifecycle is the Prime Directive (PR, not local merge).
- `/trouble` gate passed before C edits (this file + RESEARCH).

## 5–7. Execute → Audit → Confirm

Commit after every Milestone. State “Grok Build complete.” only when the
PR is merged, the worktree is gone, and main is clean.
