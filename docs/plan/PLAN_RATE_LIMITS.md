# PLAN: rate limits, quotas, and overload (slice 3)

Follows RDAP. Research gate: `docs/research/RESEARCH_RATE_LIMITS.md`.
Worktree `worktrees/rate-limits` on `gb/rate-limits`; commit per milestone;
land via PR only. Every C change follows write-legible-c.

## Milestones

### M3.1 — Research + plan (this gate)

### M3.2 — `hush_limiter` module + unit tests
- `hush-c/include/hush_limiter.h` / `hush-c/src/hush_limiter.c`: struct,
  `hush_limiter_init`, `hush_limiter_take`, `hush_limiter_now_ms`
  (`clock_gettime(CLOCK_MONOTONIC)`, `_POSIX_C_SOURCE 200809L`).
- `hush-c/tests/test_limiter.c`: burst consumption, simulated refill
  (monotonic now passed in), clamping, lazy first take, zero-rate deny,
  NULL asserts. No wall clock in the math.
- Verify: `make tests/test_limiter && ./tests/test_limiter`.

### M3.3 — Wire limits (hush_relay.c)
- Client struct: `uint32_t ip_addr`, `hush_limiter_t event_lim`,
  `hush_limiter_t req_lim`, `int auth_attempts`, `int join_attempts`.
- Static per-IP table (16) + per-pubkey table (16); helpers
  `hush_relay_ip_take`, `hush_relay_pubkey_take` (fail open when full).
- `hush_accept_new`: getpeername + per-IP connection bucket.
- `hush_handle_event_msg`: gate order from research; OK false
  `rate-limited:` before verify and after per-pubkey check.
- `hush_handle_req_msg`: CLOSED `rate-limited:` gate first.
- `hush_handle_auth_msg` / `hush_handle_join_msg`: attempt caps → drop.
- Resets in `hush_accept_new` / `hush_drop_client`.
- Verify: `make` clean; existing `make test` stays green.

### M3.4 — HTTP limits (hush_http.c)
- Static limiters + `hush_http_reply_rate_limited` (429 JSON).
- `hush_http_guard`: per-IP request bucket after auth (status exempt by
  existing routing).
- POST dispatcher: complete/fixup/reply buckets before the serve_* calls.
- Verify: `make test` green.

### M3.5 — AI budgets and overload (hush_intel.c / hush_agent.h)
- Move `HUSH_AGENT_JOBS_MAX` to `hush_agent.h`; add
  `hush_agent_jobs_active()` (observability hook).
- Per-robot bucket denial at `hush_intel_dispatch` (the actual provider
  fork), not policy validation — policy checks stay UX-paced by design.
  Overload denial relies on the existing agent start-failure diagnostic
  (revision: the planned intel-side overload gate broke the confirm-hold
  unit semantics and was dropped).
- Verify: `make test` green (check_agent.sh exercises the real dispatch).

### M3.6 — Integration test + docs
- `hush-c/tests/check_limits.py` (self-contained, Relay harness pattern):
  wire EVENT flood → `rate-limited` OK false; REQ flood → CLOSED; 9 bad
  AUTHs → connection closed; 25 rapid connections → some refused; 61 rapid
  `/api/session` → a 429; 13 `/api/fixup` with empty bodies → 12 parse
  errors then a 429 (no provider forked). Wire into `make test`.
- `NOSTR.md` (rate-limited/overloaded prefixes + gate order),
  `SECURITY.md` (abuse-controls section), `README.md` bullet,
  `CHANGELOG.md` entries.

### M3.7 — Land
- Push, `gh pr create`, `gh pr merge --auto --merge`, wait for MERGED,
  `git pull --ff-only origin main`, remove worktree + branch, clean
  `make test` on main.
