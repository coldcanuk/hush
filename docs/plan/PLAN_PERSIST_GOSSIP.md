# PLAN: Persist store.ring + kind-1039 claim gossip

**Worktree:** `worktrees/persist-gossip` / `gb/persist-gossip`  
**Research:** `docs/research/RESEARCH_PERSIST_GOSSIP.md`  
**Land:** PR to `main` (no local merge).

## Architecture

Two files, two jobs.

| File | Module | Job |
|---|---|---|
| `$HUSH_HOME/store.ring` | `hush_store` | Bounded event ring |
| `$HUSH_HOME/agents/wake.ledger` | `hush_wake` | Claim lock |

Kind **1039** (`HUSH_WAKE_KIND_CLAIM`): regular event, gossip payload. Not 30315. Not hidden by `hush_presence_req_ok`.

Load path: packed records → `hush_store_insert` each (replace re-applies) → `hush_wake_ingest_store`.

## Milestones

- M1: `store.ring` save/load, fsync, cap, tests
- M2: 1039 publish/ingest, peer deny, tests
- M3: relay persist_open + EVENT ingest; `make test`; PR

Offline-from-30315: never.
