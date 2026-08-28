# RESEARCH: Persist the 1024 event ring + multi-box claim gossip

**Date:** 2026-08-28  
**Worktree:** `worktrees/persist-gossip` / `gb/persist-gossip`  
**Base:** `main` at `dc2ef8f54` (PR #132 / issue #116 merged). Stable `d` is in the tree.

## Scope (locked)

**Primary goal**

1. Persist the bounded 1024-event store across `hush-relay` process death.
2. Gossip **claim ledger facts** (not 30315) so a second device/box with the same robot keypair does not take live work.

**Non-goals**

- Inferring offline / online from a missing 30315. Offline remains `enabled==0` or no ready AI provider.
- SQLite, unbounded logs, shared `HashSet`, kind 44102, Buzz ACP.
- Changing NIP-38 kinds 30315/1038.
- Treating 30315 as the lock. Ledger is the lock. 30315 is “what I am doing.”
- Local merge to `main`. Land by PR.

**Traps this work must not fall into** (operator-quoted)

| Trap | Required behavior |
|---|---|
| Persist Working lines with process tokens `hive:fN` | Persist 30315 as stored; `d` is already reconstructible (`hive:`+40 hex). Never mint tokens into `d`. |
| Treat the file as unbounded | Cap = `HUSH_STORE_CAPACITY` (1024). Oldest eviction stays. |
| Skip fsync | fsync file and directory on save. |
| Reload without addressable replace | Load by `hush_store_insert` in oldest-first order so `(pubkey, kind, d)` replace re-applies. |
| Confuse store file with claim ledger | Two files, two jobs: `$HUSH_HOME/store.ring` vs `$HUSH_HOME/agents/wake.ledger`. |

## Facts in the tree

- `hush_store`: RAM ring, `count<=1024`, addressable replace for kinds 30000–39999, regular kinds append until eviction. **No disk.** `hush_store_create` in `hush_relay_run`; destroy on cleanup. HTTP and NIP-01 EVENT both insert into `g_store`.
- Header comment claims “Insert if not duplicate id”; **code does not check duplicate ids.** Load must not invent a duplicate-id index.
- `hush_event_t` worst-case is tens of KiB (content 4096 + tags 32×4×256). A raw dump of 1024 structs is ~30MB. Packed records of *used* bytes stay bounded and smaller.
- Wake ledger already: device.id, lease 90s, other-device deny. That only works if both processes share the ledger file. Gossip is for when they do **not**.
- NIP-01 `EVENT` is the only wire that already moves events between boxes. `hush_handle_event_msg` inserts and fans out.
- `hush_presence_req_ok` hides **only** 30315 and 1038 on private vibes. A new claim kind can stay visible so a peer REQ/EVENT path can see locks.
- 30315 Idle expiry and #116 done+clear mean **missing 30315 is normal**. Do not gossip “offline” from that.

## Gossip medium (decision)

Do **not** invent a second network. Publish a **regular** (not addressable) kind **1039** claim record into the store on local claim/done/lease-done. Ingest 1039 from:

1. NIP-01 EVENT (peer box posted into this relay or this box is a client of another).
2. Replay after `store.ring` load (peer claims that survived in the event log).

Tags (all bounded): `d` (work presence `d`), `e` (root hex), `device` (32 hex), `lease` (unix decimal), `trigger` (64 hex), `state` (`claimed` or `done`). Author = robot hex. Content = state.

Ingest **never** publishes another 1039 (no echo). Own device id → ignore. Other device + live lease → ledger `claimed` with that device (local claim then denies). Peer `done` → delivery+done if we do not hold a live local lease.

30315 is not read for this. Two files stay two jobs: ingest updates the **ledger**; the ring only **carries** the event.

## Persist path

| File | Owner | Why |
|---|---|---|
| `$HUSH_HOME/store.ring` | `hush_store` | Event log, cap 1024, packed, fsync |
| `$HUSH_HOME/agents/wake.ledger` | `hush_wake` | Lock. Unchanged job. |
| `$HUSH_HOME/agents/device.id` | `hush_wake` | Install id. Unchanged. |

Enable persist only when `HUSH_HOME` is set, or `HUSH_CONFIG_DIR` is unset (same isolation as wake). Unit tests that only `hush_store_create` stay RAM.

Relay: `persist_open` after create; insert saves when enabled; cleanup saves.

## Risks

1. Save-every-insert + fsync latency. Mitigation: packed records, still fsync (required). Cap 1024.
2. Load memcpy of raw structs skipping replace. Mitigation: insert-only load.
3. 1039 flood. Mitigation: one record per claim/done, ring still evicts; not addressable so trail exists but stays bounded by the 1024 cap.
4. Private hive: 1039 visible on REQ. Mitigation: content is state+hashes, not slugs; 30315 stays hidden. Documented.
5. Two boxes, no EVENT path, no shared home → gossip cannot fire. Mitigation: document; persist still helps one-box restart.

## Success

- Kill `hush-relay`, start again with same `HUSH_HOME`: last ≤1024 events return, 30315 `d` still `hive:`+40 hex, addressable kinds still unique per `(pubkey,kind,d)`.
- Device B ingests device A’s 1039 with live lease: B’s `hush_wake_claim` is `HUSH_ERR_DENIED`.
- No code path treats missing 30315 as offline.
- `make test` green.
