# Research: conversation ownership & mention humanization

Incident: the riddle thread again. Happy self-mentioned ("I have a riddle
for @Happy") and rendered its peer as a truncated key ("@npub1t337pnf").
Same bug class as PR #135/#136, but a new manifestation that the prior
output scrub could not reach.

## Why the output scrub was insufficient

PR #135/#136 scrubbed the robot **output** (drop self, echo, handoff,
npub keys). But the robot still saw raw keys and a fragmented assignment
in its **input**:

1. `job->note` (the thread transcript) passed `nostr:npub1…` tokens to
   grok verbatim via `-p`. The LLM copied/truncated them.
2. `job->prompt` (the assignment) passed the scoped clause verbatim. For
   `nostr:<Happy> generate a riddle and ask nostr:<Major> to solve it`,
   `hush_agent_extract_clause` cut Happy's clause at the peer token,
   leaving the dangling fragment `generate a riddle and ask` — no object.
3. The prompt carried no explicit self-identity ("You are Happy"), so the
   LLM substituted itself for the peer.

Post-hoc scrub can delete a bad token; it cannot reconstruct
"ask @Major to solve it" from "generate a riddle and ask".

## Fix (C is the contract, input-side)

The LLM now sees names, never keys, and a complete assignment.

- `hush_agent_humanize_ask`: rewrites `nostr:npub1…` to `@Name` for known
  robots and the human, drops the acting robot's own token, and drops
  unknown tokens. Applied to both the thread (`job->note`) and the
  assignment (`job->prompt`).
- `hush_agent_append_assign`: humanizes the ask before it is snip'd into
  the system prompt.
- `hush_agent_fill_job`: when a scoped clause ends mid-sentence (no
  `. ! ?`) and the robot has a peer, appends ` @PeerName`, so
  "…and ask" becomes "…and ask @Major".
- `hush_agent_fill_prompt`: adds `You are {Name}.` identity.

Output-side scrub (`hush_agent_rewrite_mentions`) is retained as the
backstop: it still canonicalizes `@npub1`/`@Name` to `nostr:<full>`,
expands truncated prefixes, and strips self/echo/handoff before store.

## Conversation ownership model

Nostr is a graph of signed notes, not a chat transcript. The structure is
carried in tags, not in any server-owned object:

- A **thread** is a root kind-1 note plus every reply whose `e` tag
  resolves to that root (NIP-10).
- A **mention** is a `p` tag naming a pubkey; the content may also carry a
  `nostr:npub1…` NIP-21 URI at the in-sentence position.

Hush maps that onto agent dispatch:

- The human's note is the thread root. `hush_agent_follow_t` (keyed by
  root id) owns the **turn order** for the initial co-mention: which robot
  runs, in what wave, and who stops.
- **Thread owner ≠ conversation owner.** The human owns the thread (the
  root note). The conversation is owned by the follow-queue state plus the
  wake ledger, both transient and keyed by root. A robot addressing a peer
  via a fresh `p` tag starts a new queue, not a continuation of the old one.

Known gaps (next milestone, not this change):

- Follow-queue and wake-ledger state are in-memory; a relay restart loses
  turn position mid-thread. Persisting the 1024-event ring (PR #134) helps
  replays but does not restore `g_follow`/wake.
- Robot-to-robot p-tags spawn nested queues, so a long multi-robot
  conversation is a set of per-root queues, not one unified conversation
  graph. "Who owns the conversation when the thread owner is idle" is the
  open question this change does not answer.

## Verification

- `check_agent.sh`: added a delegation-phrasing case
  ("generate a riddle and ask @Major to solve it") asserting Happy's
  assignment names `@Major` and carries no `nostr:npub`. Updated the old
  "thread snip must keep npub intact" assertion to "peer shown as @Name,
  no raw keys, no self npub".
- `make test`: ALL TESTS PASSED.
