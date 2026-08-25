# PLAN_PILLS_PROMPT_INJECTION.md — Verification Gate

Base: main ce415223d (fresh worktree gb/pills-prompt-injection)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_PILLS_PROMPT_INJECTION_CURRENT.md
- Confirmed: all items already present on this base.

## Goal (satisfied)
- Channels have "about" (topic).
- When a robot is mentioned on the channel, its system prompt receives " Channel topic: <about>".
- Pills/topics become quick LLM system-prompt hints (backend ready).

## Evidence
- launch.h: about[HUSH_LAUNCH_ABOUT_MAX] in channel_t, set_channel_about, channel_about accessor
- launch.c: put/take for channel_about_N, emit "about" in session JSON for channels
- agent.c: fill_job appends " Channel topic: ..." when channel about present
- Hygiene: empty about does nothing
- make -C hush-c test → ALL PASS
- Session JSON shows "about" when set

## Lifecycle
- Worktree + PR only
- No UI change required for this atomic backend slice

