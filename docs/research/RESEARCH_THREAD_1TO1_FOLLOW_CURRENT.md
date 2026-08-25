# RESEARCH — 1:1 thread follow-up inherit (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/thread-1to1-follow`
Branch: `gb/thread-1to1-follow`
Base: `main` `77eb5fb6c` (post #72 thread-think-hygiene)

## Base State

PLAN_THREAD_1TO1_FOLLOW.md is a small verification/hygiene slice. The core behavior is already present:

**1:1 inherit rule:**
- In thread pane (1:1), follow-up send without new robot pills attaches the sole member robot.
- 1:n stays mention-gated (bots.filter ban on re-mention every member).
- Channel notes without p-tag stay silent (no auto-inherit outside thread).

**Evidence in code (already on base):**
- check_launch.sh asserts:
  - "1:1 follow-up must inherit the sole member robot"
  - "thread follow-up must not remention every member robot"
  - "thread follow-up must mention only new pills"
- UI_SPEC §13: "1:1 inherit: when this send has no robot pill and threadMembers is exactly one robot, attach that sole member"
- README mentions 1:1 thread behavior.
- Thread composer (#thread-send / thread submit) + optimistic localThink already wired from prior thread slices.
- No C change required (pure UI/JS dispatch rule for thread pane).

**Tests:**
- make -C hush-c test → ALL PASS
- check_launch.sh passes (includes the inherit greps)

## Verification performed (this worktree)

- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh (passes with required 1:1 inherit greps)
- rg for UI_SPEC 1:1 inherit language
- rg for check_launch 1:1/sole/inherit asserts
- Source inspection of thread send path (already attaches sole when no pills in 1:1 context)
- README thread paragraph updated in prior

## Conclusion

All items in the plan are satisfied on this base with no code changes.
This is a verification + hygiene close-out slice (consistent with rail-prov, canvas-fim, provider-*, oauth-*, thread-ux family slices).

Proceed to VERIFIED.md, M1.1 + verification commits on gb/*, full PR lifecycle, cleanup per PRIME_DIRECTIVE.
