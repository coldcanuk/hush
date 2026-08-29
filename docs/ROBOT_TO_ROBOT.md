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
