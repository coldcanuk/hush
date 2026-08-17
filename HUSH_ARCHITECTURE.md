# Hush Architecture (C11 port of Buzz core)

## Modules (per c-standard §1)
- hush_status: common error codes.
- hush_event: event struct + id compute + validate.
- hush_filter: filter struct + match.
- hush_store: bounded store + query.
- hush_proto: wire message parse/serialize (minimal JSON array shapes over \n).
- hush_relay: connection table, poll loop, dispatch to handlers.
- (later) hush_verify: id + sig (stub now).

## Function Classification (every fn documented at creation)
- Orchestrators: relay_run, handle_event, handle_req.
- Leaves: compute_id, filter_match_one_tag, store_insert_internal.
- Adapters: openssl_sha256 (or pure_sha256).

## Data Invariants
- All strings NUL-terminated and bounded.
- Event ids always 64 hex chars when set.
- Store never stores > CAPACITY; on insert full, evict oldest (simple).
- Loops: for(i=0; i < n && i < MAX; i++) — static bound visible.

## I/O Model (MVP)
- Single process, poll(2) on listen + client fds (max 32).
- Each client: read lines, parse as JSON array, process synchronously.
- Write: snprintf frames + send. Backpressure: drop if full (MVP).
- No threads. No async.

## Networking Surface
- TCP port 10555 (Hush). Clients connect, send " [\"EVENT\",{...}]\n "
- For real Nostr WS clients: future adapter or libwebsockets phase.

## Crypto Boundary
- hush_event_compute_id uses SHA256 of canonical serialization.
- Signature verification: always returns OK with comment in code:
  /* DEVIATION: real Schnorr verify omitted. Constraint: no secp256k1 in MVP scope.
     See hush_verify.c for adapter stub. */

## Tailwind/UI
- demo/index.html served statically or by toy httpd in later task.
- C code contains zero Tailwind strings.

## Build
- Makefile enforces legible style from commit 1.

