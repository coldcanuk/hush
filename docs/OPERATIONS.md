# Hush operations

Operator notes for running the relay. Install and file-layout questions live in
[`CONFIGURATION.md`](CONFIGURATION.md). Screenshots live in
[`../screenshots.md`](../screenshots.md).

## Running the relay

```bash
hush-relay --no-open 10555   # default port is 10555 (HUSH_DEFAULT_PORT)
```

- The process is a server: it prints the listen URL and stays running until it
  is stopped (see [Close vs Exit](#close-vs-exit)).
- The listener binds `127.0.0.1` unless `--listen` says otherwise
  (`--listen 0.0.0.0` for the LAN).
- The HTTP API is gated by a per-hive session token
  (`$HUSH_HOME/session.token`, sent as `X-Hush-Token`); without it the API
  answers `{"ok":false,"error":"session token required"}`.
- `--open` (the default on a graphical session) launches a frameless
  standalone app window (Chromium-family `--app=` plus `--ozone-platform=x11`,
  or Epiphany application mode). The same port also speaks the
  newline-delimited Nostr JSON protocol and RFC 6455 WebSocket for stock Nostr
  clients.

## Close vs Exit

| Verb | In the hive | CLI | What happens (verified live on a cloud VM) |
|---|---|---|---|
| **Close** | chooser **Close the window** | `hush-relay --close <port>` | GUI goes away. The relay keeps listening (`POST /api/close` answers `{"ok":true,"action":"close"}` and the port keeps serving). |
| **Exit** | chooser **Exit the application** | `hush-relay --quit <port>` or `POST /api/exit` | Every process stops. `POST /api/exit` answers `{"ok":true,"action":"exit"}` and the port goes dark. |
| **Cancel** | chooser **Cancel** | — | Stay put. |

### Stopping with `--quit` (behavior as of Floor's PR #207)

On Linux, `hush-relay --quit` verifies the target process is hush-relay before
signalling it. Bare `--quit` targets port `10555` only — pass the port
explicitly (`hush-relay --quit <port>`) for relays on any other port. It exits
`0` only after the relay is confirmed stopped, and exits `1` when no relay is
running on that port.

On OpenBSD and FreeBSD, `hush-relay --quit` refuses to stop the relay (exit code 2) because it cannot verify the process identity on those systems yet. Stop the relay with Exit in the hive, Ctrl+C, or `curl -X POST http://127.0.0.1:<port>/api/exit -H "X-Hush-Token: $(cat ~/.hush/session.token)"`. Tracked in #209.

History: verified pre-#207 on base `44c66f88` (2026-09-24) — `--quit` exited
`0` while the port kept serving. #207 fixed this on Linux; Floor owns the
product change. `make clean` stops relays via `scripts/kill-relay.sh`
(SIGTERM, then SIGKILL past a grace period), which also reaps the
CHILD-mode turnserver from its pidfile but never touches the systemd
`hush-turn.service` daemon.

## Rebuild guard

`make` / `make install` refuse while a live relay owns the guard port — stop
it first (see above); Close is not enough since the hive keeps the port. The
guard is port-scoped (`scripts/check-relay-port.sh`: argument, else
`$HUSH_PORT`, else `10555`), so a relay on another port does not block the
build unless `HUSH_PORT` matches.

## Threads, streaming, and stop

- Every note is transcribed to `$HUSH_HOME/threads/<root>.log` (keyed by the
  thread's root event). Transcripts are written through `0600` temp files with
  `O_NOFOLLOW` (no symlink follow), survive restart, and are what an agent
  reads when the live in-memory ring has moved on. `GET
  /api/thread?root=<64-hex>` serves the saved turns plus the rolling brief.
- While a robot is answering, the relay streams the provider's deltas, and the
  client polls them:

| Endpoint | Body | Answer |
|---|---|---|
| `POST /api/reply` | `{"root": "<hex>", "robot": "<name or hex>"}` | `{"ok":true,"running":true,"text":"…"}` while the job is live, `running:false` once it is gone. |
| `POST /api/cancel` | same | `{"ok":true,"stopped":true}` after SIGTERM to the job's process group (SIGKILL follows past the grace period), then the robot posts an honest "stopped on request" note. A second cancel answers `stopped:false`. |

## STUN/TURN and conference calls

Hush does not vendor [coturn](https://github.com/coturn/coturn). It writes a
config and starts the `turnserver` binary when you click **Enable STUN/TURN**
in Settings.

```bash
# Debian / Ubuntu / Pop!_OS
sudo apt install coturn
```

- Generated config listens on port `3478` (`HUSH_TURN_PORT_USER` `13478` is the
  fallback when binding `:3478` needs root the child does not have) with relay
  range `49152-49251/udp` (`contrib/turnserver.conf.in`). Open the firewall for
  `3478/tcp`, `3478/udp`, and `49152-49251/udp`. Set **Public host / IP** if
  the machine is behind NAT (the template carries an
  `# external-ip=YOUR.PUBLIC.IP` line).
- **Daemon mode** (systemd, `contrib/systemd/hush-turn.service`) requires a
  system install so the unit is in `/lib/systemd/system/`:

```bash
sudo make install PREFIX=/usr
sudo systemctl enable --now hush-turn
# or open Settings → Daemon mode
```

Conference calls are a WebRTC mesh on the current channel (kind 25000
signaling; `getUserMedia` + `RTCPeerConnection` with the configured ICE
servers). Supported mixes: human↔human, many humans, human↔agent,
agent↔agent, and mixed. AI agents need a speech model such as Whisper
(`HUSH_WHISPER=1` or `whisper` on `PATH`) to hear; otherwise they join as
signaling-only. When Whisper is available, robot cards show a 1:1 Call icon
and channels show a Voice icon; mute any tile to silence it locally.
