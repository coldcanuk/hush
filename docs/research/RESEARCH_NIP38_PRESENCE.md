# RESEARCH: NIP-38 hive presence line, trail kind, STUCK → Major

**Date:** 2026-08-27  
**Worktree:** `worktrees/nip38-presence` / `gb/nip38-presence`  
**Gate for PLAN_NIP38_PRESENCE.**

## Decisions (from operator)

1. Slugs are closed names. `Debugging Code` is one slug. Family `Debugging <Thing>` is allowed (`Debugging Algorithms`, `Debugging HTML`, …).
2. One presence line **per job** (`d` = `hive:<job-token>`).
3. Author is the actor’s identity (robot or human pubkey / nsec). Not the relay’s dummy key. Not a chaperon.
4. Idle/Waiting expire to nothing. Job start + heartbeat are the two clocks. **Stuck** does not expire; keep-alive while stuck. Major intervenes; disable clears it.
5. Private unless the vibe is public. 30315/1038 on a private hive must not leak on NIP-01 `REQ`.
6. Complement `thinking[]` chips. Do not replace them.
7. Stuck keep-alive (not edge-only). Disable the stuck agent to clear.
8. Humans and worker robots. **Not chaperons.**
9. NIP-38 is the presence *line*. Lanes: 30315, new Hush trail kind, `hush_cevent` ring, developer-log drawer.

## What NIP-38 is

[NIP-38](https://github.com/nostr-protocol/nips/blob/master/38.md): kind **30315**, addressable (`d` type). `content` is the status; empty content clears. Optional NIP-40 `expiration`. Relays keep **latest** `(pubkey, kind, d)` — not a log.

Nostr has **no** online/away/offline presence NIP. Issue [nips#160](https://github.com/nostr-protocol/nips/issues/160) (online|idle|dnd|offline) never landed. 38 is “what I am doing.”

## Current hive (evidence)

| Piece | Fact |
|---|---|
| Live jobs | `hush_agent_job_t` in RAM; token; `started`; 90s kill; `HUSH_AGENT_JOBS_MAX=4` |
| Chips | `hush_agent_status` → session `thinking[]` `{name,parent}` |
| Receipts | kind 1 `"Mention received."`; UI `visibleNotes` hides it |
| Intros | RAM table `(hex, root)` — not this feature |
| Channel log | `hush_cevent` ring, `GET /api/chan-events` |
| Store | append-only ring 1024; **no** addressable replace |
| Event ids | MVP hex, not schnorr (existing deviation in `hush_event.c`) |
| Public | `launch.vibe_public`; NIP-01 `REQ` currently dumps matching store rows |
| Chaperon | `hush_agent_is_work_ok` returns 0 for `role=chaperon`; Major slug is allowed |
| HTTP events | `/api/events` emits every store row including non-chat kinds |

## Frozen protocol (Hush convention on NIP-38)

**Presence line (NIP-38)**  
- kind `30315`  
- `d` = `hive:<job-token>` (human uses token `human` when not on a grok job)  
- `content` = slug (or empty to clear)  
- tags: `d`, optional `expiration` (unix seconds), `h` channel  
- author = robot hex or human hex  

**Trail (new regular kind, append-only)**  
- kind **`1038`** (Hush presence trail; not addressable)  
- same author, `content` = slug, tags `d` + `h`  
- survives replacements of 30315  

**Cevent**  
- type `presence` on each publish; type `stuck` on each stuck keep-alive  

**Slug table (exact, plus Debugging family)**  

`Building`, `Researching`, `Planning`, `Debugging Code`, `Conversing`, `Shooting the Breeze`, `Wasting Tokens`, `Stuck`, `Working`, `Waiting`, `Idle`  

Also ok: `Debugging ` + non-empty remainder (ASCII, no control chars).

**Clocks**  

| Timer | Value | Use |
|---|---|---|
| Heartbeat | 15s | Stuck republish + Major nudge |
| Stall | 30s without beat while job busy | enter Stuck |
| Idle/Waiting expire | 45s after last beat | clear 30315 (empty content) |
| Job kill | existing 90s | finish job; clear presence |

Stuck has **no** `expiration` tag. Keep-alive rewrites 30315 until the job ends or the agent is disabled.

**Major**  
On each stuck keep-alive, if the stuck actor is not Major and Major is enabled: `hush_agent_mention` Major on a note from the stuck robot (`p` = Major). Duplicate busy-on-parent is already skipped. Disable (`enabled=0`) kills the job and clears the line.

**Privacy**  
If `!vibe_public`, omit kinds 30315 and 1038 from NIP-01 `REQ`. Hive UI uses `GET /api/presence` (operator session). Chat `/api/events` never treats 30315/1038 as notes.

**Signing**  
Pubkey is the actor. Full schnorr remains the existing event-id MVP gap — do not invent a second crypto path in this slice.

## Non-goals

Outbound HTTP webhooks. Kind 44102 / NIP-MR. Chaperon presence. Replacing thinking chips. Inferring offline from silence. Porting Buzz Dart.

## Risks

1. Store without replace → 30315 floods the 1024 ring. **Mitigation:** addressable replace in `hush_store_insert`.  
2. 30315 in `/api/events` looks like chat. **Mitigation:** kind filter in UI + skip in event JSON stream for 30315/1038.  
3. Private hive `REQ` leak. **Mitigation:** omit those kinds when `!vibe_public`.  
4. Stuck ↔ Major loop. **Mitigation:** never Stuck-nudge Major about Major; `robot_busy` on parent.  
5. Slug spam. **Mitigation:** heartbeat 15s; publish only on change or keep-alive.
