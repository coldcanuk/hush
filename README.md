# Hush

**Hush** is a lightweight, legible C11 implementation of core Nostr relay functionality.

It provides a minimal, auditable, machine- and human-legible relay for Nostr events (focus on chat kinds, EVENT/REQ/CLOSE handling, simple filter matching, and bounded in-memory storage).

- Written in strict C11 following the machine-legibility standard.
- Single binary: `hush-relay`
- Designed for set-and-forget self-hosting and easy embedding.
- **License: GPLv3**

## Features (MVP)

- Nostr NIP-01 basics
- EVENT ingestion + bounded store
- REQ with filter matching (kinds, authors, ids, since/until, #h channel tag)
- CLOSE support
- Simple TCP newline-JSON protocol
- `poll(2)` single-threaded server
- Strict build: `-Wall -Wextra -Werror -Wconversion -Wshadow -std=c11`

A small TailwindCSS demo UI is included in `hush-c/demo/index.html`.

## License

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

See the file `LICENSE` for the full GPLv3 text.

## Requirements

- C11 compiler (gcc or clang)
- GNU make (or gmake on *BSD)
- POSIX headers + `poll(2)`, sockets
- (Optional) pkg-config and build tools

### Optimal on Pop!_OS / Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y build-essential pkg-config
```

## Standard Workflow

```bash
# 1. Configure (detects OS, checks tools, special Pop!_OS hints)
./configure

# Or with custom prefix (recommended for most users)
PREFIX=$HOME/.local ./configure

# 2. Build
make

# 3. Run tests
make test

# 4. Install (system)
sudo make install

# Or user-local (recommended)
make install PREFIX=$HOME/.local

# Refresh application launcher (Linux)
update-desktop-database $HOME/.local/share/applications || true
```

After `make install` you will have:

- `hush-relay` in `$PREFIX/bin`
- `hush-relay.desktop` in `$PREFIX/share/applications` (appears in your application launcher / menu)

To uninstall:

```bash
sudo make uninstall
# or for user install
make uninstall PREFIX=$HOME/.local
```

## Running

```bash
hush-relay            # listens on TCP 10555
hush-relay 12345      # custom port
```

Connect with newline-delimited JSON arrays:

```
["EVENT",{"id":"...","pubkey":"...","kind":1,"content":"hello","created_at":1720000000}]
["REQ","sub1",{"kinds":[1],"#h":["general"]}]
["CLOSE","sub1"]
```

See `hush-c/demo/index.html` for a browser-based mock UI.

## Building from a clean tree

```bash
git clone https://github.com/coldcanuk/hush.git
cd hush
./configure
make
make test
make install PREFIX=$HOME/.local
```

## Cross platform

- Linux (including Pop!_OS with optimal hints)
- FreeBSD, OpenBSD, NetBSD (via `gmake` + `uname` detection in `./configure`)

## Development & Legibility

All C code follows the write-legible-c standard:

- Exact file layout
- One job per function, ≤40 lines, nesting depth ≤2
- Explicit status returns, every call checked
- Named constants for all bounds
- No recursion, bounded loops only

See `HUSH_C_RDAP_PLAN.md`, `RESEARCH.md`, and `HUSH_ARCHITECTURE.md`.

## Contributing

- Keep changes small and atomic.
- All C changes must pass the legible-C pre-delivery checklist.
- Run `./configure && make && make test` before committing.
- Update README / docs when behavior or build steps change.

Enjoy a clean, auditable Nostr relay.
