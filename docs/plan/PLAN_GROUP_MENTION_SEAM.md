# PLAN: Group Mention Negotiation Seam (M3)

## Problem (from /trouble)
When human mentions 2+ robots in one note (e.g. "Happy tell a joke; Sgt Major Payne analyse it"), dispatch is fully independent per p-tag. No ack path originally, no coordination, no way for robots to see they are in a group or to p-mention each other back. Result: silence or duplicate/conflicting replies.

Acks (M1) now provide visual receipt. Next atomic seam: awareness + reply p-tagging so AIs can negotiate (individual, cooperate, split, or full back-and-forth).

## Scope (KISS, atomic)
- Detect co-mentions (multiple robot p-tags on the triggering note).
- Pass co-robot list into the per-robot job/prompt so the LLM knows "you + these others were mentioned; you may @ them by putting nostr:npub in content".
- When a robot job finishes, scan its output for nostr:npub... and add corresponding p-tags to the emitted note (in addition to human p-tag).
- This enables: acks for all, then robots can emit notes that trigger further mentions to peers.
- No full "choose one leader" or server-side group job yet. LLM decides via prompt + hygiene.
- No change to channel policy or UI selector (later milestones).
- Server still emits "mentions"[] for any p-tags (from M1).

## Evidence (verbatim from main after M1)
- hush_agent_consider + hush_intel_consider walk every p-tag independently and call handle_mention per robot.
- hush_agent_fill_note only ever adds "h", optional "e", and one "p" (human_pub).
- Replies from grok go through finish_job -> insert_note with only human_pub.
- UI paintNote + mentionedRobots now authoritative on ev.mentions (server p-tags).

## Milestones / Tasks (this worktree)
M3.1: Research + plan (this doc) + identify injection points (fill_job, fill_note, finish_job).
M3.2: Extend hush_agent_job_t + fill_job to capture co-robot npubs from parent p-tags (only robots, not human).
M3.3: Update fill_prompt to inject a short "Group context" line when co-robots > 0. Keep hygiene.
M3.4: In finish_job / insert path, parse job->out for nostr:npub... patterns and append p-tags for matched robots (plus the existing human p).
M3.5: Regenerate embed if UI touched (none needed), make, make test, run check_agent.sh.
M3.6: Add a small test assertion or note in check that multi-mention produces p-tags from robot reply if it echoes a nostr: (stretch).
M3.7: Commit, push, PR, auto-merge, cleanup per PRIME_DIRECTIVE.

## Not in scope (future atomic)
- Server ack notes
- Channel robot_talk / topics -> prompt injection
- Full negotiation state machine or leader election
- UI changes for "thinking together"
- Inventory selector

## Verification
- make test passes
- Manual or check script: post note with 2 robot npubs; observe both acks; observe if a robot reply contains a nostr: for the other, the stored event has the p-tag.
- Critic/Skeptic re-engage after implementation.

## Files
- hush-c/src/hush_agent.c (main)
- hush-c/include/hush_agent.h (if struct grows)
- tests/check_agent.sh (optional assertion)
- docs/plan/PLAN_GROUP_MENTION_SEAM.md

Per AGENTS.md, RDAP, legible-c, PRIME_DIRECTIVE.
