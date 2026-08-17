# Hush

**Hush** is a lightweight, legible C11 implementation of core Nostr relay functionality.

Optimized for the Goose AI agent. All development uses worktrees inside the repository.

- Written in strict C11 following the machine-legibility standard (write-legible-c).
- Single binary: `hush-relay`
- Designed for set-and-forget self-hosting and embedding.
- **License: GPLv3**

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

See [AGENTS.md](AGENTS.md) and [BRANCHING.md](BRANCHING.md).

All work:
- Starts from clean `main`
- Uses `git worktree add -b gb/<slug> worktrees/<slug>` (inside repo)
- Commits after every Milestone
- Merges --no-ff to main, then removes the worktree

No orphaned worktrees or branches.

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

