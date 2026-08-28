# PLAN: Issue #116 — Stable `d`, three keys, wake ledger

**Worktree:** `worktrees/issue-116-stable-d` / `gb/issue-116-stable-d`  
**Research:** `docs/research/RESEARCH_ISSUE_116.md`

## Exact `d` formula

```
d = "hive:" + lowercase hex of the first 20 bytes of
    SHA-256( lowercase(robot_hex) || ":" || lowercase(root_hex) )
```

- `robot_hex` and `root_hex` are each 64 hex characters (full, not sliced).
- Length: 5 + 40 = 45, fits `HUSH_PRESENCE_D_MAX` (48).
- SHA-256: OpenSSL `EVP_sha256`, same primitive as `hush_event_compute_id`.
- `"fN"` from `g_id_seq` is the fixup/HTTP handle only.

## Three keys (uncollapsed)

1. **Delivery:** slot `trigger[32]` = decoded triggering event id. Same robot+trigger after `done` → deny.
2. **Work / `d`:** slot `key[32]` = full SHA-256(robot:root). Presence `d` is `hive:` + first 20 bytes hex of that digest. Phase is slot `state`, not `d`.
3. **Local pipe id:** `hush_agent_make_token` → `"fN"`. Not passed to presence. Not in intro identity. Not in signed 30315/1038 `d` tags.

## Ledger file (not a speedup)

| Item | Value |
|---|---|
| Path | `$HUSH_HOME/agents/wake.ledger` (`hush_home_agents_dir`) |
| Device id | `$HUSH_HOME/agents/device.id` — 16 random bytes as 32 lowercase hex, created once |
| Magic | `0x314B5748` (`HWK1` little-endian) |
| Slots | `HUSH_WAKE_SLOT_MAX` = 256 (documented; order of intro max, bounded) |
| Slot | 32-byte key, 32-byte trigger, 16-byte device, state, lease unix |
| States | `empty` `intro` `claimed` `done` |
| Lease | `HUSH_WAKE_LEASE_S` = 90 = `HUSH_AGENT_TIMEOUT_S`. Not `HUSH_PRESENCE_STALL_S` (30). |
| I/O | rewrite whole file; `fsync` file and directory on claim and done |
| Never | child pids, fds, hostname, remint device every boot, hash map in front of the file |

`g_lines[]` stays the RAM cache. 30315 is “what I am doing.” Ledger is the lock.

## Claim state machine

- `empty` → take, fsync
- `claimed`, unexpired, other device → deny
- `claimed`, same device, unfinished lease → reclaim (restart mid-job; do not mint a second `d`)
- `claimed`, lease expired → clear 30315, 1038 `lease-drop` trail, then free/take
- `done` + same trigger → deny
- `done` + new trigger on same root → take
- Intro or done for `(robot, root)` → no second intro
- Claim **before** spawn. Finish/kill → `done` + `hush_presence_clear` on that `d`

Stuck UI may stay non-expiring. The **claim lease** expires.

## Modules

| Module | Role |
|---|---|
| `hush_presence` | Reconstructible `d`, `g_lines` cache, 30315/1038, REQ hide, operator JSON, lease-drop trail |
| `hush_wake` | Device id, bounded ledger, claim/intro/done/expire. New file so agent does not grow. |
| `hush_agent` | Keep `"fN"` for fixup. Store `trigger_id` separately from conversation `parent_id`. Call wake then spawn. Release = done + clear. |
| `hush_cevent` | Comment only: `g_ack` has one owner (the PWA). No `.c` rewrite. |

## Altitudes (new/changed)

- Orchestrators: `hush_wake_init`, `hush_wake_claim`, `hush_wake_done`, `hush_wake_expire`, `hush_wake_mark_intro`, `hush_presence_publish` (existing), `hush_agent_start_grok` (claim then spawn)
- Leaves: hex fold, slot scan, state apply, `make_d` formatting
- Adapters: OpenSSL SHA-256, file fsync/rename, `hush_presence_lease_drop` from wake

## Tests

`test_presence.c` (formula, replace on stable `d`, private REQ vs JSON).  
`test_wake.c` (ledger, restart, two devices, lease vs Stuck, done replay, intro once).  
`test_cevent.c` skipped for new wrap tests — already asserts ack/drops.

## Loop gates

- Phase 3: Domain 1 ≥ 9.0
- Phase 4: Domain 2 ≥ 9.0
- Phase 5: Domain 3 ≥ 9.0
- Phase 6: Domain 4 ≥ 9.0
