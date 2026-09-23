# `/api/status` healthprobe

`GET /api/status` is the operator healthprobe for `hush-relay`
(liveness/readiness style). It is read-only, needs no session token,
and is already what the test harness polls while waiting for the relay
to come up (`hush-c/tests/check_pwa.sh`, `check_win.py`,
`check_browser_launch.py`). No new surface: this doc only records the
OBSERVED behavior so ops can probe from the doc alone.

## Endpoint (OBSERVED)

Per `hush-c/src/hush_http.c` (`hush_http_serve`) and
`hush-c/src/api_status.c` (`hush_http_serve_status`):

- `GET /api/status` → `200 OK`, `Content-Type: application/json`.
- Only `GET` is served. `POST /api/status` falls through to the POST
  dispatcher, which has no `/api/status` branch and answers
  `404 Not Found` (`text/plain`, `not found`); other non-`GET` methods
  likewise 404 (`OPTIONS` answers the generic `204 No Content`). Probe
  with `GET` only.
- Auth: **none required**. `hush_http_needs_auth` exempts `/api/status`
  (and only it plus one-shot `GET /api/complete?t=<job-token>`); see
  also `SECURITY.md` ("every `/api/*` route requires it except
  `/api/status` (liveness) …"). Do not send the session token from
  probes; a leaked probe log must not carry credentials.
- Host allowlist: **still enforced**. `hush_http_guard` runs before the
  auth exemption, so a request with a foreign or missing `Host` answers
  `403 Forbidden` (`text/plain`, `bad host`) even without a token.
  Locked in by `hush-c/tests/check_trust.py` (`check_host_allowlist`):
  loopback `Host` values answer 200, `evil.com` /
  `127.0.0.1.evil.com` / missing `Host` answer 403.
  With the default loopback bind, `Host` must be (port optional)
  `127.0.0.1`, `localhost`, `::1`, or `[::1]`
  (`hush_http_host_is_loopback`). Plain `curl http://127.0.0.1:<port>/…`
  sets this correctly on its own. A non-loopback `--listen` address
  makes the Host check advisory instead — see `SECURITY.md`
  ("Operator checklist").

## Success body (OBSERVED)

`hush_http_serve_status` formats exactly this shape
(`hush-c/src/api_status.c`):

```json
{"ok":true,"version":"0.0.1","events":12,"clients":0,"port":10555,
 "whisper":false,"turn_running":false,"vibe_public":true,"thinking":[]}
```

| Field | Meaning |
|---|---|
| `ok` | Always `true` on a served status reply. Gate probes on this. |
| `version` | Relay version string (`HUSH_VERSION`, currently `0.0.1`). Informational. |
| `events` | Event-store count (`hush_store_count`). Grows as notes arrive. |
| `clients` | Current connection count. Informational. |
| `port` | Listen port the relay reports (`hush_http_listen_port`). Echo, not a promise — probe the port you configured. |
| `whisper` | Speech-to-text availability. Informational; do not gate on it. |
| `turn_running` | Whether the TURN relay is running. Informational; do not gate on it. |
| `vibe_public` | Vibe visibility flag (true when no vibe is set). Informational; do not gate on it. |
| `thinking` | Agent-job array from `hush_agent_status` (`hush-c/src/hush_agent.c`): `[]` when idle, non-empty while a robot job runs. A busy relay is still healthy — **do not treat non-empty `thinking` as unhealthy**. |

## Unhealthy (treat as down)

Only what the code actually does:

- **Connection refused / timeout**: nothing is listening (relay down or
  wrong port). Unhealthy.
- **Non-2xx**: `403` means the probe's `Host` header is not loopback
  (fix the probe URL, not the relay); `404` means the wrong method or
  path (use `GET /api/status` exactly — query strings are stripped, but
  keep the probe canonical). `401` never comes from this path: it is
  auth-exempt, so a `401` on the probe URL means you are hitting a
  different route.
- **Empty / malformed body**: a `200` whose body is missing or does not
  parse as JSON with `"ok":true` is not a pass. With `curl -sf` (below)
  the transport failure already fails the probe; check the body too.

## Probe

Default bind is `127.0.0.1`, default port `10555`
(`HUSH_DEFAULT_PORT` in `hush-c/include/hush_relay.h`; `hush-relay
[port] [--listen ADDR]` in `hush-c/src/hush_relay_main.c`). Adjust the
port when the relay was started with an explicit one.

```bash
curl -sf http://127.0.0.1:10555/api/status
```

Readiness wait in the same style as `hush-c/tests/check_pwa.sh`:

```bash
port=${1:-10555}
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/status" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
```

systemd (`ExecStartPost` blocks the unit until the relay answers):

```ini
ExecStartPost=/bin/sh -c 'for i in $(seq 1 100); do curl -sf http://127.0.0.1:10555/api/status >/dev/null 2>&1 && exit 0; sleep 0.1; done; exit 1'
```

Kubernetes (exec form, so `Host: 127.0.0.1` is sent and the allowlist
passes even on the default loopback bind — a plain `httpGet` sends the
pod IP as `Host` and would answer 403):

```yaml
livenessProbe:
  exec:
    command: ["curl", "-sf", "http://127.0.0.1:10555/api/status"]
  initialDelaySeconds: 2
  periodSeconds: 10
readinessProbe:
  exec:
    command: ["curl", "-sf", "http://127.0.0.1:10555/api/status"]
  initialDelaySeconds: 1
  periodSeconds: 5
```

## Not this endpoint

- `POST /api/exit` answers `{"ok":true,"action":"exit"}` and **stops the
  relay** (same shutdown flag as SIGTERM; see `hush_http_serve_exit` in
  `hush-c/src/hush_http.c` and `docs/ops/package-upgrade.md`). Never
  use it as a probe.
- `POST /api/close` answers `{"ok":true,"action":"close"}` and only
  detaches the GUI; the relay keeps listening. It is not a health
  signal either.
- Both are token-gated (`401` without the session token — verified live
  for `/api/close`), unlike the auth-exempt status probe.
- `GET /api/session` and `GET /api/events` sit behind the session token
  (401 without it) and therefore make poor unauthenticated probes.
