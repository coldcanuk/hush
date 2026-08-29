# Research: robot reply scrub (echo, npub keys, last handoff)

Transcript after PR #135 (riddle split works, rest does not):

```
Happy: I have keys… what am I? @Happy generate a new riddle and let
@npub1t337pnf take the next turn.
Major: A keyboard. @Happy riddle answered; @npub1t337pnf your turn,
continue the thread.
Happy is thinking
```

## Problems

1. Happy echoed the human ask, including a self-mention.
2. Both notes still showed `@npub1t337pnf` instead of `@Major`.
3. Major answered, then handed off.
4. Happy looked like it was thinking again (ack gradient on `@Happy`
   in Major's note, or a follow-on job).

## Why prompt-only failed

`HUSH_AGENT_PEER_STANDARD` said keep `@Name` mentions in the order
they were given. The model copied the human line into the riddle.
`--no-memory` is on; this is imitation of the thread + that rule, not
session memory. Last-robot prompt was prepended; the model ignored it.

Unresolvable `@npub1t337pnf` is a hallucinated or truncated key.
`sameKey` cannot map garbage to Major. Display fallback painted the
stump. C expand-on-prefix only helps when the stump is a real prefix.

## Fix (C is the contract)

After rewrite: drop self mentions, drop sentences that echo the ask
(20-char needle), drop handoff phrases, last robot drops remaining
npub tokens, tidy punctuation. Unknown npub tokens are dropped, not
kept. Last robot emits no extra p-tags. UI never paints `@npub1…`
when lookup misses.

Replay the transcript in `check_agent.sh` with a fake grok that emits
those exact strings.
