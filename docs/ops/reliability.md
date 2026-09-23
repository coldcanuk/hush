# Single-hive reliability ops index (WS3 A–D)

One readable path through the already-landed WS3 reliability work for a
single relay ("hive"): measure the store, back it up, upgrade the
package without surprise-killing the relay, and probe it while it runs.
Each step links the operator doc that owns the detail. This index adds
no new bench numbers, restore claims, or probe fields — everything
OBSERVED lives in the linked docs.

## 1. Measure the store — [store bench](store-bench.md)

Run the insert-path harness (`hush-c/tests/test_store_bench`) and read
the OBSERVED baseline table there (three consecutive runs at the
harness defaults: 1200 events, 1024B content). Median / p95 is the
steady-state insert cost; max / final-compact is the periodic
snapshot-sync cost. Not targets, not SLOs.

## 2. Back up and restore — [store backup](store-backup.md)

File-level copy of the `$HUSH_HOME` pair (`store.ring` + `store.log`)
via `scripts/hush-store-backup backup <dest-dir>` /
`scripts/hush-store-backup restore <src-dir>`. Back up with the relay
**stopped** for a consistent pair; a live copy is best-effort with the
same ≤1s durability window the store itself gives. Never restore under
a running relay. Round-trip proof (`hush-c/tests/test_store_backup.c`)
runs under `make test`.

## 3. Upgrade the package gracefully — [package upgrade](package-upgrade.md)

Both packaging paths stop the relay the same way: SIGTERM, then poll
once per second up to the grace period (default 30s,
`HUSH_STOP_GRACE_S`). Upgrade never SIGKILLs; remove/purge still does
as a last resort. SIGTERM shares one cleanup path with `--quit` and
`/api/exit`. Canonical logic is `scripts/hush-relay-stop`; verify with
`sh hush-c/tests/check_pkg_upgrade.sh` (runs under `make test`).

## 4. Probe it while it runs — [api-status healthprobe](api-status.md)

`GET /api/status` is the liveness/readiness probe: read-only, no
session token, `200 OK` with `{"ok":true,…}`. Default bind `127.0.0.1`,
default port `10555` — see the linked doc for the probe one-liner plus
readiness-wait, systemd, and Kubernetes sketches, and for what is *not*
a health signal (`/api/exit` stops the relay; `/api/close` only
detaches the GUI).

## When to use which

- Sizing the store or checking insert health → step 1 (bench).
- Before an upgrade, migration, or risky change → step 2 (backup).
- Installing a new package version → step 3 (graceful stop).
- Live monitoring, readiness waits, supervisors → step 4 (probe).

## Scope

This closes the single-hive reliability loop for WS3 A–D. Out of scope:
federation, TLS-in-relay, multi-human.
