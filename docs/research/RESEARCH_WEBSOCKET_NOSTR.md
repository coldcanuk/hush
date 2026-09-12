# RESEARCH: RFC 6455 WebSocket transport for the Hush wire protocol

**Scope.** P0 slice 2: give `hush-relay` a real WebSocket Nostr transport on the
existing port, so stock Nostr clients can connect. Raw newline TCP stays (it is
the debug/dev transport; retiring it is a later P2 item).
**Baseline.** `main` @ `0df475403` (slice 1 merged, PR #161). Full `make test`
green before this worktree (`worktrees/ws-nostr`, `gb/ws-nostr`).
**Method.** Static reading of the relay/HTTP dispatch + RFC 6455 reference check
(via datatracker.ietf.org). All integration points verified against sources.

## (a) Verified integration points

| Point | Location | Note |
|---|---|---|
| Protocol sniff | `hush_on_bytes` `hush_relay.c:591` | `hush_http_looks_like` routes `GET`/… to the HTTP path; a WS upgrade is a `GET` and will be sniffed as HTTP — the upgrade check must live inside the HTTP branch |
| HTTP serving + drop | `hush_relay.c:598-609` | `hush_http_serve` then `hush_drop_client`. WS must instead write the 101, mark the client, and keep it |
| Wire send chokepoint | `hush_send_str` `hush_relay.c:685` | every wire frame (OK/EOSE/EVENT/AUTH/CLOSED/NOTICE) flows through here — make it WS-aware to frame payloads as text frames in one place |
| Challenge trigger | `hush_relay.c:628-633` | raw TCP gets the challenge lazily on its first line; WS can send it immediately after the 101 (the handshake already proved the client's protocol) |
| Client struct | `hush_relay.c:89-108` | gains `is_ws` + message-reassembly state; the 64 KiB out queue already backpressures |
| `hush_http_header_value` | `hush_http.c` (static) | header lookup is HTTP-internal; the WS module parses the upgrade request itself from the raw buffer (self-contained codec) |

## (b) RFC 6455 facts pinned for tests

- Accept = base64(SHA-1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")).
  Example: key `dGhlIHNhbXBsZSBub25jZQ==` → `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.
- Client frames MUST be masked; an unmasked client frame is a protocol error
  (close 1002). Server frames MUST NOT be masked.
- Control frames (ping/pong/close): FIN=1, payload ≤ 125, never fragmented.
- Lengths: 7-bit; 126 → 16-bit network order; 127 → 64-bit. Payload length must
  use the minimal encoding.
- Close: 2-byte big-endian status + optional UTF-8 reason. Codes used: 1000
  normal, 1002 protocol error, 1003 unsupported data, 1007 invalid payload
  data, 1009 too big.
- Fragmented text = text frame (FIN=0) + continuation frames (opcode 0), FIN on
  the last. A continuation without a started message, or a new data frame
  inside a fragmented message, is a protocol error.
- Text messages must be well-formed UTF-8 (RFC 3629); violations close 1007.
- §5.7 pinned frame bytes for the unit tests: masked "Hello"
  `81 85 37 fa 21 3d 7f 9f 4d 51 58`; 256-byte binary `82 7E 0100 …`; 64 KiB
  binary `82 7F 0000000000010000 …`; ping/pong "Hello" `89/8A 85 …`;
  fragments "Hel" `01 03 …` + "lo" `80 02 …`.

## (c) Design decisions

1. **Codec as a pure module** (`hush_ws.c/h`): handshake accept/take/reply,
   frame parse/format, UTF-8 validator. No sockets, no globals — unit-testable
   against the RFC vectors.
2. **One send chokepoint**: `hush_send_str` generalizes to
   `hush_send_buf(c, data, n)`; when `c->is_ws`, the payload is wrapped in
   an unmasked text frame. All protocol handlers stay transport-agnostic.
3. **Challenge after 101**: WS clients receive the NIP-42 challenge immediately
   after the handshake (natural trigger); raw TCP keeps the first-frame trigger.
4. **Message assembly in the relay**: complete text messages dispatch through
   `hush_on_nostr_line` unchanged; fragmented text accumulates in a bounded
   per-client buffer (`HUSH_WS_MSG_MAX` = 32 KiB, matching the line cap);
   binary frames are answered with close 1003.
5. **Replies**: ping → pong (echo payload); close → close echo then drop;
   malformed/masked violations → close 1002; oversized → close 1009; invalid
   UTF-8 → close 1007.
6. **Handshake validation**: require `Upgrade: websocket`,
   `Connection: … upgrade …` token, a 24-char base64 `Sec-WebSocket-Key`,
   and `Sec-WebSocket-Version: 13`; otherwise the request falls through to the
   normal HTTP router (documented simplification — no dedicated 400 path).

## (d) Risks

1. **Framing bugs** — mitigated by RFC §5.7 byte-vector tests + round-trips.
2. **Breaking raw TCP clients** — mitigated: raw path untouched; send chokepoint
   branches only on `is_ws`.
3. **Slow-loris via partial frames** — mitigated: bounded per-client buffers,
   existing out-queue backpressure, HUSH_MAX_CLIENTS.
4. **Masking-key side channels** — none: keys come from the client and are
   applied in-place to a bounded buffer.
