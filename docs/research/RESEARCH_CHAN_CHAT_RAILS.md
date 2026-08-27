# RESEARCH: Co-mention follow-through, one intro, chaperons, channel events

**Date:** 2026-08-26  
**Worktree:** `worktrees/chan-chat-rails` / `gb/chan-chat-rails`  
**Protocol:** /trouble four-minds (TOOLED). MCP mail/calendar/GitHub: not consulted.

## What is broken (quoted)

```
@Happy tell me a joke @Major analyze the joke, was it funny? rate it.
Thread · 3
👍Happy
🎯Major
```

- Major introduced himself twice
- Happy's introduction missing
- Happy generated the joke; Major never replied
- Happy mentioned himself `@Happy`
- Major was supposed to analyze Happy's joke

## Phase 0 — Context Register

- MCP: github/gmail/calendar — not this C relay. Skipped.
- Evidence from `hush_intel.c`, `hush_agent.c`, `hush_http.c`, `demo/index.html`.

Data: dispatch is file-local. **8/10**.  
Sherlock: "Why not a live hive dump?" We do not have the user's session store; code paths below are sufficient to falsify.  
Linus: no objection.  
Brian Cox: human note happens first; Happy's joke is later. Silence after the joke is a missing second dispatch, not a hung first job.

## Evidence

**E1** `hush_http.c` POST /api/event calls only `hush_intel_consider` (not `hush_agent_consider` directly).

**E2** `hush_intel_handle_robot` for each p-tag; on first mention `hush_intel_release` calls `hush_agent_consider(store, launch, ev)` with the **original event**.

**E3** `hush_agent_consider` walks **every** p-tag and `hush_agent_handle_mention` for each.

Data: first mention therefore starts Happy **and** Major jobs on the human note. **9/10**.  
Sherlock: "Data, you claim both start — why is Happy's intro missing?" Because the user may have seen Major's intro plus a second Major policy note; or p-tag order put Major first.  
Linus: the cheapest bug is E2+E3, not a missing Happy renderer.  
Brian Cox: Major-twice is two Major-authored notes in one thread (intro + `DENY_JOBS` "Holding. This channel is at its job cap.").

**E4** `hush_intel_policy_blocks`: `jobs_busy >= max_jobs` (default 2) posts `HUSH_INTEL_DENY_JOBS` as the **robot hex**. After E3 starts two jobs, the second `handle_robot` for Major posts Holding from Major.

**E5** `robot_hops == 0` (default) posts `HUSH_INTEL_DENY_HOP` on robot-authored notes. Follow-up analyze cannot run as a hop.

**E6** `hush_agent_finish_job` inserts the joke and does **not** call `hush_intel_consider`. Extra p-tags on the joke never dispatch.

**E7** Hygiene: `"No npub"` plus peer standard `"emit the peer nostr:npub"`. Model self-@s instead of handing off.

**E8** `hush_agent_on_deck` is one intro per (hex, root). A second Major "intro" is likely E4's Holding note, not a second on_deck, unless Happy's joke has no `e` tag (new root).

**E9** `isDevLogNote` only hides `"Mention received"`. Holding/hop denies stay in chat.

## Hypotheses

| H | Claim | Explains | Does not | Falsifier |
|---|---|---|---|---|
| H1 | `agent_consider(ev)` dispatches all p-tags from the first mention | Major job on human note; job cap; second Major note | Happy intro missing if Happy truly never on_deck | consider_one: only current mention jobs |
| H2 | finish_job never re-enters intel | Major silent after joke | double intro on the first note | call follow-kick after insert |
| H3 | hops=0 blocks robot→robot | no analyze after joke | first-note double Major | follow queue bypasses hop |
| H4 | prompt forbids npub | self-@Happy | dispatch | per-robot assignment slice |

Bayesian (priors from this file, likelihood from E2–E6):  
P(H1)=0.35, P(E\|H1)=0.9 → 0.315  
P(H2)=0.30, P(E\|H2)=0.85 → 0.255  
P(H3)=0.25, P(E\|H3)=0.7 → 0.175  
Normalize top3 H1/H2/H3: **0.423 / 0.342 / 0.235**. Combined H1+H2 is the lever.

Gate: Data **yes 8/10**, Sherlock **yes 8/10** (still verify intro counts in a test), Linus **yes 9/10** (scoped consider_one + follow queue), Brian **yes 8/10** (joke then analyze is the arrow).

## Permutations (dispatch rule)

| Kind | Who speaks | After whom | Stop |
|---|---|---|---|
| 1:1 human↔human | Humans only | Each other | `robot_reply=off` or no p-tags |
| 1:1 human↔robot | Named robot | Human note | One job; intro once per thread |
| 1:n human→robots | Mention order | First robot now; later robots after the previous robot note in-thread | Follow queue; hops not required for queued assignees |
| 1:1 robot↔robot | Only if `robot_talk` and hops, or follow queue | Previous robot | `max_robot_turns` + cooldown + chaperon |
| robots-only | Same as robot↔robot | Chaperon is not a worker unless Major | Default `max_robot_turns=4`, `robot_talk=0` |

## Chaperon

- Channel names a chaperon slug. Babysit only: on-topic / bring-back / user triggers.
- **Major** may work **and** chaperon.
- User-created `role=chaperon` robots never take a work grok job.
- Chaperon need not be in the channel robot list as a worker.

## Events

In-hive ring (`hush_cevent`): typed, `seq`, `due` (unix). Types: mention, intro, job_start, job_done, follow, hop_denied, jobs_held, chaperon. `GET /api/chan-events`. No outbound HTTP.

## Flow

```
human note with Happy then Major
  → ack each
  → intro+job Happy only
  → queue Major
Happy joke inserted
  → follow-kick Major
  → intro (once)+job Major on the joke + original assignment
robot-only over max_robot_turns
  → chaperon event (+ optional Major/chaperon nudge)
```
