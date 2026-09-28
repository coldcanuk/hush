# Hush configuration

Install, environment, on-disk layout, and provider setup. Day-to-day
run/stop/turn/thread questions live in [`OPERATIONS.md`](OPERATIONS.md).

## Installation

Build from source (requires `gcc`, `make`, `libssl-dev`, `libx11-dev`,
`python3` on Debian/Ubuntu):

```bash
./configure
make
make test        # ends with: ALL TESTS PASSED
make install     # ~/.local/bin, no sudo
```

`make install` installs to `~/.local/bin/` by default. `PREFIX`/`BINDIR`/
`DATADIR` re-derive from a command-line `PREFIX` (so `make install
PREFIX=/tmp/x` never leaks the `~/.local` paths baked by `./configure`), and
empty values are refused. For a system-wide install:

```bash
sudo make install PREFIX=/usr
```

Packaged installs (all build from this source):

```bash
./configure && make deb
sudo dpkg -i ../hush-relay_*.deb        # Debian, Ubuntu, Pop!_OS
```

```bash
make rpm
sudo dnf install ~/rpmbuild/RPMS/*/hush-relay-*.rpm   # Fedora, RHEL, CentOS, openSUSE
```

```bash
make flatpak                            # any distro (flatpak-builder; builds into build-dir/)
# make flatpak prints its own install hint; Hush is not verified to be on Flathub
```

```sh
# OpenBSD (pkg_add gmake first):
./configure --prefix=/usr/local
make openbsd
doas pkg_add ./dist/openbsd/hush-relay-*.tgz
# Everyday: pkg_info -aQ hush, doas pkg_add -u, doas pkg_delete hush-relay
# Details: ../openbsd/README.md
```

```sh
# FreeBSD (pkg install -y gmake first):
./configure --prefix=/usr/local
gmake freebsd
pkg add ./dist/freebsd/hush-relay-*.pkg
# Everyday: pkg search hush, pkg info hush-relay, pkg update && pkg upgrade
# Details: ../freebsd/README.md
```

## Environment variables

| Variable | Role (all verified in code) |
|---|---|
| `HUSH_HOME` | Hive home: session token, `config/`, `agents/`, `skills/`, `robots/<slug>/loadouts/` (PE-4 favorites), `threads/`. Tests override per-run; unset means `~/.hush`. |
| `HUSH_CONFIG_DIR` | Overrides the config directory (`vibe.json`, `providers.json`). Tests set it alongside `HUSH_HOME`. |
| `HUSH_PORT` | Scopes the `make` rebuild guard (`scripts/check-relay-port.sh`): argument, else `$HUSH_PORT`, else `10555`. See [`OPERATIONS.md`](OPERATIONS.md#rebuild-guard-make). |
| `HUSH_KILL_GRACE_S` | Seconds `make install` / `make clean` wait between SIGTERM and SIGKILL when stopping relays (`scripts/kill-relay.sh`, default 3). |
| `HUSH_TEST_STOP_RELAYS` | Set to `1` to run the relay-stopping phases of `make test` outside GitHub Actions. |
| `HUSH_WHISPER` | Any value other than empty or `0` marks Whisper available (otherwise an executable `/usr/bin/whisper` or `/usr/local/bin/whisper` does; `PATH` is not searched) so agents hear conference calls; gates the Call/Voice icons. |
| `HUSH_CODEX_SKILL_DIR` | Points at another complete copy of the write-legible-c skill. Missing or conflicting skill files prevent Codex dispatch. |

## On-disk layout and permissions

| Path | Contents |
|---|---|
| `$HUSH_HOME/session.token` | Per-hive API token (`X-Hush-Token`). |
| `$HUSH_HOME/config/vibe.json` (`0600`) | Named vibe, channels, projects, profile (no email), members, raised-robot labels. Never holds an nsec or provider secret. Survives rebuild / Exit. |
| `$HUSH_HOME/config/providers.json` (`0600`) | Provider overlay: host and model per id. Secret values are never in here. |
| `$HUSH_HOME/threads/<root>.log` (`0600`, `O_NOFOLLOW`) | Durable per-thread transcripts (plus `<root>.brief`); served via `GET /api/thread`. |
| `$HUSH_HOME/robots/<slug>/loadouts/` | PE-4 favorite loadouts (1–8 skill ids each, up to 32 per robot), written via `POST /api/loadout`. |
| `$XDG_RUNTIME_DIR/hush/relay-<port>.pid`, else `~/.local/state/hush/relay-<port>.pid` | Relay pidfile (`pid starttime port`) used by `--quit` and the rebuild guard. |
| `$HUSH_HOME/skills/{system,user,robots}/` | Forged skills. Product scopes are System (application-wide) and This robot; `make clean` never touches this tree. |

## Provider setup

**Configure Providers** on the KIT menu is the hive-wide desk; each robot
stores only a provider id. Secrets Hush accepts (API key, username, password,
token, passkey) live only in `pass`:

```
pass show hush/providers/<id>/api_key
pass show hush/providers/<id>/username
pass show hush/providers/<id>/password
pass show hush/providers/<id>/token
pass show hush/providers/<id>/passkey
```

`GET /api/provider` never returns the values (booleans like `has_key` only).
OAuth-CLI runtimes authenticate through their own homes, which Hush detects
but never writes or copies:

| Runtime | Authenticated means |
|---|---|
| Grok Build | nonempty `~/.grok/auth.json` (`Log in with OAuth` runs `grok login --oauth` in a terminal) |
| Codex | nonempty `~/.codex/auth.json` or `~/.codex/config.toml` (a bare `~/.codex` directory does not count) |
| Goose | `~/.config/goose/config.yaml` (official; legacy `~/.goose` probed only if present) |

## Ops doc index

| Doc | Contents |
|---|---|
| [`ops/api-status.md`](ops/api-status.md) | `/api/status` health probe |
| [`ops/package-upgrade.md`](ops/package-upgrade.md) | Packaging upgrades without surprise-kills |
| [`ops/reliability.md`](ops/reliability.md) | Reliability ops index (single-hive) |
| [`ops/store-backup.md`](ops/store-backup.md) | Store backup / restore |
| [`ops/store-bench.md`](ops/store-bench.md) | Store bench (OBSERVED baseline) |

Plus [`pass-integration.md`](pass-integration.md) for the `pass` contract.

## Codex skill environment

Codex is the supported development agent. Hush exposes the write-legible-c
skill in the isolated working directory of each Codex runtime job, and `make
install` installs the complete skill under
`share/hush/codex/skills/write-legible-c`. Select `codex` and run `codex
login`. See [`AGENTS.md`](../AGENTS.md), [`docs/CODEX.md`](CODEX.md), and
[`.agents/skills/write-legible-c`](../.agents/skills/write-legible-c/SKILL.md).
