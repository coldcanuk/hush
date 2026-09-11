# RESEARCH — Review Hardening: synthesis (Phase 1 gate)

**Knowledge document:** [REVIEW_HUSH_0.0.1.md](REVIEW_HUSH_0.0.1.md)
**Evidence notes (all read against tree `5f67c65eb` + worktree commits):**

| Note | Scope |
|---|---|
| [RESEARCH_SECURITY_SURFACE.md](RESEARCH_SECURITY_SURFACE.md) | HTTP/security surface, route inventory, PWA integration, test callers |
| [RESEARCH_RELAY_CORRECTNESS.md](RESEARCH_RELAY_CORRECTNESS.md) | store, client writes, wire parser, intel/agent tables, leash semantics |
| [RESEARCH_EVENT_AUTH.md](RESEARCH_EVENT_AUTH.md) | event struct/id/keys, crypto environment, BIP-340 feasibility, NIP-42 |
| [RESEARCH_THREAD_MEMORY_STREAMING.md](RESEARCH_THREAD_MEMORY_STREAMING.md) | context window, providers, PWA polling, streaming/cancel/memory design |

---

## 1. Verification summary

Every review claim was checked against the source. Results:

| Review claim | Verdict | Notes |
|---|---|---|
| 1. No event authentication; no `sig` field; claimed pubkey trusted | **verified** | `hush_event.h:20-29`; `hush_event_validate` has zero production callers |
| 2. HTTP API unauthenticated and reachable off-host | **verified** | `INADDR_ANY` (`hush_relay.c:287`); only `logged_in` gates on `/api/event`, `/api/presence` |
| 3. `/api/project` shell injection | **verified** | command built `hush_launch.c:1881-1883`, executed `:1884`; only callers `hush_http.c:1751`, `test_launch.c:316` |
| 4. `Access-Control-Allow-Origin: *` on every reply | **verified** | exactly two emitters (`hush_http.c:520`, `:648`) |
| 5. `/api/session` nsec+token, `/api/ice` TURN password, provider overwrite, scan SSRF, exit | **verified** | TURN credential only while TURN is enabled |
| 6. Join token never checked; membership is not an access gate | **verified** | token only minted, persisted, displayed |
| 7. Build hardening flags absent | **verified** | only `-Wl,-z,noexecstack` |
| 8. Predictable `/tmp` dirs accepted on EEXIST | **verified** | 5 sites, no owner/symlink check |
| 9. Full-ring rewrite + fsync per insert | **verified** | 3 inserts per human message; `store.ring` full snapshot to tmp + rename + dir fsync |
| 10. 16 clients, no timeout, silent short writes | **verified** | `hush_send_str` treats EAGAIN/short write as done |
| 11. Wire parser drops tags/timestamps and filter values | **verified** | `created_at` hard-coded 1720000000; `ids`/`since`/`until` unparsed |
| 12. Hold/follow table overflow clobbers slot 0 | **verified** | `hush_intel.c:435`, `hush_agent.c:3618` |
| 13. `hush_agent_consider` dead → reset never runs | **verified** | zero call sites |
| 14. Silent no-reply on any start failure | **partially refuted** | job-table-full, ledger-full, claim-denied are silent; no-runtime, guidance failure, and turn-cap each post a note. `inflight++` after a dropped job is real and jams waves |
| 15. Replies > 4096 B become failures | **verified**, mechanism corrected | worker rejects `len>=4097` and exits without writing (`hush_agent.c:1898-1919`); the read-path erase is unreachable for this case |
| 16. `cooldown_s` unenforced; `max_jobs` only for the lead p-tag; `robot_hops` boolean | **verified** | `hush_intel.c:603,607-608,680-697`; `hush_launch.c:2594-2595` |
| 17. three JSON parsers disagree | **verified** | `hush_http.c` ad-hoc parser drops backslashes |
| 18. context window 6×384 B | **verified** | current message is not snipped |
| 19. PWA polls 4 routes at 1 s; `hush_cevent` unread by UI | **verified** | `index.html:5721-5746`, `:6125` |
| Root eviction degrades to "current message only" | **partially refuted** | the header + current message always remain and root replies still match; only the opening note/attribution is lost |

**Additional findings not in the review (now in scope):**

1. `POST /api/turn` is dead code: the method-agnostic guard at `hush_http.c:337` wins before the POST dispatch, so TURN can never be toggled from the UI (`check_turn.sh:58` only greps `"compiled":`, so tests never caught it).
2. Bare `make` after a first build stopped rebuilding: the `-include $(OBJS:.o=.d)` line precedes `all:`, so the first generated `.d` rule became the default goal.
3. Integration tests are not hermetic: they inherit the operator's real `pass` store (cold-start assertions fail when `hush/identity/nsec` exists) and several never set `HUSH_HOME`.
4. `openbsd/net/hush-relay/Makefile` declares `WANTLIB = c` while the binary links `-lcrypto`.
5. A single Nostr line >32767 B is dropped mid-frame (`hush_relay.c:475-479`); store snapshot failure is swallowed while the API still answers OK.

## 2. Crypto feasibility (decides Feature 1)

- OpenSSL **3.0.13** on this machine exposes secp256k1 through `EC_GROUP`; there is no EVP Schnorr and no libsecp256k1 package or header.
- A throwaway probe (outside the repo) implemented BIP-340 **verification only** on OpenSSL BIGNUM/EC in ~135 lines / 5.5 KB and passed all 19 official vectors, compiling clean under Hush's strict flags at ~291 µs/op.
- **Decision:** implement `hush_schnorr.c` in-repo on the already-linked `-lcrypto`; no new dependency. Fix the parser to read `created_at`/`tags`/`sig`, recompute ids on ingest, then add NIP-42 as a later milestone.

## 3. Architecture decisions (Phase 2)

| # | Decision | Rationale / constraint |
|---|---|---|
| D1 | Listener binds `127.0.0.1`; `--listen ADDR` opts into LAN exposure | Review §8.2; DNS-rebinding and LAN exposure both close |
| D2 | Per-hive 32-hex session token in `$HUSH_HOME/session.token` (0600), stable across restarts | Cookie survives restarts; file is owner-only; no new dependency |
| D3 | Credential carriers: `Cookie: hush_session`, `X-Hush-Token`, `Authorization: Bearer`, `?k=` | Browser needs no JS change (same-origin cookie); scripts/CLI get headers |
| D4 | Gate every `/api/*` route except `/api/status` and `GET /api/complete?t=` | Status keeps readiness probes working; `/api/complete` GET is already a capability token |
| D5 | Loopback responses set the cookie; `Host` must name the local machine in loopback mode; wildcard CORS deleted | Browsers bootstrap automatically; cross-origin pages cannot read or ride |
| D6 | `/api/project` uses `mkdir()` + `fork/execvp git` with path validation | Review §8.1; the only shell sink is removed |
| D7 | Store becomes append-only records + periodic snapshot | Review §8.4; keeps C11/OpenSSL, no SQLite |
| D8 | Wire parser preserves `created_at`/tags and parses `ids`/`since`/`until`/all authors/`#h` | Review §8.6; README truth |
| D9 | BIP-340 verification on ingest with `sig` stored | Feature 1; OpenSSL BIGNUM path from §2 |
| D10 | Thread memory: per-root JSONL transcript + rolling brief + real `messages[]` for API providers | Feature 2; dependency-light, survives restart |
| D11 | Streaming staged: pure SSE parser → `stream:true` + `curl -N` → job fds in the poll set → `/api/cancel` → budgets/usage → PWA consumption | Feature 3; single-threaded poll loop preserved |
| D12 | Integration tests get isolated `HUSH_HOME`, `XDG_RUNTIME_DIR`, and a fake `pass` helper | Verification must not depend on the operator's machine |

## 4. Residual risks

| Risk | Mitigation |
|---|---|
| Session token persistence turns a runtime secret into a stored one | 0600 owner-only file, O_NOFOLLOW reads, no token in logs/argv; documented |
| Token gate breaks PWA/tests | Full route inventory + cookie bootstrap + harness wrapper; browser flow checked by `check_collaboration_ui.cjs` when Playwright is available |
| BIP-340 implementation subtlety | Official vector suite as a unit test before the wire path uses it |
| Append-only store migration corrupts `store.ring` | Versioned record header + read-old-format path + restart tests |
| Scope across many goal rounds | One PR per phase; each phase has an explicit Definition of Done; plan re-frozen here |

## 5. Phase-1 gate result

Research is complete, the architecture decisions above are frozen, and
[PLAN_REVIEW_HARDENING.md](../plan/PLAN_REVIEW_HARDENING.md) carries the
concrete Phase → Milestone → Task list. Remaining phases proceed without
further research unless a task's evidence contradicts a note above.
