# Security Policy

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you discover a security vulnerability in Hush, please report it privately via
[GitHub Security Advisories](https://github.com/coldcanuk/hush/security/advisories/new)
or by contacting the maintainers listed in `debian/control`. Include as much
detail as possible:

- A description of the vulnerability and its potential impact
- Steps to reproduce or a proof-of-concept (if available)
- The affected version(s) or commit range
- Any suggested mitigations you've identified

You will receive an acknowledgment within **48 hours**. We aim to provide a
full response — including a timeline for a fix — within **7 days** of initial
contact. We'll keep you informed as we work toward a resolution.

We ask that you:

- Give us reasonable time to address the issue before any public disclosure
- Avoid accessing or modifying data that does not belong to you
- Not perform denial-of-service attacks or disrupt production systems

We will credit reporters in release notes unless you prefer to remain anonymous.

---

## Supported Versions

| Version | Supported |
|---------|-----------|
| `main` (latest) | ✅ Active |
| Previous releases | ⚠️ Best-effort; upgrade recommended |

Hush is pre-1.0. We do not maintain long-term support branches at this stage.
All security fixes land on `main` first.

---

## Security Design Principles

### Authentication — HTTP session token and NIP-42 on the wire

The HTTP API is gated by a per-hive session token. On first run the relay mints
`session.token` (32 hex characters from `/dev/urandom`, mode 0600) inside
`$HUSH_HOME` and prints its path at startup. Every `/api/*` route requires it
except `/api/status` (liveness) and `GET /api/complete?t=<job-token>` (a
per-job capability token). Credentials are accepted as:

| Carrier | Use |
|---|---|
| `Cookie: hush_session=…` | issued only to the window that opens the one-time pairing link |
| `X-Hush-Token: …` | scripts and CLI clients |
| `Authorization: Bearer …` | HTTP clients |

The relay never hands the token to a caller that does not already hold a
credential (#336): no loopback request earns a cookie, and `?k=` in a URL is
not accepted (URLs land in history, logs, and `Referer`). At start the relay
mints a pairing code in memory only (32 hex, not derived from the token,
never on disk, new each start). The app window the relay opens is launched
with `/?pair=<code>`; with `--no-open` the relay prints that link once. The
first request presenting the code gets the `hush_session` cookie
(`Max-Age` 30 days, so a reopened window of the same profile stays signed in)
and the code is re-minted at once, so a copy of the link is dead. The page
removes `?pair=` from its address bar on load. Exercised by
`check_bootstrap.py`. Limit: until the window redeems it, the code is in the
browser's command line (`/proc/<pid>/cmdline`) for about a second.

Owner authority that a robot must never borrow needs more than the token
(#336 part 5). Redeeming the pairing code also mints a browser-session
secret issued as the `X-Hush-Nonce` response header (32 hex, memory only,
not derived from the token) plus a non-HttpOnly `hush_nonce` storage cookie
the page reads and sends back as that header. A new pairing rotates it and
a relay restart ends it. An owner `Yes`/`No` answer note (held turns, team
offers, loop "Continue?") and any `approval_mode` write are refused `403`
unless the request carries the live `X-Hush-Nonce` (constant-time compare);
`session.token` alone, in any carrier, is not enough. A stored
`approval_mode` that is present but unknown, empty, or not a string asks
every action; a vibe that never saved one keeps the auto-approve default.
Exercised by `check_approve_nonce.py` and `test_auth`.

Agent children (#336 parts 1 and 2) run in a jail: a fresh per-job `HOME`
(mode 0700). Provider creds in that HOME will come from Vault
(`vault.onerelay.app`, SOT): per-job copy, must match, deleted after (TODO).
**INTERIM:** only allowlisted real-HOME auth trees are bound read-only
(`.grok`, `.codex`, `.copilot`, `.config/goose`, `.goose`, `.cline`,
`.ollama`); never `~/.password-store`, `~/.gnupg`, `~/.ssh`, or `$HUSH_HOME`.
An unlisted auth path refuses the turn and names the path. The child env is
an allowlist (no `HUSH_*`). Landlock ABI ≥ 4 is preferred (deny
`$HUSH_HOME`/config/legacy and the real HOME tree, deny the relay TCP port,
scope ptrace); otherwise `bwrap --unshare-pid --proc /proc` with
`no_new_privs` when that hides the relay's `/proc` and blocks ptrace; with
neither, the turn is refused with a clear reason. The relay keeps
`HUSH_HOME`/`HUSH_CONFIG_DIR` in its own environ; children do not.


No write a child can reach stamps the owner (#336 part 4). An agent child
can reach the relay port and can read `session.token`, so every route that
stores an event under the owner's pubkey (`POST /api/event` notes of any
kind, `/api/signal`, `/api/presence`) needs the live `X-Hush-Nonce` too and is
refused `403` without it. Exercised by `check_owner_stamp.py`.

Agent children are jailed before exec (#336 parts 1 and 2). The environment
is an allowlist (PATH, HOME, LANG, TERM, locale, proxy/CA, and provider
`GROK_`/`CODEX_`/`COPILOT_`/`GOOSE_`/`OLLAMA_`/`CLINE_` settings): nothing
named `HUSH_*`, so no `HUSH_KEY_PASS`, `HUSH_HOME`, or `HUSH_OP_*`. Inherited
descriptors above stderr are closed. On Linux with Landlock ABI ≥ 4 the child
enters a domain that denies every open under `$HUSH_HOME`, the config dir and
the legacy config dir (the job cwd stays usable), denies TCP connect to the
relay port, and — because Landlock scopes ptrace to the domain — cannot read
the relay through `/proc/<pid>` or ptrace it. Without Landlock ABI 4 the turn
is refused; there is no weaker fallback. The relay itself calls
`hush_keyfile_take_phrase()` at start: it copies `HUSH_KEY_PASS` into memory,
wipes the bytes in the initial environment block, and unsets it, so
`/proc/<relay>/environ` no longer shows the phrase. Exercised by
`check_sandbox.py`.

The listener binds `127.0.0.1` by default. `--listen 0.0.0.0` (or another
address) exposes it deliberately; remote clients must then present the token.
`Access-Control-Allow-Origin` is not set, the `Host` header must name the local
machine in loopback mode, and the browser cookie is `HttpOnly; SameSite=Strict`.

On the wire protocol — WebSocket or raw TCP — every connection receives a
per-connection [NIP-42](https://github.com/nostr-protocol/nips/blob/master/42.md)
challenge: WebSocket clients immediately after the `101`, raw clients in
response to their first wire frame (the sniff cannot tell the protocols apart
before the first bytes; see [NOSTR.md](NOSTR.md)). A client authenticates with
a signed kind-22242 `["AUTH", <event>]` whose `challenge` tag echoes the
challenge and whose `created_at` is within ±600 s of now. The relay verifies
the NIP-01 id and BIP-340 signature, binds the pubkey to that socket, and
answers `OK true`; a failed attempt is answered `OK false` and receives a
fresh challenge. WebSocket framing follows RFC 6455 (masked client frames,
unmasked server text frames, bounded reassembly, close/ping handling);
malformed frames close `1002`, binary `1003`, invalid UTF-8 `1007`,
oversized `1009`.

Wire `EVENT` frames are authenticated: the relay recomputes the NIP-01 id and
verifies the BIP-340 signature with OpenSSL against the claimed pubkey before
store, `OK true`, and fan-out. A rejected event is answered
`["OK", <id>, false, "invalid: ..."]` and dropped. Kind 22242 events are answered
`restricted:` and are never stored, fanned out, or served.

Locally created events (the HTTP path) carry no signature; the HTTP session
token is their credential.

### Authorization — private vibes are real boundaries

Channel membership (`humans[]` / `robots[]`) is conversation metadata: it
decides which robots the leash may dispatch and what the UI shows. It is **not**
a request access-control mechanism by itself.

A **vibe** has a `public` or `private` visibility flag. Public vibes admit
signature-verified wire events and `REQ` from anyone; NIP-42 stays available but
is not required for reads. Private vibes gate every wire operation until the
connection proves membership:

| Operation | Private-hive gate |
|---|---|
| `REQ` | `["CLOSED", sub, "auth-required: …"]` unless authorized |
| `EVENT` | `["OK", id, false, "auth-required: …"]` unless authorized |
| fan-out | delivered only to authorized connections |

A connection becomes authorized by either

1. NIP-42 AUTH with a member pubkey — the local human's key or any roster
   member — after which published events must carry that same pubkey, or
2. `["JOIN", "<token>"]` with the vibe join token, granting guest read and
   write; a token guest may publish events signed by any key (documented
   tradeoff until capability tokens land).

The join token (16 hex chars) is shown once by the UI and persisted **only** as
`vibe_token_hash` = hex(SHA-256(token)) in `vibe.json` (schema version 2;
version-1 plaintext files are migrated on load). `POST /api/vibe` with
`{"action":"rotate_token"}` mints a replacement and returns its plaintext once.

### Abuse controls — token buckets and quotas

Every ingress path is throttled by monotonic-clock token buckets
(`hush_limiter`); the defaults are generous because the default hive is a
local single-user app, and the caps are flood defenses.

| Path | Limit | On exhaustion |
|---|---|---|
| wire EVENT, per connection | 10/s (burst 20) | `OK false "rate-limited: …"` |
| wire EVENT, per source IP | 30/s (burst 60) | `OK false` |
| wire EVENT, per author pubkey (verified only) | 30/min (burst 10) | `OK false` |
| wire REQ, per connection | 5/s (burst 10) | `CLOSED "rate-limited: …"` |
| AUTH attempts / connection | 8 | connection dropped |
| JOIN attempts / connection | 16 | connection dropped |
| HTTP API, per source IP | 60/s (burst 120) | `429 Too Many Requests` |
| `POST /api/fixup` | 12/min | `429` |
| `POST /api/complete` | 12/min | `429` |
| robot dispatch, per robot | 60/min (burst 20) | honest denial note |
| concurrent AI jobs | 4 (global) | existing start-failure diagnostic |

The per-IP and per-connection EVENT buckets run **before** signature
verification, so a forged-frame flood cannot pin the ~300 µs BIP-340 check;
the per-pubkey bucket runs after verification and only counts valid frames.
Overload degrades gracefully: a full job queue refuses new AI work while chat
and event delivery keep flowing. Small fixed tables fail open when full;
`/api/status` stays exempt for liveness probes.

### STUN/TURN

The optional coturn child/daemon is started with a generated long-term
credential (`user=hush:<random>`). Never run an open TURN relay: it will be
used as a DDoS reflector. TLS/DTLS for TURN is out of this slice; put
coturn behind a firewall and set `external-ip` when NATed. Daemon mode
installs a systemd unit but does not enable it until the operator asks.

### Provider scan and ICE (trust boundary)

`GET /api/ice` and `POST /api/provider/scan` sit behind the HTTP session
token (401 without it) and behind the Host allowlist (403 on a foreign
Host), like every other non-liveness `/api/*` route. Regression coverage:
`hush-c/tests/check_trust.py`.

- Scan fails closed: an unreachable host answers `{"ok":false}` and the
  reply never echoes the submitted `api_key`. Saved providers never persist
  secrets to the overlay, the session payload, or later GETs
  (`hush-c/tests/check_provider.sh`).
- ICE with TURN idle returns only the public STUN entry
  (`stun:stun.l.google.com:19302`) with no `credential` field. With TURN
  enabled the reply carries the live TURN username and password, so the
  session token is what stands between a remote caller and relay
  credentials: keep the default loopback bind unless remote UI access is
  intended, and never re-add a wildcard `Access-Control-Allow-Origin`
  (the suite fails if one appears).

### Operator checklist (short runbook)

1. Bind: default listens on loopback. A non-loopback `--listen` makes the
   session token the only boundary; the Host check stays advisory then.
2. Token: `$HUSH_HOME/session.token` (0600) is the credential for every
   `/api/*` route except `/api/status` and one-shot
   `GET /api/complete?t=<job-token>`. Cookie, `X-Hush-Token`,
   and `Authorization: Bearer` carry it (`check_trust.py`); `?k=` is refused
   and the cookie only follows the one-time pairing link
   (`check_bootstrap.py`, #336).
3. Join tokens: `POST /api/vibe {"action":"rotate_token"}` when membership
   changes. Rotation invalidates the previous token immediately; the file
   persists only `vibe_token_hash`, and a restart clears the displayed
   plaintext (display-once). Flipping visibility never mints a replacement.
4. Floods: per-connection/per-IP/per-pubkey EVENT buckets answer
   `OK false "rate-limited"`, REQ floods earn `CLOSED "rate-limited"`, AUTH
   is capped at 8 attempts and JOIN at 16 per connection, HTTP per-IP flood
   earns 429. `check_limits.py` pins each cap.
5. After any suspicion of exposure: rotate the join token, restart the relay
   (fresh session token), and re-check `GET /api/provider` for unexpected
   hosts or models.

### Event Store

The store is a bounded 1,024-event ring. Every insert appends one record to
`$HUSH_HOME/store.log`; the relay rewrites `$HUSH_HOME/store.ring` as a
snapshot every 256 inserts and once at shutdown, then empties the log. The log
is synced at most once per second, so a hard crash can lose the last second of
chat. On load a missing snapshot is empty and a torn log tail is ignored; the
snapshot is written through a temp file plus rename. Oldest events are evicted
first. The files are not a tamper-evident audit log; do not treat a running
`hush-relay` as a compliance archive.

### Agent Secret Storage — `pass`

Hush-aware tools store secrets in the unix password manager `pass` **by
default**. The human must uncheck the modal box to opt out.

| Secret | Path | Retrieve |
|---|---|---|
| Human identity | `hush/identity/nsec` | `pass show hush/identity/nsec` |
| Agent nsec | `hush/agents/<agent-name>/nsec` | `pass show hush/agents/<agent-name>/nsec` |
| Provider API key | `hush/providers/<id>/api_key` | `pass show hush/providers/<id>/api_key` |
| Provider username | `hush/providers/<id>/username` | `pass show hush/providers/<id>/username` |
| Provider password | `hush/providers/<id>/password` | `pass show hush/providers/<id>/password` |
| Provider token | `hush/providers/<id>/token` | `pass show hush/providers/<id>/token` |
| Provider passkey | `hush/providers/<id>/passkey` | `pass show hush/providers/<id>/passkey` |

Grok Build and Codex OAuth tokens stay in `~/.grok/auth.json` and
`~/.codex`. Hush only forks the official CLI (`grok login --oauth`,
`codex login`) and never copies those homes into `pass`.

Hive metadata (vibe name, visibility, token, channels, projects,
profile without email, members, robot labels) lives in
`~/.config/hush/vibe.json` mode 0600. That file must never contain an
nsec or provider secret. `make clean` does not delete it.

See [IMPORT.md](IMPORT.md) and [docs/pass-integration.md](docs/pass-integration.md).

The `HUSH_PRIVATE_KEY` environment variable, when set, takes precedence for
harnessed agents and CI.

### Input Validation

- Event ids, pubkeys, and signatures are fixed-length hex buffers; wire events
  are recomputed and BIP-340 verified before they are stored.
- Content and tag strings are bounded (`HUSH_EVENT_MAX_CONTENT`,
  `HUSH_EVENT_MAX_TAGS`, `HUSH_EVENT_MAX_TAG_LEN`).
- The wire parser rejects malformed lines instead of trusting client input.
- Filter arrays have static caps (`HUSH_FILTER_MAX_KINDS` and related
  constants).

### Transport Security

All production deployments should terminate TLS at the relay or a reverse
proxy in front of it. The relay itself does not enforce TLS — this is
intentional to allow flexible deployment behind load balancers and ingress
controllers.

### Build Hardening

Hush is strict C11. The required compiler flags are
`-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`. `./configure` probes
`-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-fPIE`, `-Wl,-z,relro`,
`-Wl,-z,now`, and `-pie`, and records the flags the toolchain accepts in
`config.mk`; `hush-c/Makefile` always compiles with
`-fstack-protector-strong -fPIE` and links `-Wl,-z,noexecstack`. There is no
Rust, Cargo, or `unsafe` crate surface in this repository.

---

## Disclosure Policy

We follow [coordinated disclosure](https://en.wikipedia.org/wiki/Coordinated_vulnerability_disclosure).
Reporters will be credited unless they request anonymity.
