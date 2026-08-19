# PLAN — One joke, full-note snip

Methodology: **RDAP** (Research-Driven Adaptive Planning).
Research lock: `docs/research/RESEARCH_ONE_JOKE_SNIP.md`.
Worktree: `/opt/repo/hush/worktrees/one-joke-snip`
Branch: `gb/one-joke-snip`
Base: `main` `c22e4d2bb`

## Scope

**In.** Flatten `hush_agent_snip_line` so a multi-line prior note is
visible to Grok. Hygiene: exactly one joke when the last ask is a joke.
`--no-memory`. Test that the follow-up `-p` contains the flattened
second line. Spec/README one paragraph.

**Out.** `--max-turns 1`. Joke classifiers. Streaming. Content bound.
Live hive restart. GitHub unfork.

## Primary objective

A "tell me a joke" note stores one joke. "Tell me another" does not
repeat a joke already in that thread.

## Phase 0 — Isolate

### M0.1 Worktree
- [x] Task 1: `gb/one-joke-snip` at `worktrees/one-joke-snip` from
      clean `main` `c22e4d2bb`.

## Phase 1 — Research gate

### M1.1 Research
- [x] Task 1: `docs/research/RESEARCH_ONE_JOKE_SNIP.md` (this slice).
- Verify: four-minds, Bayes H1 0.579 / H2 0.350, architecture lock.
- Commit: `Milestone 1.1: research one-joke snip`

## Phase 2 — Spec

### M2.1 UI_SPEC
- [x] Task 1: `UI_SPEC.md` §13: snip flattens whitespace; one joke
      when asked for a joke; `--no-memory`.
- Verify: `rg -n 'exactly one joke|snip_line|--no-memory' UI_SPEC.md`.
- Commit: `Milestone 2.1: spec one joke and flattened snip`

## Phase 3 — Agent

### M3.1 Flatten snip + hygiene + no-memory
- [x] Task 1: `hush_agent_snip_line` collapses space/tab/CR/LF to one
      space, keeps 160 visible chars, no first-newline cut.
- [x] Task 2: hygiene + rules sentence
      `If the last human ask is a joke, reply with exactly one joke.`
- [x] Task 3: `--no-memory` in `hush_agent_exec_grok`. ARGV_MAX stays 28.
- Verify: `make -C hush-c` compiles. §14 checklist on touched C.
- Commit: `Milestone 3.1: flatten snip, one-joke hygiene, no-memory`

## Phase 4 — Tests

### M4.1 check_agent sees joke 2
- [x] Task 1: fake grok logs `-p` to `$HUSH_CONFIG_DIR/grok-p.log`.
      After follow-up, log contains flattened `Byte me. go: fmt`.
      Grep `--no-memory` and the one-joke sentence.
- Verify: `make -C hush-c test` → ALL TESTS PASSED.
- Commit: `Milestone 4.1: test flattened follow-up transcript`

## Phase 5 — Land

### M5.1 PR
- [x] Task 1: push `gb/one-joke-snip`, PR → main, auto-merge, remove
      worktree after MERGED.
- Verify: `gh pr view` MERGED; main checkout clean.

## Audit (pre-exec)

- Phases atomic. One commit per milestone.
- No main writes.
- Dual lever H1+H2 matches research; turns stay 2.
