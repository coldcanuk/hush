# Store persistence bench — OBSERVED results (not targets/SLOs)

These are OBSERVED Floor measurements of the event-store insert path.
They are a baseline for the WS3 checklist item A, not performance
targets or SLOs.

## Harness

`hush-c/tests/test_store_bench` (source: `hush-c/tests/test_store_bench.c`).

The bench inserts `events` events with `content` bytes each into an
isolated `$HUSH_HOME`, times each `hush_store_insert`, then reports
median / p95 / max insert latency plus the final-compact
(`hush_store_destroy`) wall time. It fails only on an insert error;
it does not gate latency.

## Measurement metadata

- Host: `kiff` (Linux 7.1.5-76070105-generic x86_64, 16 nproc)
- Repo: https://github.com/coldcanuk/hush
- Measured SHA: `c8a7529f84baef287a889d4722ec587f5714c586` (pre-WS1 tip when Floor ran the harness)
- Date: `2026-09-22T23:40:25Z` (UTC)
- Load: events=1200, content=1024B (harness defaults)
- Binary was up-to-date vs that tree (`make -C hush-c tests/test_store_bench` → up to date)

## OBSERVED results (three consecutive runs, harness defaults)

| Run | median | p95 | max | final-compact |
|-----|--------|-----|-----|---------------|
| 1 | 0.035ms | 0.056ms | 17.667ms | 25.0ms |
| 2 | 0.035ms | 0.065ms | 17.671ms | 24.8ms |
| 3 | 0.026ms | 0.056ms | 13.877ms | 19.8ms |

Read `median` / `p95` as the steady-state insert cost and `max` /
`final-compact` as the periodic snapshot-sync cost, not as SLOs.

## Not measured

U3–U6 remain UNKNOWN / unmeasured: capacity-boundary, snapshot-cadence
stress, SIGKILL loss window, backup/restore wall time, and
upgrade-stop grace have no OBSERVED numbers yet.

## Persistence files

Per `SECURITY.md` ("Event Store") and `hush-c/include/hush_store.h` /
`hush-c/src/hush_store.c`, the store persists under `$HUSH_HOME`
(default `~/.hush`) as:

- `store.ring` (`HUSH_STORE_FILE`) — snapshot, rewritten every 256 inserts and once at shutdown
- `store.log` (`HUSH_STORE_LOG_FILE`) — appends since that snapshot, replayed on load

## How to reproduce

```bash
./configure
make -C hush-c tests/test_store_bench
./hush-c/tests/test_store_bench
```

With no arguments the binary uses the defaults (1200 events, 1024B).
Pass `[events] [bytes]` to vary the load. CI runs the bench binary as
part of `make test` but does not gate on its latency numbers yet.
