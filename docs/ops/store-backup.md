# Store backup and restore

File-level backup of the existing event-store persistence pair. No new
storage engine, no format change: the tool copies the two files the
relay already maintains.

## Files

Per `SECURITY.md` ("Event Store") and `hush-c/include/hush_store.h` /
`hush-c/src/hush_store.c`, the store persists under `$HUSH_HOME`
(default `~/.hush`) as:

- `store.ring` (`HUSH_STORE_FILE`) — snapshot, rewritten every 256
  inserts and once at shutdown
- `store.log` (`HUSH_STORE_LOG_FILE`) — appends since that snapshot,
  replayed on load; synced at most once per second

## Tool

`scripts/hush-store-backup` (POSIX shell, no new C surface):

```bash
scripts/hush-store-backup backup <dest-dir>   # $HUSH_HOME pair -> dest
scripts/hush-store-backup restore <src-dir>   # src pair -> $HUSH_HOME
```

Both directions copy through a temp file plus rename and leave the
copies mode 0600, matching the store writer. Backup refuses when the
store home holds neither file; restore refuses unless the source holds
both files. `$HUSH_HOME` overrides the home; otherwise `~/.hush` is
used, mirroring `hush_home_root`.

## Stopped (preferred) vs live

Back up with the relay **stopped** for a consistent pair: a clean
shutdown compacts the ring and leaves both files in a loadable state.

A live copy (relay running) is best-effort. As documented in
`SECURITY.md`, the log syncs at most once per second, so a hard crash
can already lose the last second of chat; a live backup inherits that
same ≤1s durability window and may additionally catch `store.ring`
and `store.log` from different instants. On load the relay replays the
log over the snapshot and ignores a torn tail, so a live backup still
restores — but with no tighter guarantee than the store itself gives.
Do not invent one.

## Restore procedure

1. Stop the relay (`--quit` / Exit; never restore under a running
   relay — it holds the log open and will compact over your files).
2. `scripts/hush-store-backup restore <src-dir>` (needs both
   `store.ring` and `store.log` in the source).
3. Start the relay (or reopen persist); it loads the snapshot and
   replays the log on top.

## Round-trip proof

`hush-c/tests/test_store_backup.c` runs under `make test`: it inserts
seed events into a temp `$HUSH_HOME`, backs up via the script, wipes
the store files, restores via the script, reopens, and asserts the
events match.
