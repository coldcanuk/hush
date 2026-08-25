# RESEARCH — Conversation intelligence + channel policy (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/conv-intel-policy`
Branch: `gb/conv-intel-policy`
Base: `main` `a023200ff` (post #77 close-x-dialog)

## Base State

PLAN_CONV_INTEL_POLICY.md is a verification/hygiene slice. The implementation is already present on this base from prior intel/channel/policy work:

**Channel policy:**
- hush_launch_channel_t has policy struct (kind, robot_reply, burst_ms, etc.)
- hush_launch_policy_default, policy_copy, set_channel_policy
- Persistence in vibe.json (channel_* fields)
- Session JSON emits policy on channels[]
- Manage Channel UI (#manage-policy, radios for reply/burst, save posts robot_reply + burst_ms)

**hush_intel:**
- hush_intel.h/c present (consider, poll, init, shutdown)
- Holds for chatty bursts, cue matching, recap with t=hush-confirm
- Hops, cooldown, max_jobs respected
- Forward to hush_agent_consider when policy allows
- HTTP POST calls consider; relay pump calls poll

**Thread hygiene:**
- Follow-up posts only new pills + explicit @ (no auto re-mention every member)
- Verified in HTML/JS composer logic

**Tests:**
- make -C hush-c test → ALL PASS
- test_intel.c for silent/hold/confirm/off/hop-0
- check_launch greps for manage-policy, robot_reply, burst_ms

**Docs:**
- UI_SPEC §20 covers policy + intelligence
- README mentions Manage Channel leashes

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- Explicit greps:
  - launch.h: policy struct, defaults, copy, set
  - launch.c: persist/emit policy
  - hush_intel.c/.h: consider/poll/hold/confirm
  - HTML: manage-policy block, radios, save
  - check_launch: policy greps
- Thread follow-up mention hygiene present
- No new C required for verification gate

## Differences from original PLAN base

- Current base is later. Channel policy, hush_intel burst/confirm/leash, Manage Channel UI, thread hygiene were implemented in earlier intel/channel/robot-cards/pills slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of rail-prov, canvas-fim, provider-*, oauth-*, thread-*, onboard-*, splash-*, payne-*, vibe-*, pills-*, close-x, code-canvas, etc.) to close PLAN_CONV_INTEL_POLICY.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria item.
No code changes needed.
H4 lock (policy before dispatch, burst/confirm, t=hush-confirm, 1:n mention-gated, humans-only quiet) holds.

Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/conv-intel-policy from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- rg/grep for policy struct, hush_intel, manage-policy, robot_reply, burst_ms, thread follow-up hygiene
- Source + HTML + test inspection
