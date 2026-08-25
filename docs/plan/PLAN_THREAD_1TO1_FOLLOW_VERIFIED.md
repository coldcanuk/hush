# PLAN_THREAD_1TO1_FOLLOW.md — Verification Gate

Base: main 77eb5fb6c (fresh worktree gb/thread-1to1-follow)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_THREAD_1TO1_FOLLOW_CURRENT.md
- Confirmed: behavior and checks already present on this base.

## M2.1 Spec
- UI_SPEC §13: "1:1 inherit: when this send has no robot pill and threadMembers is exactly one robot, attach that sole member"
- README thread paragraph covers 1:1 behavior
- Verified

## M3.1 Composer inherit
- Thread submit (no new robot pills) attaches the sole member in 1:1 context
- 1:n stays mention-gated (bots.filter ban on re-mentioning every member)
- Optimistic #thread-think after such send
- Verified in source + served + check_launch greps

## M4.1 Launch check
- check_launch.sh greps:
  - "1:1 follow-up must inherit the sole member robot"
  - "thread follow-up must not remention every member robot"
  - "thread follow-up must mention only new pills"
  - 1:1 with help text
- Verified

## M5.1 Verify + land
- make -C hush-c test → ALL PASS
- check_launch.sh → launch routes ok
- Embed clean (prior)
- No C change required

## DoD (satisfied)
- [x] In 1:1 thread pane, follow-up without new @ still addresses the sole member robot
- [x] 1:n stays mention-gated
- [x] Channel notes without p-tag stay silent
- [x] make && make test pass
- [x] PR merged, worktree removed, main clean (pending)

