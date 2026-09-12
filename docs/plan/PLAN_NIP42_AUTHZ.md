# PLAN: NIP-42 AUTH + private-hive authorization (slice 1)

Follows RDAP. Research gate: `docs/research/RESEARCH_NIP42_AUTHZ.md`. Worktree
`worktrees/nip42-authz` on `gb/nip42-authz`; commit per milestone; land via PR only.
Every C change follows write-legible-c (ctx, out, in; ≤4 params; status enums; ≤40-line
functions; named literals; `HUSH_TRY` only in non-acquiring functions).

## Phase 2 — Milestones

### M1.1 — Wire protocol: AUTH + JOIN frames (proto)

- `hush-c/include/hush_proto.h`: add `HUSH_MSG_AUTH`, `HUSH_MSG_JOIN` to
  `hush_msg_type_t`; declare `hush_proto_format_auth`, `hush_proto_format_closed`,
  `hush_proto_format_notice`.
- `hush-c/src/hush_proto.c`: parse `["AUTH",{..}]` → event at /1 (object) or /2 (string
  sub_id form is not legal for client AUTH; object only), string form → AUTH with empty
  event; `["JOIN","<token>"]` → `sub_id` reused? No — new field `char join_token[64+1]`
  in `hush_client_msg_t`. Formatters mirror `hush_proto_format_ok`.
- `hush-c/tests/test_proto.c`: parse both AUTH forms, JOIN, and round-trip each formatter.
- Verify: `./configure && make && ./hush-c/tests/test_proto`.

### M1.2 — Auth module: challenge mint + SHA-256 join hash

- `hush-c/include/hush_auth.h`: `HUSH_AUTH_CHALLENGE_BUF` (65), `HUSH_AUTH_SHA256_HEX`
  (64); `hush_auth_challenge_mint(char *out, size_t outsz)`;
  `hush_auth_sha256_hex(const char *text, char *out, size_t outsz)`;
  `hush_auth_join_matches(const char *presented, const char *stored_hash)`.
- `hush-c/src/hush_auth.c`: `RAND_bytes` + existing hex encoder; EVP SHA-256; join
  matches = hash + constant-time `hush_auth_tokens_equal`.
- `hush-c/tests/test_auth.c`: mint shape/uniqueness; `sha256("abc")` =
  `ba7816bf...15ad`; join match / mismatch.
- Verify: `./configure && make && ./hush-c/tests/test_auth`.

### M1.3 — NIP-42 validation module + independent test signer

- `hush-c/include/hush_nip42.h` / `hush-c/src/hush_nip42.c`: `HUSH_NIP42_KIND_AUTH =
  22242`, `HUSH_NIP42_CREATED_WINDOW_S = 600`; `hush_nip42_request_t`;
  `hush_nip42_validate`; `hush_nip42_relay_host_ok` (documented deviation comment).
- `hush-c/tests/sign_bip340.py`: pure-Python BIP-340 sign; `--key <hex> --challenge
  <str>` prints signed 22242 event JSON. Pin the fixed-key fixture into
  `hush-c/tests/test_nip42.c` with a provenance comment.
- `hush-c/tests/test_nip42.c`: valid AUTH → OK; wrong kind; stale created_at; wrong
  challenge; missing challenge tag; tampered content/id/sig; relay-tag ok/mismatch/absent.
- Wire `test_nip42` into the Makefile wildcard (automatic).
- Verify: `./configure && make && ./hush-c/tests/test_nip42`.

### M1.4 — Relay: challenge, AUTH/JOIN handlers, private-hive gates

- `hush-c/src/hush_relay.c`: client state fields; lazy challenge send; handlers; gate
  helpers (`hush_client_is_authorized`, `hush_client_pubkey_ok`,
  `hush_pubkey_is_member`); event/REQ/fanout gates; kind 22242 restricted.
- Verify: `./configure && make && make test` (existing wire tests must stay green) +
  manual probe: `printf '["EVENT",{"id":"","pubkey":"","kind":1,"created_at":0,"content":"","tags":[],"sig":""}]
' | nc 127.0.0.1 <port>` on a temp `HUSH_HOME` shows the challenge line first.

### M1.5 — Join token hashing + migration + rotate endpoint

- `hush-c/src/hush_launch.c`: `HUSH_LAUNCH_VIBE_VERSION` bump (schema note);
  `hush_launch_save_vibe` writes `vibe_token_hash` (and clears legacy plaintext);
  `hush_launch_take_vibe_head` reads hash-first with one-shot legacy migration;
  `hush_launch_rotate_token` (public, declared in `hush_launch.h`).
- `hush-c/src/hush_http.c`: `/api/vibe` `{"action":"rotate_token"}` route; session
  JSON continues to expose the live plaintext or `""`.
- `hush-c/src/hush_ui_html.h` (generated header): update the join-token copy to mention
  rotation after restart. If generated from a source file, edit the source and re-run
  `scripts/embed-ui.sh`.
- Verify: `make test`; manual: create private vibe in a temp home, confirm
  `vibe.json` has `vibe_token_hash` and no plaintext; restart relay; rotate via API;
  confirm hash changes.

### M1.6 — Integration test `check_authz.py`

- Modeled on `check_collaboration.py` `Relay` harness: public-vibe challenge + open REQ;
  private vibe (set via `/api/vibe` visibility + captured join token) → REQ CLOSED, EVENT
  OK false, JOIN bad token denied, JOIN good token → REQ + fanout, AUTH via
  `sign_bip340.py` (member key = operator identity pubkey from `/api/session`) → OK true
  + REQ, AUTH pubkey mismatch on EVENT, kind 22242 restricted, fanout only to authorized
  clients.
- Wire into `hush-c/Makefile` `test` target.
- Verify: `make test` green end-to-end.

### M1.7 — Docs + changelog

- `SECURITY.md`: rewrite "Authentication" and "Authorization" sections for NIP-42 +
  private-hive enforcement + hashed join token.
- `NOSTR.md`: protocol table rows AUTH/JOIN/CLOSED/NOTICE; 22242 restricted; private-hive
  gating matrix.
- `README.md` lines 27-28: NIP-42 AUTH now implemented (replay note).
- `CHANGELOG.md`: 0.0.1 entry addendum.
- Verify: `git diff --stat` review; `make test` final.

## Phase 3 — Land

- `git push -u origin HEAD`; `gh pr create`; `gh pr merge --auto --merge`; after merge:
  `git pull --ff-only origin main`; `git worktree remove worktrees/nip42-authz`;
  delete branch locally + remotely.
- Verify: main updated, worktree gone, `make test` on main green.
