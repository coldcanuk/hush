# RESEARCH: NIP-42 AUTH + private-hive authorization for Hush 0.0.1

**Scope.** Implement P0 slice 1 of the prioritized roadmap: NIP-42 authentication over the
newline-JSON wire protocol, and turn the "private" vibe flag into a real confidentiality
boundary (join credential enforced; join tokens stored hashed).
**Baseline verified.** Worktree `worktrees/nip42-authz` on branch `gb/nip42-authz` @
`8f4ad1f84` (main, post PR #159). Baseline `./configure && make && make test` is green
("ALL TESTS PASSED", exit 0).
**Related in-repo work.** `docs/research/RESEARCH_EVENT_AUTH.md` (BIP-340 verification,
already landed as PR #157) and its section (f) NIP-42 design sketch; `SECURITY.md:45-86`;
`NOSTR.md`.

## (a) Verified current state

| Area | State @ 8f4ad1f84 | Evidence |
|------|-------------------|----------|
| BIP-340 verify | **done** | `hush_schnorr.c/h`; official 19-vector CSV test (`tests/test_schnorr.c`) |
| Wire EVENT verification | **done** | `hush_handle_event_msg` (`hush_relay.c:656-682`) runs `hush_event_verify` (id recompute + sig) before store/fanout; OK false on failure |
| `hush_event_t.sig` + strict parser | **done** | `hush_event.h:31-33`, `hush_proto_parse_event` parses created_at/tags/sig |
| NIP-42 AUTH | **absent** | no `HUSH_MSG_AUTH`, no `["AUTH",<challenge>]` on connect, no CLOSED formatter, no per-connection auth state (`struct client` `hush_relay.c:89-102`) |
| kind 22242 handling | **absent** | would be stored and fanned out like any kind |
| Private vibe enforcement | **absent** | `vibe_token` (16 hex chars, `hush_launch_make_token` `hush_launch.c:2043`) is generated, saved **plaintext** in `vibe.json`, shown in the UI, restored, and **never checked on any path** (`SECURITY.md:83-85` confirms). Only presence kinds are filtered by `vibe_public` (`hush_presence_req_ok`) |
| HTTP session token | done (unchanged) | `hush_http_request_is_authed` (`hush_http.c:607`): cookie/bearer/`X-Hush-Token`/`?k=`; exempt: `/api/status`, `GET /api/complete?t=` |
| Membership data | exists, unused for wire | `hush_roster_t.members[].pubkey_hex` (64 hex, `hush_roster.h:86,103`); `g_launch.human.pubkey_hex` |
| Wire test signing | pinned fixture only | `check_collaboration.py:575-617` embeds one externally signed EVENT; no signer available at test time |

## (b) Design decisions

### BIP-340 signer for tests (independent implementation)

Relay challenges are random per connection, so integration tests must sign live challenges.
`hush-c/tests/sign_bip340.py` implements BIP-340 signing in ~60 lines of pure-Python
EC arithmetic (no external deps; python3 is already a test dependency). The unit-test
fixture (fixed key, fixed challenge) is generated once by that script and pinned as C
constants, so unit tests do not depend on Python. Cross-checked against the official
19-vector verifier already in the repo.

### Wire protocol additions (Hush line protocol, documented in NOSTR.md)

- Inbound `["AUTH",{event}]` (NIP-42 client form) → `HUSH_MSG_AUTH`. Inbound
  `["AUTH","<challenge>"]` (server form, echoes) → `HUSH_MSG_AUTH` with empty event, ignored.
- Inbound `["JOIN","<token>"]` → `HUSH_MSG_JOIN` (Hush extension: present the vibe join
  token; grants guest access to a private hive).
- Outbound formatters added: `["AUTH","<challenge>"]`, `["CLOSED","<sub>","<reason>"]`,
  `["NOTICE","<text>"]`.

### Challenge lifecycle

1. Minted per connection: 32 random bytes hex-encoded (64 chars) via `RAND_bytes`.
2. Sent **lazily on the first complete wire line**, not at accept: HTTP and wire share the
   listener and `is_http` is decided from the first bytes, so an accept-time challenge
   would poison HTTP responses.
3. Regenerated after every failed AUTH validation (prevents challenge reuse).
4. Kept after success; a successful re-AUTH rebinds `authed_pubkey` (last valid wins).
5. Freshness is enforced on the AUTH **event** (created_at within ±600 s), not on the
   challenge: a challenge lives exactly one connection, which is the NIP-42 model.

### AUTH validation (`hush_nip42.c`, new module)

`hush_nip42_validate(&request, reason, reason_len)` where `request = {event, now,
challenge, bind_addr}`:

1. kind == 22242 (else "invalid: wrong kind");
2. |now − created_at| ≤ 600 s (else "invalid: stale created_at");
3. exactly one `["challenge","<value>"]` tag equal to the connection challenge
   (else "invalid: challenge mismatch");
4. `relay` tag: **optional** (documented deviation — Hush has no canonical public URL).
   When present, the host part (after stripping ws:// wss:// http:// https:// and any path)
   must match one of `localhost`, `127.0.0.1`, `::1`, `[::1]`, or the configured
   bind address (else "invalid: relay mismatch");
5. `hush_event_verify` (id recompute + BIP-340) — its reasons ("invalid: id mismatch",
   "invalid: bad signature") propagate unchanged.

### Authorization matrix

Public hive (`vibe_public`): challenge still sent (NIP-42 compliance); reads and
signature-valid writes allowed without AUTH; kind 22242 always rejected with
`["OK", id, false, "restricted: ..."]` and never stored/fanned out.

Private hive (`!vibe_public`), per connection:

| Operation | Gate |
|-----------|------|
| `REQ` | denied with `["CLOSED", sub, "auth-required: ..."]` unless member-AUTH or token JOIN |
| `EVENT` (non-22242) | OK false `"auth-required: ..."` unless member-AUTH or token JOIN |
| `EVENT` pubkey | when member-AUTH: must equal `authed_pubkey`; when token-joined only: any validly signed pubkey (token = guest capability; documented tradeoff, full fix is the capabilities item P0 #3) |
| fanout | only delivered to authorized connections |
| kind 22242 | always `restricted`, never stored |

Member = `pubkey_hex` equals `g_launch.human.pubkey_hex` or a roster member
(`hush_roster_has_member`). AUTH with a non-member key gets OK false
`"auth-required: not a member"`.

### Join token: hashed storage + migration

- `vibe.json` stores `vibe_token_hash` (SHA-256 of the 16-hex-char token, hex). The
  plaintext stays only in memory for the run that created/rotated it (display-once).
- Load: `vibe_token_hash` preferred; a legacy plaintext `vibe_token` field is accepted,
  kept in memory this run, and the file is rewritten hash-only on the next save
  (one-shot migration).
- JOIN compares `sha256(presented)` against the stored hash in constant time.
- `POST /api/vibe {"action":"rotate_token"}` (session-token gated like every `/api/*`
  route) regenerates the token, persists the new hash, and returns the plaintext once.
- UI copy: after a restart the session JSON carries an empty `join_token`; the UI line is
  updated to say the token must be rotated to be re-shared.

### Relay wiring (`hush_relay.c`)

- `struct client` gains `challenge[65]`, `authed_pubkey[65]`, `token_joined`; all
  cleared in `hush_drop_client`.
- New handlers `hush_handle_auth_msg` and `hush_handle_join_msg`; dispatch extended in
  `hush_on_nostr_line`.
- `hush_handle_event_msg` order: 22242 restricted → private-hive gate → pubkey bind →
  verify (existing) → store/fanout.
- `hush_handle_req_msg`: private-hive gate first.
- `hush_fanout`: skips unauthorized clients in a private hive.

### HTTP

- No route loses or gains token requirements; `/api/vibe` learns `rotate_token` only.

## (c) Risks

1. **Crypto correctness.** Mitigated: verification is the already-landed, vector-tested
   `hush_schnorr_verify`; new code does not touch the curve math.
2. **Breaking existing wire tests.** Mitigated: default vibe is public (`hush_launch.c:801`,
   `vibe_public = 1`), so no existing test changes policy; the new challenge line precedes
   existing `["AUTH",...]`-ignorant clients harmlessly (they ignore unknown frames today —
   `HUSH_MSG_UNKNOWN`).
3. **HTTP poisoning by challenge.** Mitigated: lazy challenge on first wire line.
4. **Replay.** NIP-42 closes the freshness/session gap for AUTH-gated operations; plain
   EVENT replay remains possible on public hives by design (NIP-01 relays accept idempotent
   events) — documented.
5. **UI regression (join token display).** Mitigated: session JSON keeps the live plaintext;
   after restart it is empty and the UI copy explains rotation.
