# PLAN: Mention pills, tool rail, voice icons, exit reap (RDAP)

Branch: `gb/pills-rail-voice-exit`
Worktree: `worktrees/pills-rail-voice-exit`
Base: `main` `b1147e000`

## 1. Methodology

RDAP — Double Diamond + Spiral + atomic Milestones. Commit after every
Milestone on this branch. Land only via PR.

## 2. Scope

See [`../research/RESEARCH_PILLS_RAIL_VOICE_EXIT.md`](../research/RESEARCH_PILLS_RAIL_VOICE_EXIT.md). Primary goal, non-goals, DoD,
constraints, assumptions, and risks are locked there.

**Primary Goal**

Pills for mentions and Manage Channel; compact sidebar glyphs; a
movable collapsible tool rail for Install/Profile/Settings/Call/Close/
Exit with Install help; Whisper-gated 1:1 and channel voice with mute;
Exit reaps every process Hush started.

**Non-Goals**

Live LLM replies; in-process Whisper engine; collapsing Close into
Exit; tray; remote quit.

**Success Criteria**

Research DoD items 1–9.

## 3. Phases → Milestones → Tasks

### Phase 0 — Isolation (COMPLETE)

- [x] Task 1 of M0.1: clean main `b1147e000`, worktree
      `/opt/repo/hush/worktrees/pills-rail-voice-exit` on
      `gb/pills-rail-voice-exit`.
- Verify: `pwd | grep hush/worktrees` and `git branch --show-current`.

### Phase 1 — Research (GATE, this commit)

- [x] Task 1 of M1.1: inspect UI, whisper, fork/cleanup, live leftovers.
- [x] Task 2 of M1.1: four-minds protocol on Exit leftovers.
- [x] Task 3 of M1.1: write `RESEARCH_PILLS_RAIL_VOICE_EXIT.md` and
      this plan.
- Verify: files exist; scope + Bayes + architecture locked.
- Commit: `Milestone 1.1: research + frozen plan for pills, rail, voice, exit`

### Phase 2 — Architecture

#### M2.1 UI_SPEC contracts

- Task 1 of M2.1: add UI_SPEC §15 tool rail, §16 mention/manage pills,
  §17 whisper-gated call/voice + mute, §18 exit reap. Update header
  Hick: brand + badge stay; actions move to `#tool-rail`.
- Verify: `rg -n "tool-rail|composer-pill|chan-voice|hush_child_track" UI_SPEC.md`.
- Commit: `Milestone 2.1: UI_SPEC for rail, pills, voice, exit reap`

### Phase 3 — Exit reap (C, /trouble fix)

#### M3.1 Track and reap children

Files: `hush-c/include/hush_relay.h`, `hush-c/src/hush_relay.c`,
`hush-c/src/hush_provider.c`.

- Task 1 of M3.1: `hush_child_track` / `hush_relay_reap_children` in
  relay. Cap 8 pids. Cleanup calls reap. `hush_open_app_window` tracks.
- Task 2 of M3.1: `hush_provider_spawn_login` tracks the login pid
  (declare a small public `hush_relay_track_child(pid_t)`).
- Task 3 of M3.1: Linux `/proc` sweep for leftover
  `--class=hush-relay` + this port. Attach message names `--quit`.
- Verify: `make -C hush-c` compiles. write-legible-c §14 on the new
  helpers.
- Commit: `Milestone 3.1: Exit reaps forked browser and login children`

### Phase 4 — UI chrome

#### M4.1 Compact glyphs + Manage Channel pills + mention pills

File: `hush-c/demo/index.html` (then embed).

- Task 1 of M4.1: shrink `.chan-del` and `.robot-card .toggle` to 24px.
- Task 2 of M4.1: Manage Channel `+`/`−` pills (no checkboxes).
- Task 3 of M4.1: composer mention pills; `#msg` holds leftover text;
  submit serializes pills to `nostr:<npub>`.
- Verify: visual classes present; `prettyMentions` still used on notes.
- Commit: `Milestone 4.1: mention and manage pills; compact sidebar glyphs`

#### M4.2 Tool rail

- Task 1 of M4.2: `#tool-rail` with hamburger, grip, drag, collapse,
  `localStorage.hush-rail`.
- Task 2 of M4.2: move Install/Profile/Settings/Call/Close/Exit into
  the rail. `#install-help` copy locked in research.
- Verify: header no longer hosts those buttons; greps for `tool-rail`
  and `install-help`.
- Commit: `Milestone 4.2: movable collapsible tool rail`

#### M4.3 Whisper-gated voice

- Task 1 of M4.3: cache `status.whisper`; robot Call icon; channel
  Voice icon.
- Task 2 of M4.3: 1:1 opens stage for that robot; channel voice invites
  roster robots; tile Mute.
- Verify: icons absent in HTML static tree (created in JS); JS contains
  `robot-call`, `chan-voice`, `tile-mute`.
- Commit: `Milestone 4.3: Whisper-gated robot call and channel voice`

### Phase 5 — Tests, docs, land

#### M5.1 Tests + docs

- Task 1 of M5.1: extend `check_launch.sh` greps.
- Task 2 of M5.1: extend `check_exit.sh` so a fake tracked child (or
  `--open` if display) is gone after `/api/exit`.
- Task 3 of M5.1: README + NOSTR one-liners (pills are UI; wire is
  NIP-27). Embed UI.
- Verify: `./configure && make && make test`.
- Commit: `Milestone 5.1: tests and docs for pills, rail, voice, exit`

#### M5.2 PR + merge + cleanup

- Task 1 of M5.2: push, `gh pr create --base main --head gb/pills-rail-voice-exit`.
- Task 2 of M5.2: `gh pr merge --auto --merge`. Wait MERGED.
- Task 3 of M5.2: pull main, `git worktree remove`, delete branch.
- Verify: `git worktree list` has only main; `git status` clean.
- Commit: none on main.

## 4. Audit (frozen)

- Every implementation Milestone names files, commands, and a verify.
- Phase 1 research gate is this commit.
- Worktree lifecycle is the Prime Directive (PR, not local merge).
- `/trouble` four-minds gate passed for the exit reap before C edits
  (recorded in RESEARCH).

## 5–7. Execute → Audit → Confirm

Commit after every Milestone. State “Grok Build complete.” only when
the PR is merged, the worktree is gone, and main is clean.
