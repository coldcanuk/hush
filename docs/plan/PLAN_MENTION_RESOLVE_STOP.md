# PLAN: mention names + last-robot stop

Research lock: `docs/research/RESEARCH_MENTION_RESOLVE_STOP.md`.
Worktree: `/opt/repo/hush/worktrees/mention-resolve-stop`
Branch: `gb/mention-resolve-stop`
Base: `main` `4e538a7d3`

Land via PR only. C11, write-legible-c on every new helper.
`./scripts/embed-ui.sh hush-c/demo` after HTML.

## Scope

**In.**

1. Snip does not cut `nostr:npub1` / `npub1` tokens. Soft cap fits two
   mentions + a short clause.
2. Before insert: `@npub1` → `nostr:npub1`; truncated npub → full
   roster npub; `@Name` → `nostr:<full>` (longest name first).
3. Prompt: peers as `@Name` (always, including 2-robot / explicit).
   Never teach `nostr:%s`. Last robot: stop, no handoff.
4. `renderPreservingMentions` uses `sameKey` (and `@npub1…` tokens).

**Out.** Hop policy, leader election, intro table, `--no-memory`
changes, new persistence, rewriting historical events.

## Success

- Two-mention `P:` log contains both full npubs.
- Fake grok `@Major` lands as `nostr:<payne_npub>`.
- Second robot's `S:` contains the last-robot stop rule; first does
  not.
- Served UI mention pills use `sameKey`.
- `make test` green.

## Phase 0 — Isolate

- [x] M0.1 Worktree `gb/mention-resolve-stop`.

## Phase 1 — Research gate

- [x] M1.1 This plan + research file. Commit.

## Phase 2 — Snip (H1)

- [x] M2.1 Atomic npub copy in `hush_agent_snip_line`. Raise
      `HUSH_AGENT_SNIP_MAX` to 384. Line buffers
      `SNIP_MAX + HUSH_IDENTITY_NPUB_MAX`.

## Phase 3 — Rewrite + prompt (H2, H4)

- [x] M3.1 `hush_agent_rewrite_mentions` on finish (bounded).
- [x] M3.2 Peer list is `@Name` for every group job. `job.last` +
      last-robot stop rule. PEER_STANDARD: write `@Name`, never keys.

## Phase 4 — UI (H3)

- [x] M4.1 `mentionHit` + `sameKey` in `renderPreservingMentions`.
      Embed at build.

## Phase 5 — Verify

- [x] M5.1 `check_agent.sh`: both npubs in grok log; `@Major` rewritten;
      last-rule on Payne only. `make test` green.

## Final

PR to main; auto-merge; remove worktree.
