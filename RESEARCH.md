# Hush C Port — Research Synthesis (Phase 1)

## Scope Locked
- MVP: Nostr NIP-01 basics for chat (kinds 0,1,5,7,9), EVENT/REQ/CLOSE/COUNT (COUNT minimal), simple #h channel tag + authors/ids/since.
- In-memory bounded store (ring buffer or fixed array of 1024 events for MVP).
- Filter match ported from buzz-core/filter.rs (AND within filter, OR across).
- Wire: newline-delimited JSON arrays for MVP (clients can wrap or we provide a tiny bridge later). Full WS in later milestone.
- Crypto: SHA-256 for event id computation (OpenSSL adapter or pure-C). Schnorr signature verification stubbed with explicit deviation comment (see hush_verify.h).
- No persistence, no auth challenge loop, no fan-out to Redis, no workflows.

## Key Findings
- Wire messages are tiny fixed-shape JSON arrays. Hand-rolled parser is feasible and keeps scope small + legible.
- StoredEvent concept maps to struct hush_event { char id[65]; ... uint32_t kind; char content[4096]; ... } with strict bounds.
- Filter: struct with optional arrays (kinds[8], authors[8], etc.) + tag match.
- Verification: id recompute + (stub) sig. For now id only.
- Rust uses heavy async; C will be single-thread poll loop (max 32 conns MVP).
- UI: see § Tailwind decision below.

## Tailwind Decision (logical opt-out for core)
**We opt out of using Tailwind inside C source or build.**
Reason: TailwindCSS is a CSS framework for HTML presentation. Hush C is a protocol implementation and relay engine. Mixing would violate separation and legible-C purity (no CSS strings in C).
Instead:
- hush-c/demo/ contains a standalone index.html using Tailwind via CDN (https://tailwindcss.com/docs/installation/play-cdn) + classes adapted from the licensed kits following the provided v4 rules (gap-*, text-base/7, no leading-*, bg-red-500/60, etc.).
- Optional later: a minimal HTTP server in C that serves the demo/ dir on GET /.
- This fulfills the requirement that "the user interface for our software 'Hush' will be displayed using TailwindCSS Plus".

## Risks Updated
- Parser: will decompose aggressively (tokenize, parse_array, parse_event_object as separate leaves).
- Bounds: every string has MAX_ const; loops bounded by MAX_FILTERS=8 etc.
- No recursion.

## Build Plan
- hush-c/Makefile
- Targets: all, test, clean, demo (copy or echo html)
- CFLAGS = -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -O2 -Iinclude
- Link: -lssl -lcrypto if using OpenSSL (guarded by #ifdef HUSH_USE_OPENSSL, else pure sha stub)

## Next Concrete Plan
See HUSH_C_RDAP_PLAN.md (this file updated in P1 gate). All subsequent phases use the skeleton from c-standard §15.
Every module starts with the 7-part layout.
Public API prefix: hush_
Status: hush_status_t enum per module, 0 = HUSH_OK.

## Verification Performed
- Read protocol, kind, filter, event, verification, handlers (event/req/ingest).
- Compiled probe C11 + poll + (optional) sha.
- Inspected UI kits.
- Confirmed no C source to conflict with.

## References
- crates/buzz-core/src/{kind,filter,event,verification}.rs
- crates/buzz-relay/src/{protocol.rs,handlers/*.rs}
- references/c-standard.md (loaded)
- Tailwind rules in user prompt.

