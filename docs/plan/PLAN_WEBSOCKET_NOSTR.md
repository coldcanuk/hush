# PLAN: WebSocket Nostr transport (slice 2)

Follows RDAP. Research gate: `docs/research/RESEARCH_WEBSOCKET_NOSTR.md`.
Worktree `worktrees/ws-nostr` on `gb/ws-nostr`; commit per milestone; land via
PR only. Every C change follows write-legible-c.

## Milestones

### M2.1 — Research + plan (this gate)
Done: docs committed below.

### M2.2 — `hush_ws` codec module + unit tests
- `hush-c/include/hush_ws.h`: opcodes, close codes, size constants;
  `hush_ws_parse_t`; `hush_ws_frame_t`; prototypes for
  `hush_ws_accept`, `hush_ws_handshake_take`,
  `hush_ws_format_handshake_reply`, `hush_ws_format_frame`,
  `hush_ws_parse_frame`, `hush_ws_utf8_ok`.
- `hush-c/src/hush_ws.c`: SHA-1 accept (EVP), base64 (EVP_EncodeBlock),
  header scan, frame parser (masking, extended lengths, control-frame rules,
  minimal-encoding check on write), UTF-8 validator (RFC 3629).
- `hush-c/tests/test_ws.c`: RFC accept vector; handshake matrix; §5.7 frame
  byte vectors both directions; round-trips; error matrix (unmasked, bad
  control frames, reserved bits, non-minimal length, bad UTF-8 cases).
- Verify: `./configure && make tests/test_ws && ./tests/test_ws` (from
  `hush-c/`; the wildcard build picks the module up).

### M2.3 — Relay integration
- `hush-c/src/hush_relay.c`: client fields (`is_ws`, `ws_msg`,
  `ws_msg_len`, `ws_frag`); `hush_send_buf` refactor; upgrade detection in
  the HTTP branch of `hush_on_bytes` → 101 + immediate challenge; WS frame
  loop (text assembly → `hush_on_nostr_line`, ping/pong, close echo, binary
  1003); resets in accept/drop.
- `hush_relay_announce`: mention `ws://`.
- Verify: `make && make test` (existing suites must stay green).

### M2.4 — Integration test `check_ws.py`
- Self-contained python client (handshake + mask/unmask framing, no external
  deps): handshake 101 + accept math; challenge frame immediately after 101;
  REQ→EOSE over WS; AUTH flow via `sign_bip340.py`; private-hive gating
  (CLOSED/JOIN) over WS; fragmented text message processed; ping→pong echo;
  unmasked client frame → close 1002; close 1000 echo; binary frame → close
  1003.
- Wire into `hush-c/Makefile` `test` target.
- Verify: `make test` green end-to-end.

### M2.5 — Docs
- `NOSTR.md`: transport section (handshake, framing rules, same JSON lines),
  challenge-after-101, client table note; remove "no WebSocket endpoint" from
  status + "Deliberately not implemented".
- `README.md` feature list; `SECURITY.md` transport note; `CHANGELOG.md`
  Unreleased entries.

### M2.6 — Land
- Push, `gh pr create`, `gh pr merge --auto --merge`, wait for MERGED,
  `git pull --ff-only origin main`, remove worktree + branch, `make test`
  on main green.
