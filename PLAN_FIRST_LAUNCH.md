# PLAN: First-launch UX (RDAP)

Branch: `gb/first-launch-ux`  
Worktree: `worktrees/first-launch-ux`  
Base: `b11570db6` (main, PWA)

## Scope

See RESEARCH.md §2026-08-17 first-launch. Copy Buzz’s **sequence**, not its stack.

## Phase 0 — Isolation

- [x] M0.1 worktree `gb/first-launch-ux` on clean main.

## Phase 1 — Research

- [x] M1.1 Buzz onboarding + community + welcome + identity APIs.
- [x] M1.2 Nostr keys, relays, NIPs (19/07/29/34/42/90), kinds registry, DVMs.
- [x] M1.3 Hush PWA/HTTP/store/`pass` contract.
- [x] M1.4 Synthesize RESEARCH.md + this plan. **Gate.**

## Phase 2 — Architecture

- M2.1 Lock routes, modules, session JSON, Payne seed (this file + RESEARCH).

## Phase 3 — Implementation

- M3.1 `hush_bech32` + NIP-19 vector tests.
- M3.2 `hush_identity` (OpenSSL secp256k1) + `scripts/hush-pass`.
- M3.3 `hush_launch` session / vibe / channel / project / Payne.
- M3.4 HTTP routes + session pubkey on `POST /api/event`.
- M3.5 PWA first-launch gate + hive create flows.
- M3.6 Tests (`check_launch.sh`) + README.

## Phase 4 — Verify / land

- M4.1 `./configure && make && make test`.
- M4.2 Push, PR, auto-merge, delete worktree.

## Task notes

Every C change follows write-legible-c. No `-Werror` carve-outs. Link `-lcrypto`.
