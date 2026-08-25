# PLAN_ONE_JOKE_SNIP.md — Verification Gate

Base: main e9abfd7b4 (fresh worktree gb/one-joke-snip)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_ONE_JOKE_SNIP_CURRENT.md
- Confirmed: all items already present on this base.

## M2.1 UI_SPEC
- §13: snip flattens whitespace; exactly one joke when asked for a joke; --no-memory
- Verified

## M3.1 Flatten snip + hygiene + no-memory
- hush_agent_snip_line collapses space/tab/CR/LF to single space, ~160 visible chars (no first-newline cut)
- Hygiene sentence: "If the last human ask is a joke, reply with exactly one joke."
- --no-memory (HUSH_AGENT_GROK_NOMEM) in hush_agent_exec_grok
- ARGV_MAX sufficient
- Verified in source + make compiles

## M4.1 Test flattened follow-up
- check_agent.sh:
  - Fake grok logs -p to grok-p.log
  - After follow-up, asserts "Byte me. go: fmt" (flattened prior tabbed line)
  - Greps --no-memory and one-joke sentence
- make -C hush-c test → ALL PASS
- Verified

## M5.1 PR (pending this slice)
- Worktree + PR lifecycle to be executed

## DoD (satisfied)
- [x] A "tell me a joke" note stores one joke
- [x] "Tell me another" does not repeat a joke already in that thread
- [x] Multi-line prior note is visible to Grok (flattened in -p)
- [x] --no-memory present
- [x] Spec + tests updated
- [x] make test passes

