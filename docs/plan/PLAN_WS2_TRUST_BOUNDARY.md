# WS2 trust-boundary regression suite + ops guardrails

Branch: `gb/ws2-trust-boundary`. Base: `origin/main` @ WS1 (`e76cf61b`, lean CI green).
No relay rewrites; tests + SECURITY deltas only. KISS.

## Observed baseline (all verified by probe, not assumed)

- `check_authz.py`, `check_ws.py`, `check_limits.py` green on base.
- Gaps with no coverage: token carriers (only `X-Hush-Token` used by
  harnesses), Host allowlist, CORS absence, scan/ice auth gates, JOIN cap,
  WS 1007/1009, non-member AUTH matrix cell, token format/double-rotate.
- Relay behaviors pinned by probe on isolated relays:
  - Cookie / `X-Hush-Token` / `Bearer` / `?k=` all 200; wrong/missing 401.
  - Loopback Host (bare or with port) 200; foreign/missing Host 403,
    including `/api/status` and authed routes.
  - No `Access-Control-Allow-Origin` on 200/401/403/204; OPTIONS 204.
  - `/api/ice`, `/api/provider/scan` 401 without token; scan fails closed
    without echoing the key; idle ICE exposes STUN only, no credential.
  - JOIN cap 16 (`HUSH_LIMIT_JOIN_ATTEMPTS`); 17th earns no NOTICE.
  - Non-member valid AUTH answers true yet REQ/EVENT stay `auth-required`;
    public guest signed EVENT answers true.
  - Join token is 16 hex chars; rotation invalidates the predecessor;
    visibility flips preserve the token.

## Milestones (commit per M on the worktree branch)

- M1: new `hush-c/tests/check_trust.py` (carriers, Host, CORS, scan/ice).
- M2: extend `check_authz.py` (matrix, format, rotation chain),
  `check_ws.py` (EVENT gate, 1007, 1009), `check_limits.py` (JOIN cap);
  wire `check_trust.py` into `hush-c/Makefile` test target.
- M3: SECURITY.md subsection (scan/ICE posture) + operator checklist;
  this plan file.
- M4: full `./configure && make && make test`, push, open PR to main
  (no merge). Report PR + Actions run URLs; tag claims
  OBSERVED/INFERRED/UNKNOWN.

## Out of scope

Armory/chat features, relay rewrites, new CI plumbing (WS1 CI reused as-is).
