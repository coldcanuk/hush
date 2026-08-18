# Hush

<img src="assets/icons/256x256/hush-relay.png" width="72" height="72" alt="Hush">

**Version: 0.0.1**

**Hush** is a lightweight, legible C11 implementation of core Nostr relay functionality.

Optimized for the Goose AI agent. All development uses worktrees **inside this repository** (`worktrees/<slug>`), never under `/opt/repo/worktrees` or other external paths.

- Written in strict C11 following the machine-legibility standard (write-legible-c).
- Single binary: `hush-relay`
- Designed for set-and-forget self-hosting and embedding.
- **License: GPLv3**
- Version source of truth: top-level `VERSION` (currently `0.0.1`)

## Features (MVP)

- Nostr NIP-01 basics for chat (kinds 0,1,5,7,9)
- EVENT ingestion + bounded in-memory store
- REQ with filter matching (kinds, authors, ids, since/until, #h)
- CLOSE
- Simple TCP newline-delimited JSON protocol (MVP; WebSocket adapter later)
- `poll(2)` single-threaded server
- Strict build: `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`

A small TailwindCSS demo UI is in `hush-c/demo/index.html`.

## Goose + Worktree (Prime Directive)

**Law:** [PRIME_DIRECTIVE.md](PRIME_DIRECTIVE.md) — also [AGENTS.md](AGENTS.md), [BRANCHING.md](BRANCHING.md).

1. Always create a worktree: `git worktree add -b gb/<slug> worktrees/<slug>` (inside this repo only).
2. Commit and **push** on that `gb/*` branch.
3. Land on `main` **only** via Pull Request → review → auto-merge.
4. After merge, delete the worktree.
5. **Writing directly to `main` is strictly prohibited.**

```bash
./scripts/install-hooks.sh   # blocks commit/push on main
```

## Build

```bash
./configure
make
make test
```

## Run

```bash
./hush-relay 10555
```

## Skills for Goose

Core skills in `.goose/skills/`:
- worktree, c-build, c-test, legible-c, relay, goose-init, publish

## Code of Ethics

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) — SQLite's Code of Ethics (Rule of St. Benedict).

