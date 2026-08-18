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
- Same port also serves the chat **PWA** over HTTP (`GET /`, manifest, service worker, icons)
- Optional **STUN/TURN** (coturn) from Settings, including systemd daemon mode
- Vibes are **public** (discoverable) or **private** (join token)
- Mesh **conference calling** (humans and AI agents; agent voice needs Whisper)
- Strict build: `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`

The chat UI is a Progressive Web App served by the relay itself
(`hush-c/demo/`: `index.html`, `manifest.webmanifest`, `sw.js`, icons).
On first launch it asks for a Nostr identity (create or import `nsec1…`),
backs up the key in a modal (unix `pass` **checked by default**; retrieve
with `pass show hush/identity/nsec`), names this relay as a **vibe**,
and seeds **Sgt Major Payne** plus starter channels. From the hive you can
create more channels and a basic git project. See
[docs/pass-integration.md](docs/pass-integration.md).

Install it from the browser (Chromium “Install”, or iOS Share → Add to Home Screen)
while the relay is running at `http://127.0.0.1:<port>/`.

Importing identities and channels from the predecessor project? See [IMPORT.md](IMPORT.md).

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

STUN/TURN support is **on by default**. To omit it:

```bash
./configure --disable-stun-turn
make
```

## STUN/TURN and conference calls

Hush does not vendor [coturn](https://github.com/coturn/coturn). It writes a
config and starts the `turnserver` binary when you click **Enable STUN/TURN**
in Settings.

```bash
# Debian / Ubuntu / Pop!_OS
sudo apt install coturn
```

- **Child mode** (no root): `hush-relay` forks `turnserver` on port 3478
  (root) or 13478 (user) with a generated long-term username/password.
- **Daemon mode** (systemd): requires a system install so the unit is in
  `/lib/systemd/system/`:

```bash
sudo make install PREFIX=/usr
# then either:
sudo systemctl enable --now hush-turn
# or open Settings → Daemon mode
```

Open the firewall for `3478/tcp`, `3478/udp`, and the relay range
`49152-49251/udp`. Set **Public host / IP** if the machine is behind NAT.

A **vibe** (this relay) can be public or private. Public vibes are
discoverable and joinable. Private vibes hide from discovery and share a
join token.

Conference calls are a WebRTC mesh on the current channel (kind 25000
signaling). Supported mixes: human↔human, many humans, human↔agent,
agent↔agent, and mixed. AI agents need a speech model such as Whisper
(`HUSH_WHISPER=1` or `whisper` on `PATH`) to hear; otherwise they join as
signaling-only.

## Run

```bash
./hush-relay          # default port 10555; opens a standalone app window if a display is available
./hush-relay --open   # same, and always open the app window (used by the .desktop launcher)
./hush-relay --no-open 10555
```

The process is a server: it prints the listen URL and stays running until Ctrl+C.
`--open` (the default on a graphical session) launches a **standalone app
window** (Chromium/Chrome/Brave/Edge `--app=`, or Epiphany application mode)
with no browser tab strip or URL bar. Firefox-as-default is not used, because
it cannot hide chrome. The same port also speaks the newline-delimited Nostr
JSON protocol (see `.goose/skills/relay/SKILL.md`).

The application launcher entry (`hush-relay.desktop`) starts the relay **without**
a terminal window and opens that app window. Clicking the icon again while the
relay is already running just reopens the window.

## Installation

Hush is available through multiple package managers. Choose the one that fits your distribution:

### DEB (Debian, Ubuntu, Pop!\_OS, etc.)

```bash
# Build from source
./configure
make deb
# Install the resulting .deb
sudo dpkg -i ../hush-relay_*.deb
```

### RPM (Fedora, RHEL, CentOS, openSUSE, etc.)

```bash
# Build from source
make rpm
# Install the resulting .rpm
sudo dnf install ~/rpmbuild/RPMS/*/hush-relay-*.rpm
```

### Flatpak (Any Linux distribution)

```bash
# Build from source
make flatpak
# Or install from Flathub (when available)
flatpak install flathub io.github.coldcanuk.hush
```

### OpenBSD (`pkg_add`)

```sh
# On OpenBSD (pkg_add gmake first):
./configure --prefix=/usr/local
make openbsd
doas pkg_add ./dist/openbsd/hush-relay-*.tgz

# Or drop the port into the ports tree:
#   doas cp -R openbsd/net/hush-relay /usr/ports/net/hush-relay
#   cd /usr/ports/net/hush-relay && make makesum && make package
```

Everyday commands ([FAQ 15](https://www.openbsd.org/faq/faq15.html)):
`pkg_info -aQ hush`, `doas pkg_add -u`, `doas pkg_delete hush-relay`.
Details: [openbsd/README.md](openbsd/README.md).

### FreeBSD (`pkg`)

```sh
# On FreeBSD (pkg install -y gmake first):
./configure --prefix=/usr/local
gmake freebsd
pkg add ./dist/freebsd/hush-relay-*.pkg

# Or drop the port into the ports tree:
#   cp -R freebsd/net/hush-relay /usr/ports/net/hush-relay
#   cd /usr/ports/net/hush-relay && make makesum && make package
```

Everyday commands ([pkg reference](https://www.freebsdsoftware.org/blog/freebsd-pkg-reference.html)):
`pkg search hush`, `pkg info hush-relay`, `pkg update && pkg upgrade`,
`pkg delete hush-relay`.
Details: [freebsd/README.md](freebsd/README.md).

### From Source (all platforms)

```bash
./configure
make
make install
```

`make install` installs to `~/.local/bin/` by default — no `sudo` required.

For a system-wide install:

```bash
./configure
make
sudo make install PREFIX=/usr
```

## Skills for Goose

Core skills in `.goose/skills/`:
- worktree, c-build, c-test, legible-c, relay, goose-init, publish

## Code of Ethics

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) — SQLite's Code of Ethics (Rule of St. Benedict).

