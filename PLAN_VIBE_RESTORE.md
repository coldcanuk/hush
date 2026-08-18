# PLAN: Persist vibe + home config; honest provider auth

Frozen after Phase 1 synthesis (2026-08-18). Execute only this file.
Worktree: `/opt/repo/hush/worktrees/vibe-restore-robot-auth`
Branch: `gb/vibe-restore-robot-auth`
Base: main `29b5f923a`

## 1. Methodology

RDAP. Issue 2 also ran `/trouble` (TOOLED). Research → this gate →
architecture → implement → PR. Land on `main` only via Pull Request.

## 2. Scope

### Primary Goal

1. Recover the last vibe after process death, Exit, or
   `make clean && make && make install`. Identity comes back from
   `pass` or nsec import; vibe comes back from `~/.config/hush/vibe.json`.
2. Keep provider-auth facts honest on the Raise-robot configure drawer:
   Grok/Codex OAuth-or-key via their CLIs, Goose via `goose configure`,
   Cline via ClinePass / BYOK (not OAuth-first), APIs via keys.

### Non-Goals

- Chat / event-ring persistence.
- Multi-vibe.
- OAuth browser from C.
- Writing foreign homes (`~/.config/goose`, `~/.grok`, `~/.codex`, Cline).
- Changing `make clean` semantics.
- Putting nsecs or provider secrets in `vibe.json`.
- Email on disk (session-only).

### Success Criteria / DoD

1. Create vibe writes `$XDG_CONFIG_HOME/hush/vibe.json` else
   `$HOME/.config/hush/vibe.json` (0600). Tests use `HUSH_CONFIG_DIR`.
2. Cold boot + identity restore + existing file → `has_vibe=1`,
   `ready=true`. UI skips "Name your vibe".
3. Import of nsec on a process that already has the file → same.
4. File has no `nsec`. Session after ack still `"nsec":""`.
5. `make clean` still only deletes build products.
6. Cline drawer does not claim OAuth. Grok/Codex keep `grok login` /
   `codex login`. Goose keeps `goose configure`.
7. `./configure && make && make test` pass. Embed if HTML changes.
8. PR merged, worktree removed, main clean.

### Constraints

C11 + write-legible-c §14. Worktree/PR law. String-field JSON only.
Reuse existing `hush_provider_config_dir` path rule.

### Assumptions

This process is the vibe. Disk vibe is host-local, not keyed to npub.
Logout keeps vibe (already true in RAM); disk matches.

### Environment

gcc, make, `./configure`, `make test`, curl, `pass` / fake-pass.

### Top Risks

1. File size / prompt length → cap to existing roster fields.
2. Different human imports onto same host vibe → allowed (relay = vibe).
3. Two processes → existing single-port rule; write tmp+rename.
4. Test pollution of real `~/.config/hush` → `HUSH_CONFIG_DIR` required
   in unit + smoke tests.

## 3. Plan

### Phase 0 — Isolation (done)

- [x] Task 1 of M0.1: worktree `worktrees/vibe-restore-robot-auth`
      on `gb/vibe-restore-robot-auth` from clean main `29b5f923a`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/vibe-restore-robot-auth`.

### Phase 1 — Research (this commit)

- [x] M1.1 Evidence: launch restore, store, make clean, UI gate.
- [x] M1.2 Provider auth: grok/codex/goose CLIs + Cline docs.
- [x] M1.3 Synthesis: append RESEARCH.md; write this PLAN.
- Commit: `Milestone 1.3: freeze vibe-restore research and plan`

### Phase 2 — Architecture

#### M2.1 Lock UI_SPEC + home-dir contract

- Task 1 of M2.1: UI_SPEC §3 Resume: restored vibe from
  `~/.config/hush/vibe.json`. §7: file survives rebuild.
- Task 2 of M2.1: UI_SPEC §11 Cline row: ClinePass / BYOK, not OAuth.
- Task 3 of M2.1: Caps table: vibe overlay path + `HUSH_CONFIG_DIR`.
- Verify: `rg -n "vibe.json|ClinePass|HUSH_CONFIG_DIR" UI_SPEC.md`
- Commit: `Milestone 2.1: lock vibe persist and Cline auth contract`

#### M2.2 Module boundary

- Task 1 of M2.2: `hush_launch` owns save/load. Path helper lives in
  launch (copy of provider dir rule + `HUSH_CONFIG_DIR`). Extract
  shared `hush_home` only if a later milestone needs a third caller.
- Task 2 of M2.2: Public API:
  `hush_launch_save_vibe`, `hush_launch_restore_vibe`.
  Relay calls restore after identity. Mutators call save.
- Verify: this PLAN still names one owner.
- Commit: `Milestone 2.2: lock launch persist boundary`

### Phase 3 — Implementation

#### M3.1 Save / load vibe.json (unit-tested)

- Task 1 of M3.1: implement save/load in `hush_launch.c` + header.
- Task 2 of M3.1: `test_launch.c` uses `HUSH_CONFIG_DIR=/tmp/hush-launch-cfg-<pid>`,
  create vibe, `hush_launch_init`, restore identity+vibe, expect
  `has_vibe` and name. Assert file has no `nsec`.
- Verify: `make -C hush-c tests/test_launch && ./hush-c/tests/test_launch`
- Commit: `Milestone 3.1: persist vibe.json under HUSH_CONFIG_DIR`

#### M3.2 Wire boot + mutators

- Task 1 of M3.2: `hush_relay_prepare` calls `hush_launch_restore_vibe`.
- Task 2 of M3.2: save after create vibe, visibility, channel, project,
  profile, add/remove agent, add member.
- Verify: unit test still green; `rg -n "hush_launch_restore_vibe|hush_launch_save_vibe" hush-c/src`
- Commit: `Milestone 3.2: restore vibe on boot and save on mutate`

#### M3.3 Smoke: import + restart recovers vibe

- Task 1 of M3.3: extend `check_launch.sh` with `HUSH_CONFIG_DIR` and
  a second hush-relay start that sees `has_vibe:true` after identity
  restore (or after import+ack against the leftover file).
- Verify: `sh hush-c/tests/check_launch.sh`
- Commit: `Milestone 3.3: smoke restart recovers vibe`

#### M3.4 Provider drawer honesty

- Task 1 of M3.4: Cline help mentions ClinePass / BYOK, not OAuth.
- Task 2 of M3.4: embed UI.
- Task 3 of M3.4: `check_launch.sh` greps ClinePass or "bring your own".
- Verify: `rg -n "ClinePass|bring your own" hush-c/demo/index.html`
- Commit: `Milestone 3.4: honest Cline auth copy`

#### M3.5 Docs

- Task 1 of M3.5: README + SECURITY: `~/.config/hush/vibe.json`
  survives `make clean`; secrets stay in `pass`.
- Verify: `rg -n "vibe.json" README.md SECURITY.md`
- Commit: `Milestone 3.5: document home persist`

### Phase 4 — Verify, PR, cleanup

- Task 1 of M4.1: `./configure && make clean && make && make test`
- Task 2 of M4.1: write-legible-c §14 on C diff.
- Task 3 of M4.1: push, `gh pr create`, `gh pr merge --auto --merge`.
- Task 4 of M4.1: after merge, remove worktree + `gb/*`.
- Commit before push if polish needed:
  `Complete: persist vibe in ~/.config/hush – ready for merge`

## 4. Audit of this plan

- Research → plan-update gate: Phase 1 last task (this file).
- Worktree lifecycle: Phase 0 start + Phase 4 PR (not local merge).
- Tasks are atomic and name verification.
- `/trouble` gate: implement only after four-minds unanimous yes
  (recorded in the execution session, not this file).
