# RESEARCH — Channel Topics/Pills → System Prompt Injection (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/pills-prompt-injection`
Branch: `gb/pills-prompt-injection`
Base: `main` `ce415223d` (post #76 payne-provider-edit)

## Base State

PLAN_PILLS_PROMPT_INJECTION.md is a small atomic backend slice. The implementation is already present on this base from prior pills/rail/channel/agent work:

**Channel about/topic:**
- `hush_launch_channel_t` has `about[HUSH_LAUNCH_ABOUT_MAX]`
- `hush_launch_set_channel_about`
- Persistence: put/take for channel_about_N in vibe JSON
- Session JSON emits `"about"` for channels when set
- Public accessor: `hush_launch_channel_about(launch, slug)` returns the string or ""

**Prompt injection:**
- In `hush_agent_fill_job` (after building base prompt), if channel has about, appends " Channel topic: <about>"
- Used when a robot is mentioned on that channel
- No change when about is empty (hygiene preserved)

**Verification evidence already in place:**
- make -C hush-c test → ALL PASS
- Session JSON contains "about" when set
- Accessor used by agent job fill

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- Explicit greps:
  - launch.h: about field, set_channel_about, channel_about accessor
  - launch.c: put/take for channel_about, emit "about" in channel JSON
  - agent.c: injection logic in fill_job (" Channel topic:")
  - Session output shows about when present
- No UI change required for this atomic backend slice
- Hygiene: empty about does nothing

## Differences from original PLAN base

- Current base is later. Channel about field, persistence, accessor, and prompt injection were implemented in earlier channel/pills/rail/agent slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern) to close PLAN_PILLS_PROMPT_INJECTION.md per user directive.

## Conclusion

Implementation satisfies the goal. No code changes needed.
Proceed to VERIFIED.md + commits on gb/* + full PR lifecycle.

## Commands executed
- git worktree add -b gb/pills-prompt-injection from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- rg/grep for channel about, set_channel_about, channel_about, "Channel topic"
- Source + session JSON + agent fill inspection
