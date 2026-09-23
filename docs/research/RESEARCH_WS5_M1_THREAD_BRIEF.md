# RESEARCH — WS5 M1: durable summarized thread state (rolling brief)

Scope: close the gap between the durable per-root transcript that already
exists and the agent prompt that mostly ignores it. One milestone only:
a stated context-budget policy, a deterministic rolling brief pinned to
the root, and wiring so root eviction / restart no longer degrades to
current-message-only while `$HUSH_HOME/threads` has data.

Tree: `main` @ `8e4887d6` (post WS4 `edcc8f93`). All paths relative to
repo root unless noted. Claims tagged OBSERVED / INFERRED / UNKNOWN.

## 1. OBSERVED baseline (verified in this tree)

- `hush-c/include/hush_thread.h` + `hush-c/src/hush_thread.c` implement
  durable per-root JSONL transcripts (`$HUSH_HOME/threads/<root>.log`,
  one `{"id","pubkey","at","content"}` object per kind-1 note) and a
  per-root `.brief` file. Caps: `HUSH_THREAD_CONTENT_MAX = 2048` per
  turn, `HUSH_THREAD_BRIEF_MAX = 2048`, `HUSH_THREAD_TURNS_MAX = 32`
  per read. Roots must be 64-char lowercase hex; files are 0600 and
  opened `O_NOFOLLOW`; republished ids dedupe on read.
- Every ingest path records: POST `/api/event` (`api_channels.c:72`),
  verified relay `EVENT` (`hush_relay.c:1213`), agent notes/replies
  (`agent_dispatch.c:346`, `hush_agent.c:521`).
- The agent prompt already consults the brief: `agent_thread.c`
  `hush_agent_fill_thread` loads it and injects one snipped
  `Thread brief:` line (`HUSH_AGENT_SNIP_MAX = 384`). Live ring window
  is `HUSH_AGENT_THREAD_MAX = 6` work notes plus the opening
  (`Conversation owner:`) line plus current message, into
  `HUSH_AGENT_NOTE_MAX = HUSH_EVENT_MAX_CONTENT * 3 + 1 = 12289`.
- WS4 (stream/cancel/budgets) is on main (`edcc8f93`). Not redone here.

## 2. OBSERVED gaps vs the M1 outcome

1. **The brief does not roll.** `hush_agent_brief_update`
   (`agent_dispatch.c:333-340`) calls `hush_thread_brief_set(root,
   answer)` on every published reply, so each answer *replaces* the
   brief. A long thread's brief remembers only the latest reply, never
   the opening. There is no `brief_roll` / roll-up anywhere
   (grep for `roll` in `hush-c/src` hits only comments).
2. **Durable turns render only on a total ring miss.**
   `hush_agent_fill_thread` (`agent_thread.c:190-192`) calls
   `hush_agent_append_durable` only when `count == 0 && !owner`.
   INFERRED consequence: a thread with 30 durable turns but 2 notes
   left in the ring renders 2 turns, never the other 28; a thread
   whose root was evicted but whose replies survive renders replies
   without the opening. Only the empty-ring case backfills.
3. **No stated context-budget policy.** The constants exist
   (`HUSH_AGENT_THREAD_MAX`, `HUSH_AGENT_SNIP_MAX`,
   `HUSH_THREAD_BRIEF_MAX`, `HUSH_THREAD_TURNS_MAX`,
   `HUSH_AGENT_NOTE_MAX`) but no comment or note states the eviction
   rule: when the window would exceed budget, what is summarized,
   what is evicted, and what order the job note is assembled in.

## 3. INFERRED design constraints

- INFERRED: an LLM summarizer is out of scope and unnecessary. The
  outcome allows a bounded extractive rollup; a flatten-and-append
  brief (oldest evicted first, newest kept) is deterministic, testable
  without fixtures, and honest about what it is.
- INFERRED: the brief injection path (`snip_line` to 384 bytes) is
  already budget-safe, so the rollup only needs to respect
  `HUSH_THREAD_BRIEF_MAX` on disk, not the prompt budget.
- INFERRED: `messages[]` multi-turn stays deferred (OUT of this PR).
  The brief + recent-turns flattened note is the M1 surface.

## 4. UNKNOWN (explicitly not resolved this M1)

- UNKNOWN: ideal per-roll snip length for provider quality. M1 picks
  a fixed, documented constant; tuning is a later milestone with
  harness evidence.
- UNKNOWN: whether robot context-file persistence or `messages[]`
  changes the budget split. Both are OUT (later WS5 milestones).

## 5. Stated context-budget policy (normative for M1)

Per root, newest wins, oldest is summarized away:

- **Live ring (preferred):** up to `HUSH_AGENT_THREAD_MAX = 6` newest
  work notes from the store, each flattened to one
  `HUSH_AGENT_SNIP_MAX = 384`-byte line. Ring notes are verbatim and
  always beat durable copies of the same id.
- **Durable transcript (fill):** `$HUSH_HOME/threads/<root>.log`
  keeps every turn (each capped at `HUSH_THREAD_CONTENT_MAX = 2048`).
  Reads return at most `HUSH_THREAD_TURNS_MAX = 32` newest, oldest
  first. `hush_agent_fill_thread` renders durable turns only for
  window slots the ring cannot fill, oldest available first, skipping
  the parent trigger, the opening id when the `Conversation owner:`
  line already shows it, and any id already rendered from the ring.
- **Rolling brief (pinned summary):** `<root>.brief` holds at most
  `HUSH_THREAD_BRIEF_MAX = 2048` bytes. Every published robot reply
  rolls one flattened snip (whitespace-collapsed, at most
  `HUSH_THREAD_ROLL_SNIP_MAX = 200` bytes) onto the brief with a
  `" | "` separator; overflow evicts from the front at separator
  boundaries (hard trim when no separator survives). Empty/blank
  answers never touch the brief.
- **Job-note assembly order:** header, `Thread brief:` line when
  non-empty, `Conversation owner:` line when the root is still in the
  ring, durable backfill, live ring turns, `Current message:` verbatim,
  then robot file context. The whole note stays within
  `HUSH_AGENT_NOTE_MAX = 12289` bytes; rendering stops (rolls back
  the partial line) at the buffer edge rather than overflowing.

## 6. Build plan (smallest coherent completion)

1. `hush_thread.h/.c`: add `HUSH_THREAD_ROLL_SNIP_MAX` and
   `hush_thread_brief_roll` (orchestrator over `brief_get` +
   flatten/snip leaf + join-evict leaf + `brief_set`).
2. `agent_dispatch.c`: `hush_agent_brief_update` rolls instead of
   replacing.
3. `agent_thread.c`: `hush_agent_fill_thread` always backfills
   durable turns into unfilled window slots (bounded by
   `HUSH_AGENT_THREAD_MAX`), with the dedupe rules from §5.
4. `tests/test_thread.c`: roll-up unit tests (append, separator,
   cap eviction, blank/invalid-root no-ops) plus a fixed-script
   restart/ring-miss test proving `hush_agent_fill_thread` still
   emits the brief and the opening substance with an empty store.
   `test_thread` already runs under `make test` via the `TEST_BINS`
   wildcard, so no Makefile change is needed.
