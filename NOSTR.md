# Hush line protocol

> **Status: Hush 0.0.1.** This document describes what the in-tree
> `hush-relay` actually speaks. It is Nostr-shaped: the same JSON lines ride
> RFC 6455 WebSocket text frames (`ws://`) or newline-delimited raw TCP.
> Event signatures are verified on ingest and
> [NIP-42](https://github.com/nostr-protocol/nips/blob/master/42.md) AUTH
> challenges gate private hives. Earlier revisions of this file described the
> upstream NIP-29 reference relay that Hush's wire format was modelled on; that
> description was wrong for this codebase and has been replaced.

## Transport

- One TCP port serves HTTP, WebSocket, and the raw line protocol. The relay
  sniffs the first bytes: a `GET` carrying an RFC 6455 upgrade handshake
  (`Upgrade: websocket`, `Sec-WebSocket-Version: 13`) gets a `101` and
  switches to WebSocket framing; other HTTP methods go to the embedded
  PWA/API; anything else is newline-delimited JSON.
- WebSocket framing follows RFC 6455: client frames must be masked, server
  frames are unmasked text frames, one wire line per message, fragmented text
  is reassembled, ping is answered with a pong echo, and close is answered
  with a close echo. A malformed frame earns close `1002`, a binary frame
  `1003`, invalid UTF-8 `1007`, and an oversized message `1009`.
- WebSocket clients receive the NIP-42 challenge immediately after the `101`.
- Raw TCP clients receive the challenge in response to their first wire frame
  (the sniff cannot tell the protocols apart before the first bytes), so a
  raw client that has nothing to say yet sends any frame (for example an
  empty line) and reads the challenge.
- One JSON value per LF-terminated line. The receive buffer is 32 KiB; a raw
  line that fills it receives `["NOTICE","line too long"]` and the connection
  closes.
- The listener binds `127.0.0.1` by default. `--listen ADDR` (for example
  `--listen 0.0.0.0`) exposes it deliberately.

## Client to relay

| Frame | Meaning |
|---|---|
| `["EVENT", <event>]` | Publish an event. The relay stores it, answers `OK`, and fans it out to matching subscriptions. |
| `["EVENT", <sub_id>, <event>]` | Same; the subscription id is accepted and ignored on publish. |
| `["REQ", <sub_id>, <filter>, ...]` | Subscribe. Up to four filters. A new REQ replaces the connection's previous subscription. |
| `["CLOSE", <sub_id>]` | Cancel the connection's subscription. |
| `["COUNT", ...]` | Parsed as a typed frame and otherwise ignored. |
| `["AUTH", <event>]` | NIP-42 authentication. The event must be kind 22242 and echo the connection's challenge tag. Success binds the event pubkey to the socket and answers `OK true`; failure answers `OK false` and mints a fresh challenge. A client echo of `["AUTH", "<challenge>"]` is ignored. |
| `["JOIN", "<token>"]` | Hush extension: present the private-vibe join token. Success answers `["NOTICE","joined"]` and grants guest access; otherwise `["NOTICE","invalid: bad join token"]`. |

The event object parses `id`, `pubkey`, `kind`, `created_at`, `content`, and
`tags`, and `sig`. A wire EVENT must carry a valid BIP-340 signature over
its recomputed NIP-01 id; otherwise the relay answers `OK false` and stores
nothing.

Filter fields parsed and matched:

- `kinds` — up to 8
- `ids` — up to 8, exact lowercase hex
- `authors` — up to 8, exact lowercase hex
- `since` / `until` — Unix seconds, inclusive; `0` means unset
- single-letter tag filters `#e`, `#p`, `#h`, `#d` — up to four keys with
  four values each

Every supplied field inside one filter must match (AND). With several filters,
an event matching any filter is delivered (OR).

## Relay to client

| Frame | Meaning |
|---|---|
| `["EVENT", <sub_id>, <event>]` | A stored event matching the subscription. |
| `["EOSE", <sub_id>]` | End of stored events; later matches stream as they arrive. |
| `["OK", <event_id>, true\|false, "<message>"]` | Publish result. `true` means stored. |
| `["NOTICE", "<message>"]` | Protocol notice, such as an oversized line or a JOIN result. |
| `["AUTH", "<challenge>"]` | Per-connection NIP-42 challenge, 64 hex chars. |
| `["CLOSED", <sub_id>, "<reason>"]` | A subscription was refused (e.g. `auth-required: …`). |

Emitted events carry `id`, `pubkey`, `kind`, `created_at`, `content`, and
`tags`; every string is JSON-escaped.

## Semantics and limits

- Store: a 1,024-event ring; the oldest event is evicted first. Persistence is
  `store.ring` plus an append `store.log` (see [SECURITY.md](SECURITY.md)).
- Kind 5 (NIP-09): an `e`-tagged deletion removes same-author targets from the
  ring, and the deletion event itself is not stored. `a`-tag targets are not
  handled.
- Kind 7 reactions and every other kind are stored like any event. Hush has no
  reaction rendering or dedicated query API.
- Kinds 30000-39999 replace the stored event with the same author and `d` tag.
- Events that arrive over the line protocol are stored, acknowledged, and fanned
  out, but they are **not** dispatched to robots. Only `POST /api/event` from
  the PWA reaches the conversation engine.
- Wire events are authenticated: the id is recomputed and the BIP-340
  signature is verified against the claimed pubkey. Rejected events get
  `["OK", <id>, false, "invalid: ..."]` and are neither stored nor fanned out.
- Kind 22242 events are answered `["OK", <id>, false, "restricted: ..."]` and
  are never stored, fanned out, or served.
- Public vibes: `REQ` and signature-valid `EVENT` are open; AUTH is optional.
  Private vibes: `REQ` is answered `["CLOSED", sub, "auth-required: ..."]`,
  `EVENT` is answered `OK false` with the same prefix, and fan-out skips
  unauthorized connections, until the connection either AUTHs as a member
  (local human or roster member; published events must then carry the authed
  pubkey) or JOINs with the vibe token (guest read and write).
- A captured valid event can still be replayed against a public hive (the
  store is idempotent); NIP-42 removes the freshness gap for AUTH-gated
  operations.
- Ingress rate limits are token buckets. Per connection: 10 EVENT/s (burst
  20), 5 REQ/s (burst 10), 8 AUTH attempts, 16 JOIN attempts. Per source IP:
  30 wire EVENT/s (burst 60). Per author pubkey: 30 verified EVENT/min (burst
  10). Exhaustion answers `["OK", <id>, false, "rate-limited: slow down"]` for
  EVENT and `["CLOSED", sub, "rate-limited: slow down"]` for REQ; the AUTH and
  JOIN caps drop the connection. The per-IP and per-connection EVENT buckets
  run before signature verification, so a forged-frame flood cannot pin the
  CPU; the per-pubkey bucket runs after verification and only counts valid
  frames. Gate order on EVENT: restricted → authorization → pubkey bind →
  per-IP/per-connection rate → verify → per-pubkey rate → store.

## HTTP side

The same port serves the token-gated PWA API. Throttled per source IP at
60 requests/s (burst 120) with `429 Too Many Requests`; the provider routes
`POST /api/fixup` and `POST /api/complete` draw from separate 12/min
quotas. See [SECURITY.md](SECURITY.md) and [README.md](README.md).

## Deliberately not implemented

NIP-29 relay groups, NIP-50 search, NIP-17 DMs, message encryption,
relay-to-relay federation, and `a`-tag deletion targets.
