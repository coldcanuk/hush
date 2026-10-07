# Hush operations

Operator notes for running and stopping the relay, rebuilding, and calls.
Install and file-layout questions live in [`CONFIGURATION.md`](CONFIGURATION.md).
Screenshots live in [`../screenshots.md`](../screenshots.md). `hush-relay --help`
prints the flags and a short exit-code summary; this page is more precise.

## Running the relay

```bash
hush-relay                   # default port 10555 (HUSH_DEFAULT_PORT); opens a window when a display is set
hush-relay --open            # always open the app window (the .desktop launcher runs this)
hush-relay --no-open 10555   # never open a window
hush-relay 10556 --listen 0.0.0.0   # another port, bound for the LAN
```

- Usage: `hush-relay [port] [--listen ADDR] [--open|--no-open|--close|--quit]`.
  `--listen=ADDR` works too.
- The process is a server: it prints `hush-relay <version> (<sha>)` and the
  listen URL, then stays running until it is stopped (see
  [Close vs Exit](#close-vs-exit)).
- The listener binds `127.0.0.1` unless `--listen` says otherwise.
- A window opens by default only when `DISPLAY` or `WAYLAND_DISPLAY` is set.
  `--open` launches a standalone app window: a Chromium-family browser with
  `--app=` plus `--ozone-platform=x11`, or Epiphany `--application-mode`.
- The HTTP API needs the per-hive session token: send the contents of
  `$HUSH_HOME/session.token` (`HUSH_HOME` defaults to `~/.hush`) as
  `X-Hush-Token`. Every `/api/` route needs it except `/api/status` and
  `GET /api/complete`. Without it the relay answers
  `401 {"ok":false,"error":"session token required"}`.
- The same port also speaks RFC 6455 WebSocket and the newline-delimited
  Nostr JSON protocol for stock Nostr clients (see
  [`.agents/skills/relay/SKILL.md`](../.agents/skills/relay/SKILL.md)).

## Close vs Exit

Close and Exit are two different verbs. In the hive, Close and Exit open one
chooser: **Exit the application**, **Close the window**, or **Cancel**. When
the last `--app` window closes, the relay can raise the same three choices in a
`zenity` dialog (when `zenity` is installed).

| Verb | In the hive / HTTP | CLI | What happens |
|---|---|---|---|
| **Close** | chooser **Close the window**; `POST /api/close` answers `{"ok":true,"action":"close"}` | `hush-relay --close [port]` | The window goes away and the relay keeps listening on its port. The CLI `--close` only prints `GUI closed. Relay still running on http://127.0.0.1:<port>/.` and exits 0; it does not signal or change anything. |
| **Exit** | chooser **Exit the application**; `POST /api/exit` answers `{"ok":true,"action":"exit"}` | `hush-relay --quit [port]`, Ctrl+C | The relay shuts down (`POST /api/exit` sets the same shutdown flag as SIGTERM/SIGINT). On the way out it stops the children it forked (each tracked child gets SIGTERM, then SIGKILL if it outlives the wait), then, **on Linux only**, runs a `/proc` sweep that stops any process whose command line has `--class=hush-relay` and this port's `--app=http://127.0.0.1:<port>/` (matched by command line, not parentage), stops its CHILD-mode turnserver, and removes its pidfile. |
| **Cancel** | chooser **Cancel** | — | Stay put. |

Click the launcher (`hush-relay --open`) while the hive is up to re-attach a
window. Close is not enough before a rebuild: the hive keeps the port.

### Stopping with `--quit`

`hush-relay --quit [port]` stops the relay on one port. Bare `--quit` means port
`10555` only; pass the port for any other relay. The **Quit Hush** action in
`hush-relay.desktop` runs bare `hush-relay --quit`, so it also targets 10555
only.

It finds the relay through the pidfile the relay wrote at start-up:
`$XDG_RUNTIME_DIR/hush/relay-<port>.pid` when `XDG_RUNTIME_DIR` is an absolute
path, else `$HOME/.local/state/hush/relay-<port>.pid`. There is no `/tmp`
fallback. If the relay could not write that file it warns
`no pidfile for port <port>; --quit will not find this relay` at start-up.

On Linux, FreeBSD, and OpenBSD the pidfile holds `pid starttime port`.
Elsewhere it holds only the pid. `--quit` signals only a live pid whose
recorded start time and port both match. Linux reads that start time from
`/proc` and does not check the executable name, so a rebuilt binary under a
running relay still matches. FreeBSD and OpenBSD read the start time from the
kernel process table and also require the caller's uid and an executable name
that starts with `hush-relay`. It sends one SIGTERM, then waits up to 20 s
(200 checks, 100 ms apart) for the relay to exit. It never escalates to
SIGKILL. It never signals pid 1 or below.

| Exit code | Meaning (from the code) |
|---|---|
| `0` | The relay was confirmed gone: `hush-relay: stopped relay on port <port> (pid <pid>)`. |
| `1` | Nothing to stop: no pidfile, or the recorded pid is dead (that stale pidfile is removed). |
| `2` | Stop failed or refused: the pid does not match the recorded start time or port (`pid <pid> is not the relay on port <port>; refusing --quit`); an old pid-only pidfile (`old/unrecognized format ... cannot verify identity`, with `ps`/`/proc` hints); an unreadable or malformed pidfile; or the relay survived the wait. |

On any other platform `--quit` never signals: there is no process-identity
check. It exits **1** when there is no pidfile or the recorded pid is dead,
and **2** in every other case: a live recorded pid (refused with a message
naming `POST /api/exit`, the `X-Hush-Token` header and the token file), a
recorded pid of 1 or less or its own pid, or an unreadable or malformed
pidfile. Stop those relays with Exit in the hive, Ctrl+C, or:

```bash
curl -X POST http://127.0.0.1:<port>/api/exit \
  -H "X-Hush-Token: $(cat ~/.hush/session.token)"   # $HUSH_HOME/session.token if HUSH_HOME is set
```

History: before #207, `--quit` on base `44c66f88` exited 0 while the port
kept serving (observed on a cloud VM on 2026-09-24). #207 fixed that on Linux.

## Rebuild guard (`make`)

`make` (the default goal, `all`) first runs `scripts/check-relay-port.sh` and
refuses to build over a live hive. `make install`, `make test` and
`make -C hush-c` do not run the guard.

- Port: `$HUSH_PORT`, else `10555` (e.g. `HUSH_PORT=10556 make`). The script
  itself also takes the port as its first argument.
- A live pid in that port's pidfile (same path rules as `--quit`) whose
  process name is `hush-relay` owns the port. The guard exits 1 and prints the
  pid and `hush-relay --quit <port>`.
- With no pidfile owner, the guard looks for any running process named exactly
  `hush-relay`. If there is none, it passes, even without curl. If there is one,
  it needs curl to attribute the port:
  - with curl, a 2xx from `http://127.0.0.1:<port>/api/status` blocks the
    build (exit 1); a relay on another port does not;
  - without curl, it cannot probe, so it fails with exit 2
    (`hush-relay is running but its port cannot be probed (curl missing)`),
    whatever port that relay uses. Install curl or quit the other relays first.
- The guard never kills anything. Its refusal text points at
  `docs/OPERATIONS.md` "Close vs Exit", the [Close vs Exit](#close-vs-exit)
  section on this page.

## `make install` and `make clean` stop relays

`make install` does not refuse; it stops relays first (`scripts/kill-relay.sh
stop`, #221/#222):

- Candidates are live processes of your uid whose executable basename starts
  with `hush-relay`: renamed copies such as `hush-relay-m11-<sha>`, replaced
  (`(deleted)`) binaries and relays on any port. Under `sudo`, the invoking
  user's (`SUDO_UID`) relays are included too. Other users' relays only get a
  `note:` line.
- Each gets SIGTERM, then SIGKILL after `HUSH_KILL_GRACE_S` seconds (default
  3). Every pid and path stopped is printed. It also reaps the relay's
  CHILD-mode turnserver from its pidfile. It never touches the systemd
  `hush-turn.service` daemon.
- It fails (exit 1) if a relay survives or cannot be verified, or if `make`
  itself runs under a hush-relay.
- `DESTDIR` staging installs (deb/rpm/flatpak builds) skip the stop.

`make clean` runs the same stop, then closes Hush app windows whose own command
line carries `--class=hush-relay*`. A window handed off into your
already-running browser is reported and left alone. Then it removes stale
renamed `hush-relay-*` copies from `BINDIR`, uninstalls the `PREFIX` artifacts
(`make uninstall`) and scrubs the build tree. `~/.hush` (and `HUSH_HOME`) is
never touched.

`make test` runs the signal/window phases of the stop checks
(`tests/check_stop_relays.sh`, the kill-relay part of
`tests/check_make_guard.sh`) only under GitHub Actions (`CI=true` and
`GITHUB_ACTIONS=true`) or with `HUSH_TEST_STOP_RELAYS=1`, because they stop
your relays. `CI=true` alone is not enough.

## Threads, streaming, and stop

- Every note is appended to `$HUSH_HOME/threads/<root>.log` (keyed by the
  thread's root event; `0600`, opened with `O_NOFOLLOW`). Each robot reply
  rolls the thread's brief forward in `<root>.brief`, written through a `0600`
  temp file and a rename. Both survive restart and are what an agent reads when
  the live in-memory ring has moved on. `GET /api/thread?root=<64-hex>` serves
  the saved turns plus the brief.
- While a robot is answering, the relay keeps the provider's partial answer,
  and the client polls it:

| Endpoint | Body | Answer |
|---|---|---|
| `POST /api/reply` | `{"root": "<hex>", "robot": "<name or hex>"}` | `{"ok":true,"running":true,"text":"…"}` while the job is live; `{"ok":true,"running":false,"text":""}` once it is gone. |
| `POST /api/cancel` | same | `{"ok":true,"stopped":true}` after SIGTERM to the job's process group (SIGKILL follows after 3 s if it has not exited); the robot then posts a "stopped on request" note. With no live job it answers `{"ok":true,"stopped":false}`. |

Both answer `400 {"ok":false,"error":"root and robot are required"}` when a
field is missing.

## STUN/TURN and conference calls

Hush does not vendor [coturn](https://github.com/coturn/coturn). It writes a
config and starts the `turnserver` binary when you click **Enable STUN/TURN**
in Settings.

```bash
# Debian / Ubuntu / Pop!_OS
sudo apt install coturn
```

- **Child mode** (the Settings button): the relay writes its own config with
  `listening-port` 3478 when it runs as root, else 13478
  (`HUSH_TURN_PORT_USER`), and relay range `49152-49251`. It adds
  `external-ip=` only when you set **Public host / IP** (for NAT). Open the
  firewall for the listening port (tcp and udp) and `49152-49251/udp`.
- **Daemon mode** (systemd, `contrib/systemd/hush-turn.service`) needs a system
  install. `make install` puts the unit in `/lib/systemd/system/` and seeds
  `/etc/hush/turnserver.conf` from `contrib/turnserver.conf.in` (port 3478,
  same relay range, a commented `# external-ip=YOUR.PUBLIC.IP` line) only when
  it runs as root or with `DESTDIR`. An existing `/etc/hush/turnserver.conf` is
  kept. `ENABLE_STUN_TURN=0` skips all of this.

```bash
sudo make install PREFIX=/usr
sudo systemctl enable --now hush-turn
# or open Settings → Daemon mode
```

Conference calls are a WebRTC mesh on the current channel (kind 25000
signaling through `POST /api/signal`; `getUserMedia` + `RTCPeerConnection` with
the configured ICE servers). AI agents need Whisper to hear. The relay treats
Whisper as available when `HUSH_WHISPER` is set to anything other than empty or
`0`, or when `/usr/bin/whisper` or `/usr/local/bin/whisper` is executable. It
does not search `PATH`. Without it, agents join as signaling-only. When Whisper
is available, robot cards show a 1:1 Call icon and channels show a Voice icon;
mute any tile to silence it locally.
