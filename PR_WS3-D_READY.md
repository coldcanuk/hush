# WS3-D: PR ready for Floor to open

`ManagePullRequest` refuses `gb/` heads (requires `cursor/` prefix),
and repo law forbids `cursor/` branches — so the branch is pushed and
the PR is ready for Floor to open with one click.

- Branch (pushed): `gb/api-status-healthprobe` @ `62f81243`
- Base: `main` @ `b1f0747a` (includes WS3-C merge)
- One-click compare / create PR:

  https://github.com/coldcanuk/hush/pull/new/gb/api-status-healthprobe

- Suggested title:

  WS3-D: document /api/status healthprobe for ops

- Suggested body (paste as-is):

WS3-D (Floor / reliability): operator-facing docs so ops can use
`GET /api/status` as the relay healthprobe. Docs-only — no `.c`/`.h`
touched.

## What

- New `docs/ops/api-status.md`, styled/placed with its siblings
  (`store-backup.md`, `store-bench.md`): endpoint, auth, success body,
  unhealthy modes, curl + readiness-wait + systemd + k8s sketches,
  default bind/port, and how it differs from `/api/exit` / `/api/close`.
- README Docs table gains one row linking it, the same way the store
  docs are linked.

## OBSERVED evidence (all derived from live source, base `main` @ `b1f0747a`)

- Route + handler: `hush-c/src/hush_http.c` (`hush_http_serve`:
  `GET /api/status` → `hush_http_serve_status`; POST falls through to
  `serve_api_post` → `404`), body formatter `hush_http_serve_status`
  in `hush-c/src/api_status.c`.
- Auth exempt, Host still enforced: `hush_http_needs_auth` /
  `hush_http_guard` in `hush-c/src/hush_http.c`; `SECURITY.md`
  ("every `/api/*` route requires it except `/api/status` (liveness)
  …"); `hush-c/tests/check_trust.py` (`check_host_allowlist`:
  loopback Host → 200, foreign/missing → 403).
- Body shape:
  `{"ok","version","events","clients","port","whisper","turn_running","vibe_public","thinking"}`
  from `api_status.c`; `thinking` array from `hush_agent_status` in
  `hush-c/src/hush_agent.c`.
- Defaults: `HUSH_DEFAULT_PORT = 10555`
  (`hush-c/include/hush_relay.h`), bind `127.0.0.1`
  (`hush-c/src/hush_relay_main.c`).
- Exit/close contrast: `hush_http_serve_exit` / `hush_http_serve_close`
  (`HUSH_HTTP_EXIT_JSON` / `HUSH_HTTP_CLOSE_JSON`) in `hush_http.c`;
  `README.md` ("`POST /api/exit` sets the same shutdown flag as
  SIGTERM"); `docs/ops/package-upgrade.md` (SIGTERM == `--quit` ==
  `/api/exit` cleanup path); `hush-c/tests/check_exit.sh`.
- Live-verified against a locally built relay (default loopback bind,
  port 18081): `GET` → `200` with the documented JSON;
  `POST /api/status` → `404 not found`; `Host: evil.com` → `403 bad
  host`; `POST /api/close` without token → `401` (token-gated, unlike
  status).

## Acceptance

Ops can probe `/api/status` from the doc alone:
`curl -sf http://127.0.0.1:10555/api/status` (+ sketches for wait-loop,
systemd, k8s exec probe).

No other WS3 slice touched. No drive-by refactors.
