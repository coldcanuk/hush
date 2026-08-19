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
On first launch a feather splash detects identity and vibe, then a
numbered wizard walks Identity → Backup (`pass` **checked by default**;
retrieve with `pass show hush/identity/nsec`) → Vibe (public or private)
→ Meet **Sgt Major Payne**. Profile holds first/last name, email,
organization, theme (`dark` / `light` / `color-blind` / `dracula` /
`desert` / `monochrome` / `christmas`), and Logout. From the hive you
can create channels and projects, invite humans, and raise agents
(plaintext/Markdown context only). Edit-robot actions sit on one
compact line (Save Robot / Close / Delete Robot). `@` in the composer mentions humans
and robots as pills; the wire still uses NIP-27 `nostr:npub1…`.
Channels carry a UUID, sit in optional Groups (NIP-29 parent), and can
be deleted or managed from a right-click menu. Manage Channel adds and
removes people with `+` / `−` pills and sets a **policy leash** (open /
humans / robots / mixed; robots reply off, when mentioned, or confirm
first). Chatty multi-send bursts coalesce; robots confirm they heard
the ask before spending a Grok turn. Install, Profile, Settings, Call,
Close, and Exit live on a movable tool rail that collapses to a
hamburger. Install puts Hush on the app launcher as its own window; it
does not start a second hive. When Whisper is on PATH (or
`HUSH_WHISPER` is set), robot cards show a 1:1 Call icon and channels
show a Voice icon; mute any tile in the conference. After Grok/Codex
OAuth the matching provider box shows authenticated (Grok needs
`~/.grok/auth.json`, Codex needs `~/.codex/auth.json` or `config.toml`)
and tells you to close the extra windows. Mention a Grok Build robot
to start a thread; a thinking chip shows while it works, a Thread
button opens a resizable hive chat (1:1 or 1:n). The tool rail is a
free-drag hamburger (no docks); double-click parks it left of the
brand. The reply is one short `grok -p` note from an empty cwd (no
desktop AGENTS.md).
Click **relay live** for stored / projects / sockets. Hive metadata persists in `~/.config/hush/vibe.json` so
`make clean && make install` or Exit does not force a new vibe after
you import the same nsec. **Exit** (`--quit`) stops the relay and the
browser / login children it forked. **Close** leaves the hive standing.
If `--open` attaches to a leftover listener, quit that process before a
new install can take the port. Secrets stay in `pass`. See
[docs/pass-integration.md](docs/pass-integration.md).

Install it from the tool rail (Chromium “Install”, or iOS Share → Add to Home Screen)
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

The process is a server: it prints the listen URL and stays running until
**Exit**, `--quit`, or Ctrl+C. `--open` (the default on a graphical session)
launches a **standalone app window** (Chromium/Chrome/Brave/Edge `--app=`,
or Epiphany application mode) with no browser tab strip or URL bar.
Firefox-as-default is not used, because it cannot hide chrome. The same port
also speaks the newline-delimited Nostr JSON protocol (see
`.goose/skills/relay/SKILL.md`).

### Close vs Exit

These are two different verbs. Rail **Close** and **Exit** open one
chooser: **Exit the application**, **Close the window**, or **Cancel**.
The OS/PWA window `×` belongs to the `--app` window. Hush cannot put
those three buttons on that close-box. Closing the last live `--app`
window raises a follow-up (`zenity` when present): the same three
verbs. Launch does not raise that dialog. Cancel re-opens the window.
Close leaves the hive standing.

| Verb | In the hive | CLI | What happens |
|---|---|---|---|
| **Close** | chooser **Close the window** | `hush-relay --close` | GUI goes away. The relay keeps listening. |
| **Exit** | chooser **Exit the application** | `hush-relay --quit` or Ctrl+C | Every process stops. Exit code 0. |
| **Cancel** | chooser **Cancel** | — | Stay, or re-attach if the `--app` window already closed. |

Click the launcher (`hush-relay --open`) while the hive is already up to
re-attach a window. `POST /api/close` acknowledges Close and does not stop
the process. `POST /api/exit` sets the same shutdown flag as SIGTERM.

### Provider configure

Selecting an AI provider on **Raise a robot** reveals a pencil. That
opens a tailored drawer:

- Grok Build and Codex are OAuth-only: **Log in with OAuth** starts
  `grok login --oauth` or `codex login` in a terminal. Authenticated
  means that provider’s own auth file exists — a leftover `~/.codex`
  directory does not count. Hush does not implement the browser dance
  and does not write `~/.grok` or `~/.codex`.
- Goose reuses `~/.config/goose` or accepts an override key.
- Gemini / xAI / OpenAI / Anthropic / Deepseek take an API key, host
  URL, and a scanned or typed model. Those fields use the same `+` /
  `−` pills as the robot name and system prompt. Deepseek host is
  `https://api.deepseek.com`.
- Cline shows an honest empty state if the editor extension is missing.

Secrets Hush accepts for a provider (API key, username, password,
token, passkey) live only in `pass`:

```
pass show hush/providers/<id>/api_key
pass show hush/providers/<id>/username
pass show hush/providers/<id>/password
pass show hush/providers/<id>/token
pass show hush/providers/<id>/passkey
```

Host and model live in `~/.config/hush/providers.json`.
The named vibe, channels, projects, profile (no email), members, and
raised-robot labels live in `~/.config/hush/vibe.json` (0600).
`make clean` only deletes build products; it does not touch that
directory. Tests set `HUSH_CONFIG_DIR`.
`GET /api/provider` never returns the values. Goose / Grok / Codex
home secrets stay in those homes and are never copied.
Cline authenticates with ClinePass or a bring-your-own provider key,
not a Grok/Codex-style OAuth-first CLI.

The application launcher entry (`hush-relay.desktop`) starts or attaches the
GUI. The **Quit Hush** desktop action runs `--quit`.

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

## Docs

Plans and research live under `docs/`. Do not leave `PLAN_*.md` or `RESEARCH*.md` at the repo root.

| Kind | Path |
|------|------|
| Plans | [`docs/plan/`](docs/plan/) |
| Research | [`docs/research/`](docs/research/) |
| `pass` | [`docs/pass-integration.md`](docs/pass-integration.md) |

## Skills for Goose

Core skills in `.goose/skills/`:
- worktree, c-build, c-test, legible-c, relay, goose-init, publish

## Code of Ethics

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) — SQLite's Code of Ethics (Rule of St. Benedict).

