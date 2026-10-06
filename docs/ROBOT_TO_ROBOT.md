# Robot-to-robot mention standard

Jobs receive this as `HUSH_AGENT_PEER_STANDARD` in prompt and rules
(`hush-c/src/hush_agent.c`).

1. **Do not copy the ask.** Write only your assignment. Do not repeat
   the human's mention list or quote the original note. Never write
   npub keys; the relay rewrites `@Name` to NIP-27 on the wire and
   strips leftover keys. The relay also **humanizes the robot's input**:
   the thread transcript and assignment are rewritten from
   `nostr:npub1…` to `@Name` (and the robot's own token is dropped)
   before they reach the model, so a model never sees a raw key to copy.
2. **One intro.** The first time a robot joins a thread it may send one
   short on-deck line. After that, ack with the hive emoji gradient and
   do the work.
3. **Handoff is optional.** A non-last robot may add `your turn, @Name`
   after the work. The last robot stops after its assignment — no
   handoff, no peer mention. The relay queues the next robot.
4. **Co-mention.** Each robot does only its own part. The relay scrubs
   self-mentions, echoed asks, and last-robot handoffs before store.

## Coordination modes (how the relay dispatches a human note)

The relay classifies a human note over N tagged robots and chooses one
mode.

| N | Human intent | Mode |
|---|--------------|------|
| 1 | any | **solo** — the robot does the whole ask |
| N | each robot has its own clause (`@A do X. @B do Y`) | **explicit** — each robot receives only its own clause |
| 2 | undirected broadcast (`@A @B plan it`) | **cooperate** — pair divides labor, no leader |
| 3+ | undirected broadcast | **orchestrate** — elect a leader, then plan the division of labor |

Detection is a deterministic fast-path only for the clearly-explicit case
(every tagged robot followed by a substantive clause). Everything else is
handled by an LLM: a pair cooperates, and three-or-more robots elect a leader.

### Leader election (3+)

1. `Major` (Payne) leads when present — no election.
2. Otherwise the relay narrows to **leadership-skilled** robots
   (`system:hive-patterns`, `system:conflict-break`, `system:canvas-coach`,
   `system:summary-handoff`, `system:job-cap`). If exactly one, that robot
   leads.
3. Otherwise the **robots determine and elect**: a single one-shot LLM
   election pass lists the candidates (name + skill count) and returns the
   chosen leader. The elected leader then plans.

### Leader plan (3+)

The elected leader emits a fenced plan. Each task line carries an integer
wave prefix; tasks sharing a wave run in **parallel**, and waves run in
order (`fifo`) or reverse (`lifo`/`filo` → `lifo`, `lilo` → `fifo`).

  ````
  ```plan
  order: fifo
  1 Happy: generate a riddle
  2 Major: answer it
  2 Scout: verify it
  3 Builder: write a summary
  ```
  ````

The relay parses this block and dispatches each non-leader robot its own
sub-task. Wave 2 above runs Major and Scout in parallel; waves 1, 2, 3 run
in sequence. A worker missing from the plan still runs (full ask) as its own
trailing wave, so nobody is silently dropped.

### Explicit delegation detection

Count `nostr:<npub>` tokens that are each followed by a substantive clause
(4+ characters, so connectors like "and" are ignored). Every robot having its
own clause is **explicit** (strict per-robot scoping). Anything less certain
goes through the LLM (cooperate or leader).

### Two-robot loop (#280)

A human note that mentions exactly two robots may become a loop
("take turns until one of you is stumped"). Hush drives every turn;
robots never chain by mentioning each other, and `robot_hops` stays `0`.

1. **Lead.** The first robot mentioned leads. It sees the whole human
   note in its prompt and decides: a final line `LOOP: continue` keeps
   the exchange going, `LOOP: stop <reason>` ends it. No line, or any
   other `LOOP:` text, means stop. The tag is case-insensitive and may
   follow blanks or markdown marks (`**LOOP: continue**`, `` `loop:
   Continue.` ``, `- LOOP: stop`).
2. **Partner.** The second robot keeps only its own clause (strict
   scope, as in explicit mode). A partner's `LOOP:` line is ignored.
3. **Strip.** On loop turns the relay removes every `LOOP:` line before
   store. Ordinary replies outside a loop are not stripped.
4. **Turns.** After the queued pass, the relay alternates partner and
   lead, each turn through the normal dispatch path. The partner is
   asked to reply to the lead's last note; the lead to the partner's.
   That note is quoted as text, not as instructions, and its own
   double quotes become single quotes, so a peer cannot steer the next
   ask beyond what it said. The existing `Thread brief:` context line
   still shows recent notes as written; the cap and the owner-only Yes
   bound any steering through it.
5. **Cap.** Loop turns count toward the channel `max_robot_turns`
   (default 4) on the thread's loop record as well as through the usual
   note scan, so a reply that opens with "I heard:", "Holding." or a
   loop line still uses a turn. At the cap the chaperon asks "Continue
   this loop? Reply Yes or No in this thread." A typed "Yes" resumes the
   stopped turn; "No" posts "Loop stopped.". After four Yes answers the
   next cap posts "Loop limit reached. Ask again to start a new loop."
   and the loop ends.
   Only the hive owner can answer; a robot's or another human's Yes is
   ignored.
6. **Owner interrupt.** Any other note from the hive owner in the thread
   ends the loop, even mid-turn. Notes from other humans do not.
7. **Restart.** Loop state lives in relay memory only. A restart drops
   it, and a Yes at a prompt from before the restart is ignored.

### Approval setting (#279)

1. **Setting.** Settings → **Robot turns**: `Auto-approve` (default; robots
   run at once, as before) or `Approve every action`. It belongs to the
   logged-in owner's profile, is saved in `vibe.json` next to the theme as
   `approval_mode` (`auto_approve` | `approve_every_action`), and is read
   back on every relay start. A missing or unknown stored value reads as
   `auto_approve`. Any `POST /api/profile` body that names `approval_mode`
   sets only the approval setting and never touches the profile names.
   A value other than those two ids (including `""`, `null`, a number, or
   a spaced `"approval_mode": "…"`, which the relay's flat parser does not
   read) is refused (400, "approval_mode must be auto_approve or
   approve_every_action.") and changes nothing. The Settings radio shows
   the saved value on page load, on every 1 s session refresh, and after
   a post (a refused post snaps it back).
2. **One approval per robot turn.** Under `Approve every action`, every
   robot turn stops at one gate in `hush_agent_begin_work`, after the
   turn-cap check and before the robot's runtime starts. (The existing
   mention greetings, "Mention received." and "At ease. I am on deck…",
   still appear first; see #278.) That covers the first turn after a mention, each
   follow wave, each two-robot loop turn, the leader election pass, the
   leader's plan pass, and an owner reply that goes to the thread's robot.
   The chaperon posts "Approval needed: <Robot> wants to take a turn. Reply
   Yes or No in this thread."; for the election pass it posts "Approval
   needed: <Robot> wants to run the leader election. Reply Yes or No in
   this thread." The plan pass claims its own wake slot (trigger
   sha256("hush-plan-pass:" + root)), so approving it runs it even when
   the convener that ran the election is also the elected leader.
   Owner-initiated tools are not robot turns and are not gated: canvas
   fill-in (`POST /api/complete`) and fixup (`POST /api/fixup`), both
   behind the session token.
3. **Answer.** Only the hive owner answers, by typing Yes or No in the
   thread (the same parse as the loop prompt). Yes runs that turn exactly
   once through the normal path, with the ask, prompt and loop note it was
   held with. No posts "Turn declined: <Robot> stood down." and starts
   nothing; it also stops a live loop in that thread, and robots queued
   behind the declined turn do not start on their own. A Yes or No from a
   robot or another human is ignored. With several turns waiting in one
   thread, each Yes or No settles the oldest; it never answers a turn
   waiting in another thread. An answer settles a waiting turn before a
   paused loop's "Continue this loop?" question in the same thread.
4. **Void.** Any other note from the owner in that thread drops every turn
   still waiting there, and only there (silently). The note itself is
   handled as usual, so it may raise its own approval line. Turns already
   running are not affected; a follow wave behind a running turn still
   starts when it finishes.
5. **Loop and cap.** A waiting loop turn does not run, and the loop stays
   paused until the owner answers. Approval lines are not robot turns, so
   the cap counts exactly as before; at the cap the loop still asks
   "Continue this loop? Reply Yes or No in this thread.", and a Yes there
   resumes the loop, whose next turn then asks for approval as usual. Each
   owner Yes is an owner note, so like any owner note it restarts the
   channel's robot-turn count; outside a two-robot loop the channel cap
   therefore does not stop an approved chain (every turn in it was
   approved one by one). Approval lines are never shown to a robot as
   thread context.
6. **Limits and restart.** At most 8 turns wait at once, and at most 4 of
   them may be asked for by anyone other than the hive owner (another
   person's mention, or a robot turn in a thread that person opened), so
   the owner always keeps 4 for their own turns. One more gets "Too many
   turns are waiting for approval. Answer one first." and does not run.
   Waiting turns live in relay memory only: after a restart the old
   waiting turn is gone, and a Yes at its approval line is an ordinary
   owner note, so it raises a fresh approval line for the thread's robot
   (possibly a different robot) and runs nothing until that is answered.
   Approving a turn lets the robot's runtime use its usual tools for that
   turn (no per-tool approval).
