# Plan: Robot Thread Bugs

## Phase 0 - Environment & Isolation Setup
- **Milestone 0.1: Setup Worktree**
  - Task 1: Create worktree `gb/fix-robot-thread` and move into it. (Completed)

## Phase 1 - Research & Discovery
- **Milestone 1.1: Analyze Intro Bug**
  - Task 1: Inspect `hush_agent.c` for `hush_agent_intro_seen`.
  - Task 2: Compare with `UI_SPEC.md` requirements.
- **Milestone 1.2: Analyze Group Mention & Task Bug**
  - Task 1: Trace `hush_agent_follow_kick` and `hush_agent_handle_mention`.
  - Task 2: Inspect `hush_agent_walk_thread` attribution logic.
- **Milestone 1.3: Synthesize Research**
  - Task 1: Write `docs/research/RESEARCH_ROBOT_THREAD_BUGS.md` and this plan.
  - Task 2: Commit Phase 1.

## Phase 2 - Implementation: Fix Intro Scoping
- **Milestone 2.1: Make Intros Once Per Session**
  - Task 1 of Milestone 2.1: Modify `hush_agent_intro_seen` in `hush_agent.c` to ignore the `root` argument and only check if `hex` exists in `g_intro_hex`.
  - Task 2 of Milestone 2.1: Modify `hush_agent_intro_remember` to no longer store `root` in `g_intro_root` (and optionally remove `g_intro_root` entirely).
  - Task 3 of Milestone 2.1: Recompile using `make`.
  - Task 4 of Milestone 2.1: `git add . && git commit -m "Milestone 2.1: Scope robot intro to session instead of thread root"`

## Phase 3 - Implementation: Fix Thread Attribution
- **Milestone 3.1: Correct Attribution in Thread Walk**
  - Task 1 of Milestone 3.1: In `hush_agent.c` `hush_agent_walk_thread()`, instead of blindly assigning non-human notes to `walk->robot`, use `hush_agent_lookup_robot` to find the actual name of the robot that authored the note.
  - Task 2 of Milestone 3.1: Recompile using `make`.
  - Task 3 of Milestone 3.1: `git add . && git commit -m "Milestone 3.1: Attribute thread notes to correct robot author"`

## Phase 4 - Implementation: Fix Group Handoff Prompting
- **Milestone 4.1: Inject Context into Handoffs**
  - Task 1 of Milestone 4.1: In `hush_agent_follow_push`, we currently copy the original `ev->content` into `slot->ask`. Instead of only relying on the original ask, we should modify the base prompt `HUSH_AGENT_PROMPT_FALLBACK` or `HUSH_AGENT_THREAD_HEAD` to clarify: "You are part of a robot group. Review the thread so far. Only perform the parts of the human ask that have not yet been fulfilled."
  - Task 2 of Milestone 4.1: Wait, actually, the user says "Happy was supposed to generate a riddle and Major was supposed to answer". If they both see the same ask and the same thread, Major will see Happy generated a riddle. If Major's prompt explicitly says "Fulfill the last human line in this note", and the last human line is the original ask, Major might still think it needs to generate a riddle. Let's update `HUSH_AGENT_THREAD_HEAD` to clarify role continuity.
  - Task 3 of Milestone 4.1: `git add . && git commit -m "Milestone 4.1: Refine prompts for group task handoffs"`

## Final Phase - Verification & Polish
- **Milestone 5.1: Test & Cleanup**
  - Task 1 of Milestone 5.1: Run `make test`.
  - Task 2 of Milestone 5.1: Push worktree and prepare for PR merge.
