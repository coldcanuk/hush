# RESEARCH: rate limits, quotas, and overload behavior for Hush 0.0.1

**Scope.** P0 slice 3: defensive token-bucket rate limiting across the relay's
ingress paths (wire, HTTP, AI dispatch), so a flood cannot exhaust CPU,
memory, sockets, or provider budget. The bounded outbox + slow-consumer
disconnect already covers egress backpressure (tested); this slice covers
ingress.
**Baseline.** `main` @ `d8527eeef` (WS slice merged, PR #162). Worktree
`worktrees/rate-limits`, branch `gb/rate-limits`.
**Method.** Static reading of relay/http/intel/agent sources + inventory of
existing throttling.

## (a) Existing protections (verified)

| Mechanism | Location | Notes |
|---|---|---|
| 16-client cap | `hush_relay.c` `HUSH_MAX_CLIENTS` | accepts then drops beyond 16 |
| 32 KiB receive cap + oversize NOTICE | `hush_relay.c` | raw lines; WS frames capped at `HUSH_WS_MSG_MAX` |
| 64 KiB per-client outbox + POLLOUT | `hush_relay.c` | slow consumers disconnected, no torn frames |
| Channel burst hold | `hush_intel.c` `hush_intel_burst_ready` | `burst_ms` (500-5000) spaces robot dispatches |
| (channel, robot) cooldown | `hush_intel.c` `g_cooldowns` | `cooldown_s` throttles robot-triggered chains |
| Per-channel job cap | `hush_launch.h` `max_jobs` (1-4) | channel policy |
| Global job table | `hush_agent.c` `g_jobs[HUSH_AGENT_JOBS_MAX]`, MAX=4 | full-table behavior is silent |
| BIP-340 verify ~300 µs | `hush_schnorr.c` | unthrottled: a flood of bad sigs is pure CPU |

No token buckets, no per-IP state, no 429 path, no per-pubkey or per-robot
quotas, no explicit overload signal.

## (b) Design

### hush_limiter module (new)

`hush_limiter_t { double tokens; int64_t last_ms; double rate_per_s;
double burst; }` with `hush_limiter_init`, `hush_limiter_take(l, now_ms)`
(lazy first-take init, linear refill, clamp to burst), and
`hush_limiter_now_ms()` via `clock_gettime(CLOCK_MONOTONIC)`. No fallible
operations, no status enum, asserts on NULL. Wall clock stays out of limiters
(backwards clocks would mint tokens).

### Wire limits (hush_relay.c)

Event gate order in `hush_handle_event_msg` (documented in NOSTR.md):

1. kind 22242 restricted → 2. private-hive authorization → 3. AUTH pubkey
bind → 4. per-IP + per-connection EVENT buckets (cheap, before crypto) →
5. BIP-340 verify → 6. per-pubkey bucket (verified events only) → 7. store.

| Scope | Rate | Burst | Exhaustion |
|---|---|---|---|
| per-IP connections | 10/s | 20 | accept, then close |
| per-IP EVENTs (attempts) | 30/s | 60 | `OK false "rate-limited: …"` |
| per-connection EVENTs | 10/s | 20 | `OK false` |
| per-connection REQs | 5/s | 10 | `CLOSED "rate-limited: …"` |
| AUTH attempts | 8 total/conn | — | drop connection |
| JOIN attempts | 16 total/conn | — | drop connection |
| per-pubkey EVENTs (verified) | 30/min | 10 | `OK false` |

Per-IP state: a 16-entry table keyed by `sin_addr.s_addr` (listener is
AF_INET; `getpeername` at accept). Table full → fail open. Per-pubkey
table: 16 entries keyed by 64-hex pubkey, linear scan, fail open when full.
Defaults live in the relay enum block; generous on purpose — the default
hive is a local single-user app.

### HTTP limits (hush_http.c)

- Per-IP request bucket (60/s, burst 120) checked in `hush_http_guard` for
  every authed `/api/*` route; `/api/status` stays exempt (liveness).
  Exhaustion replies `429 Too Many Requests` with JSON
  `{"ok":false,"error":"rate-limited"}`.
- Expensive provider routes get their own global buckets, checked in the POST
  dispatcher before body parsing (so a flood cannot reach the fork):
  `/api/complete` 12/min burst 12, `/api/fixup` 12/min burst 12,
  `/api/reply` 12/min burst 12 → 429. The hive has one session token, so
  global == per-session here.

### AI budgets and overload (hush_intel.c / hush_agent.h)

- `HUSH_AGENT_JOBS_MAX` moves to `hush_agent.h`; new
  `int hush_agent_jobs_active(void)` counts busy jobs.
- Overload (graceful degradation): when the global job table is full,
  `hush_intel_policy_blocks` denies new dispatches with an honest
  `"overloaded: job queue full"` line instead of silently dropping — chat and
  event delivery keep flowing, only new AI work is refused.
- Per-robot provider budget: a table keyed by robot pubkey (reusing the
  cooldown-table pattern, 20/min burst 5) denies dispatch with
  `"rate-limited: robot busy"`. Existing burst/cooldown policies still run
  first and are stricter by default; this bucket is the hard provider-cost
  ceiling. Test pace verified: check_agent.sh dispatches at channel-cooldown
  spacing (≥10 s), far under the bucket.

### Rejection vocabulary

`rate-limited: …` joins `invalid:`, `auth-required:`, `restricted:` as
the machine-readable OK/CLOSED prefix family; `429` joins 401/403/404 on
HTTP. All documented in NOSTR.md/SECURITY.md.

## (c) Risks

1. **Breaking existing suites** — mitigated: defaults sized well above every
   current test's pace (verified against check_agent.sh, check_collaboration.py,
   check_pwa.sh patterns); the flood-based checks only trip them on purpose.
2. **Double-gating robots** — mitigated: robot bucket is looser than existing
   policy and sits last.
3. **Clock issues** — mitigated: monotonic clock; lazy init avoids
   first-request starvation.
4. **Per-IP tables are small** — fail-open design documented; a 16-client
   hive cannot overflow them by construction.
