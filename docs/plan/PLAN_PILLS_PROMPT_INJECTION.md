# PLAN: Channel Topics/Pills -> System Prompt Injection (atomic)

## Goal
Channels have their own "about" (topic). When a robot is mentioned on a channel, its system prompt receives a short "Channel topic: ..." pointer so behavior is context-aware. Pills/topics in UI become quick LLM system-prompt hints.

## Scope (KISS, this worktree only)
- Add `about[HUSH_LAUNCH_ABOUT_MAX]` to `hush_launch_channel_t`.
- Wire persistence: put/take for channel_about, emit in session JSON "about".
- Public accessor: `hush_launch_channel_about(launch, slug)`.
- In `hush_agent_fill_job`: after building prompt, if channel has about, append " Channel topic: <about>".
- No UI changes required for atomic backend (pills UI already exists for agents; channel about can be set via future or direct).
- Keep hygiene. No behavior change when about is empty.

## Evidence from prior
- Channels already have name/slug/id/group + policy (robot_reply etc).
- Agent jobs look up channel for dispatch.
- Prompt fill happens in fill_job before spawn.

## Verification
- make + make test (ALL PASS).
- Session JSON contains "about" when set on channel.
- Robot prompt for a job on a channel with about includes the topic line.

## Files touched
- include/hush_launch.h
- src/hush_launch.c (format, put, take, public accessor)
- src/hush_agent.c (injection in fill_job)

## Lifecycle
- Commit on gb/pills-prompt-injection
- Push
- gh pr create --base main --head gb/pills-prompt-injection
- gh pr merge --auto --merge
- Poll until merged
- On main: pull, worktree remove, branch delete local+remote

Per PRIME_DIRECTIVE, legible-c, RDAP.
