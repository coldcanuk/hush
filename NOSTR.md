# Hush line protocol

> **Status: Hush 0.0.1.** This document describes what the in-tree
> `hush-relay` actually speaks. It is Nostr-shaped, not Nostr: there is no
> WebSocket transport and no NIP-42 AUTH. Event signatures are verified on
> ingest. Earlier revisions of this file described the upstream NIP-29 reference relay
> that Hush's wire format was modelled on; that description was wrong for this
> codebase and has been replaced.

## Transport

- One TCP port serves both HTTP and the line protocol. The relay sniffs the
  first bytes: HTTP methods go to the embedded PWA/API, anything else is
  treated as newline-delimited JSON.
- One JSON value per LF-terminated line. The receive buffer is 32 KiB; a line
  that fills it receives `["NOTICE","line too long"]` and the connection closes.
- The listener binds `127.0.0.1` by default. `--listen ADDR` (for example
  `--listen 0.0.0.0`) exposes it deliberately.
- There is no WebSocket endpoint, so stock Nostr clients cannot connect.

## Client to relay

| Frame | Meaning |
|---|---|
| `["EVENT", <event>]` | Publish an event. The relay stores it, answers `OK`, and fans it out to matching subscriptions. |
| `["EVENT", <sub_id>, <event>]` | Same; the subscription id is accepted and ignored on publish. |
| `["REQ", <sub_id>, <filter>, ...]` | Subscribe. Up to four filters. A new REQ replaces the connection's previous subscription. |
| `["CLOSE", <sub_id>]` | Cancel the connection's subscription. |
| `["COUNT", ...]` / `["AUTH", ...]` | Parsed as typed frames and otherwise ignored. |

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
| `["NOTICE", "<message>"]` | Protocol notice, such as an oversized line. |

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
- There is no NIP-42 challenge yet, so a captured valid event can be replayed,
  and locally created events (the HTTP path) carry no signature.

## HTTP side

The same port serves the token-gated PWA API. See [SECURITY.md](SECURITY.md)
and [README.md](README.md).

## Deliberately not implemented

WebSocket transport, NIP-42 AUTH (only ingest signatures exist), NIP-29 relay
groups, NIP-50 search, NIP-17
DMs, message encryption, relay-to-relay federation, and `a`-tag deletion
targets.
