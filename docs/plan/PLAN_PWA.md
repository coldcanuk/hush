# PLAN: Hush UI as PWA (RDAP)

Branch: `gb/pwa`  
Worktree: `worktrees/pwa`  
Base: `f653be9` (main)

## Scope

- Primary: installable PWA shell for the existing hush-relay HTTP UI.
- Non-goals: push, auth, Node frontend, HTTPS in-process, offline POST.
- DoD: routes in RESEARCH.md §Success; `make && make test`; curl 200s.

## Phase 0 — Isolation

- [x] M0.1 worktree `gb/pwa` on clean main.

## Phase 1 — Research

- [x] M1.1 MDN + Chrome installability, current HTTP/embed/icons.
- [x] M1.2 Synthesize `RESEARCH.md` + this plan. **Gate.**

## Phase 2 — Architecture

- M2.1 Lock routes, embed format, SW cache policy (this file + RESEARCH).

## Phase 3 — Implementation

- M3.1 Rasterize 180/192/512 PNG from `assets/icons/256x256/hush-relay.png`.
- M3.2 `demo/manifest.webmanifest`, `demo/sw.js`, HTML head + SW register.
- M3.3 Multi-asset `scripts/embed-ui.sh` + Makefile deps.
- M3.4 `hush_http.c` static routes (legible C11).
- M3.5 README + test_proto or curl smoke in `make test` helper.

## Phase 4 — Verify / land

- M4.1 `./configure && make clean && make && make test` + curl against hush-relay.
- M4.2 Push `gb/pwa`, open PR, merge, delete branch.

## Task notes

Every C change follows write-legible-c. Reuse `hush_http_write_all`. No `-Werror` carve-outs.
