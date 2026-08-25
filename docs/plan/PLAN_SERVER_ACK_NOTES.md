# PLAN: Server Ack Notes (M5)

## Problem (from /trouble + prior slices)
- Acks (M1) are purely client-rendered: `paintNote` + `mentionedRobots(ev)` builds emoji pills (👍 🎯 🏆) from `ev.mentions[]` (p-tags) or content fallback and appends a `.robot-acks` bar.
- No real event/note is emitted by the server at the moment a mention is *accepted for dispatch*.
- Denies and recaps already emit via `hush_intel_post_line` (with T=hush-confirm).
- On-deck emits a note via `hush_agent_on_deck`.
- For successful mention -> job path, nothing is stored until the grok reply (or on_deck fallback).
- Result: acks are not durable/queryable server events; "receipt" is optimistic UI only. No server ack note in /api/events for the positive dispatch.

Goal: When a robot receives (and accepts) a mention, the server emits a real kind-1 note (ack note) authored by the robot. This makes receipt a first-class stored event, visible in chat, and provides a persistent seam before the full reply.

## Scope (KISS, atomic for this worktree)
- On successful mention dispatch (after `policy_blocks` passes in intel), emit a short ack note from the robot pubkey.
- Reuse/extend existing post-line mechanism (h, e, optional t-confirm to avoid re-dispatch loops).
- Ack note content: minimal visible receipt, e.g. "👍" or "Mention received." so it appears in thread as authored by the robot.
- No change to client ack bar (still renders on root for "Discord reaction" visibility on the human note).
- Ack note appears as a child (reply_to) in the thread, providing server-sourced receipt.
- Keep hygiene, no new tags unless minimal.
- No UI rewrite; no new kind; no full reaction (kind 7) model.
- Extend check to assert the ack note is present in events after a mention.
- Later milestones can evolve (e.g. suppress thin ack notes in UI, use for "acks first" ordering, or style distinctly).

## Evidence (verbatim from current main after M1/M3/M4)
- `hush_intel_consider` walks p-tags -> `hush_intel_handle_robot`.
- `hush_intel_policy_blocks` may call `post_line` for denies; on success path proceeds to hold/release/agent_consider with no immediate post for positive ack.
- `hush_intel_post_line`: sets pubkey=robot, h/e/t=confirm, store_insert. consider skips T=confirm events.
- `hush_agent_on_deck` and `post_recap` already create real notes for edge paths.
- `hush_http_serve_events` + M1.3/M1.4: emits "mentions"[] from p-tags for any stored event.
- `paintNote`: always appends ack bar derived from mentionedRobots for any ev that has mentions (even own notes).
- No call site currently emits a positive "received" note for the happy path to grok.

## Milestones / Tasks (gb/server-ack-notes)
M5.1: Research + this plan (identify injection: handle_robot after !policy_blocks).
M5.2: Add or extend emission helper (e.g. `hush_intel_post_ack` or reuse `post_line` with positive text). Call immediately on accepted mention.
M5.3: Ensure ack note carries correct threading (h channel, e root) and pubkey=robot; use T=confirm to prevent re-trigger.
M5.4: Keep client bar as-is (visual receipt on root); the new note provides durable server ack in the log/thread.
M5.5: Update/extend `hush-c/tests/check_agent.sh` with assertion: after posting a mention, /api/events contains a note with the robot pubkey as author and ack-like content (or T confirm from that robot).
M5.6: make clean, make, make test (ALL PASS). Run check_agent.sh explicitly.
M5.7: Regenerate UI embed only if demo changed (none planned). Run full critic + skeptic with verbatim evidence + unanimous gate.
M5.8: Commit (per atomic), push, gh pr create --base main --head gb/server-ack-notes, gh pr merge --auto --merge.
M5.9: After merge confirmed on main: cd /opt/repo/hush, git pull --ff-only, git worktree remove worktrees/server-ack-notes, branch delete local+remote.
M5.10: (If more atomic items remain in overall plan) repeat lifecycle for next worktree. Do not write main directly.

## Not in scope (future atomic / other worktrees)
- Full group negotiation state machine or leader election (M3 seam exists for p-tagging).
- Channel about full UI editor (M4 backend injection done).
- Inventory/Diablo robot selector, Tool Rail iteration, Discord-reactive main UI polish.
- Suppressing thin ack notes or re-styling acks vs replies.
- Per-channel ack policy or ack content templates.
- Changing ack to kind 7 reactions.
- Optimistic client bar removal (keep for immediate visual).

## Verification
- make test: ALL TESTS PASSED (multiple invocations).
- sh hush-c/tests/check_agent.sh : passes + new ack-note assertion.
- Manual/ curl proof: POST mention for a robot; GET /api/events; assert a stored event exists with pubkey==robot_hex, reply_to or e==root, content containing ack text or T=confirm authored by robot.
- UI still renders the emoji bar on the human note (client path unchanged).
- No loops (confirm tag prevents re-dispatch).
- Critic + Skeptic re-engage after implementation with cross-exam + Bayesian + unanimous gate.
- Full PRIME_DIRECTIVE lifecycle executed (worktree only, PR, auto-merge, cleanup).

## Files
- docs/plan/PLAN_SERVER_ACK_NOTES.md (this)
- hush-c/src/hush_intel.c (emission call + optional helper)
- hush-c/include/hush_intel.h (if new decl)
- hush-c/tests/check_agent.sh (assertion)
- (no UI change expected; embed not required)

## RDAP + Discipline
- Research first (paths in intel/agent/http/UI).
- Atomic milestones, commit per M on the branch.
- legible-c for all .c/.h edits.
- After each slice: rebuild, test, check, critic/skeptic.
- No main writes. PR only to land.
- After this worktree lands and is cleaned, continue with remaining plan items via new worktrees if directed.

Per AGENTS.md, PRIME_DIRECTIVE.md, write-legible-c, RDAP.