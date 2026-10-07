# Changelog

Notable changes to Hush. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions come from
the top-level `VERSION` file.

## [Unreleased]

### Changed

- A robot loop that hits its limit says "Mention both robots again to start
  a new loop." A fresh loop arms only when the next note names exactly two
  robots. The feed card's "Decision" cue on a waiting loop shows for the
  hive owner only.
- Field-office look (#282): the default theme is now a darker Medal of
  Honor briefing-dossier khaki (olive-ink rules, oxblood stamp, no white
  surfaces); the theme id and stored choices are unchanged. On the 49
  views the palette check measures (sign-up cards, main thread, thread
  pane, Kit menu, Settings and the other 15 dialogs, and the BOARDS
  drawer, at 1440 and 390) text is at least 4.5:1, control edges and
  switch tracks at least 3:1, and the 36 keyboard focus rings it checks
  at least 3:1 (main had accent on paper at 3.97:1
  and faint text at 3.17:1). Thread, Download / Canvas and Stop are small
  22px ink stamps instead of the 32px plate, with a 44px touch area on
  phones. Settings theme radios use the theme's accent instead of the
  browser blue, in all 8 themes. Field-office radios and checkboxes,
  dialog password fields and selects are no longer white boxes, and the idle Send switch label is readable.

### Added

- Approval setting (#279): Settings → Robot turns offers `Auto-approve`
  (the default; robots run at once, as before) or `Approve every action`.
  With the second, every robot turn (first reply, follow wave, loop turn,
  leader election and plan) waits at one gate and the chaperon asks
  "Approval needed: <Robot> wants to take a turn. Reply Yes or No in this
  thread." Only the hive owner's typed Yes runs it; No posts "Turn
  declined: <Robot> stood down." and starts nothing. Any other owner note
  in the thread drops the waiting turn, approval lines never count toward
  the turn cap, at most 8 turns wait at once, and waiting turns are not
  kept across a relay restart. The setting is saved per owner in
  `vibe.json` as `approval_mode`, read back on start, and set with
  `POST /api/profile {"approval_mode":"..."}` (an unknown value is refused
  and a garbled stored value reads as `auto_approve`).
  r2 (Gauge review of f87770a7): approving the leader's plan pass now runs
  it when the convener is the elected leader (the plan pass has its own
  wake slot); an empty or non-string `approval_mode` is refused instead of
  clearing the profile names; the Settings radio shows the saved value
  after a page load and on every refresh; the election ask names the
  leader election; other people share at most 4 of the 8 waiting entries;
  approval lines no longer reach robot prompts.
  r3 (Gauge review of f93bf0a7): a refused turn someone other than the
  owner asked for gets "Too many requests from other people are waiting
  for the owner. Try again later." instead of the owner's "Answer one
  first." line; the docs now say the approval post must use the compact
  `"approval_mode":` form, because the relay's flat JSON reader does not
  see the key with a space before the colon (#289) and such a body is
  saved as a profile instead (#288).

- Two-robot loop (#280): when a human mentions two robots, the first one
  (the lead) also sees the whole note and can keep a back-and-forth going
  with a hidden `LOOP: continue` / `LOOP: stop` line. At the channel turn
  cap the chaperon asks "Continue this loop? Reply Yes or No in this
  thread." instead of "That's enough robot talk."; a typed Yes runs
  another round, No posts "Loop stopped.", and after four Yes answers the
  loop ends with "Loop limit reached. Mention both robots again to start a new loop.". Only
  the hive owner answers Yes/No, and any other note from the owner ends
  the loop. Every loop turn uses a turn of the cap, the control tag is
  case-insensitive and tolerates markdown, and each loop turn's ask quotes
  the peer's last note as text (the existing "Thread brief" context still
  shows recent notes as written; the cap and the owner-only Yes bound any
  steering through it). Loop state is not kept across a relay
  restart. The partner still sees only its own clause, and robots still
  never chain on their own.

### Fixed

- Phone walkthrough: the expanded inventory menu no longer opens behind the drawer, and Edit plus double-click open a robot; a right-click selects that robot and enables Edit (the button stays disabled until a robot is selected); at phone width the dispatch log keeps a minimum height and the page scrolls when the roster is tall; quick-bar labels fit at 375px.

- Worker failures are no longer silent: a dying worker writes a
  HUSH_JOB_ERR line into its output stream, the relay carries the reason
  into job->diag, and the failure note prints it ("...through deepseek-api
  (provider transport failed (curl: ...))"). API transport failures now
  embed curl's own stderr; the marker never reaches the channel.

### Fixed

- Provider troubleshooting: dispatch names the concrete missing piece. A
  job that starts on a fallback because the robot's first-choice provider
  is unready posts one line ("Deepseek API is not ready (no model
  selected); using Grok Build instead."), and the failure note appends the
  same reason when known. New helper `hush_provider_missing_reason()`.
- Agent editor provider picker: three columns, one plain style (no
  ready/picked fills), and explicit status text per provider —
  "authenticated"/"not authenticated", "api token present"/"no api token",
  "runtime installed"/"not installed". The provider drawer reminds you
  when an API provider has no model selected.

### Changed

- `hush_agent.c` split complete: from 4,524 lines to an 864-line core
  (public API, job table, notes, runtime readiness, grok start) plus six
  per-cluster modules behind `hush_agent_internal.h`: `agent_dispatch.c`
  (job lifecycle + dispatch/follow flow), `agent_process.c` (worker exec
  and provider CLIs), `agent_thread.c` (thread walking and context),
  `agent_text.c` (mention rewrite, alias mapping, reply scrub),
  `agent_prompt.c` (prompt and directive builders), and
  `agent_fixup.c` (the fixup pass). Behavior-preserving; the full suite
  stays green with the source-grep tests retargeted to the new files.

### Added

- `hush_http.c` split from 2,874 lines into a 977-line core (guard, auth,
  rate, reply/session, JSON helpers, dispatch router, lifecycle) plus eight
  per-family modules behind `hush_http_internal.h`: `http_static.c`,
  `api_status.c`, `api_identity.c`, `api_agents.c`,
  `api_channels.c`, `api_canvas.c`, `api_provider.c`, `api_turn.c`.
  Strictly behavior-preserving; the full suite stays green.

### Added

- Token-bucket rate limiting (`hush_limiter`, monotonic clock): per-connection
  EVENT/REQ buckets, per-IP wire EVENT bucket, per-pubkey verified-EVENT
  budget, AUTH/JOIN attempt caps (drop), per-IP HTTP throttling with
  `429 Too Many Requests`, and 12/min quotas on `POST /api/fixup` and
  `POST /api/complete`. Per-IP/per-connection EVENT buckets run before
  signature verification so forged-frame floods cannot pin the CPU.
- AI overload protection: a full global job queue refuses new dispatches via
  the existing start-failure diagnostic, and a per-robot provider budget
  (60/min, burst 20) caps dispatch cost at the fork; chat and event delivery
  keep flowing.
- New `check_limits.py` integration suite (EVENT flood, REQ flood, AUTH
  attempt cap, HTTP 429) wired into `make test`.


- RFC 6455 WebSocket transport for the Nostr wire protocol: upgrade handshake
  on the shared port (`Sec-WebSocket-Accept` via SHA-1), masked client
  frames, unmasked server text frames, fragmented-message reassembly,
  ping/pong, close echo with proper status codes (1002/1003/1007/1009), and a
  UTF-8 validator (`hush_ws` module with RFC 6455 §5.7 vector tests).
  WebSocket clients receive the NIP-42 challenge immediately after the `101`;
  all wire lines (OK/EOSE/EVENT/AUTH/CLOSED/NOTICE) are framed through one
  send chokepoint, so AUTH, private-hive gating, and fan-out behave identically
  over `ws://` and raw TCP. New `check_ws.py` integration suite.


- NIP-42 AUTH on the line protocol: a per-connection challenge (sent on the
  first wire frame), signed kind-22242 `["AUTH", <event>]` validation with a
  ±600 s freshness window, challenge rotation after failed attempts, and
  socket-bound auth state. Kind 22242 is answered `restricted:` and never
  stored, fanned out, or served.
- Private-vibe enforcement on the wire: REQ gets `CLOSED auth-required`, EVENT
  gets `OK false`, and fan-out skips unauthorized connections until the client
  AUTHs as a member (human or roster member; events must then carry the authed
  pubkey) or presents the join token via `["JOIN", "<token>"]`.
- Join tokens are persisted only as `vibe_token_hash` (hex SHA-256) in
  `vibe.json` schema version 2, with one-shot migration of version-1 plaintext
  files, display-once semantics, and `POST /api/vibe {"action":"rotate_token"}`
  plus a rotate button in the PWA settings.
- `hush_nip42` validation module, `hush_auth` challenge minting and SHA-256
  helpers, `hush_proto` AUTH/JOIN/CLOSED frames, and an independent pure-Python
  BIP-340 test signer (`tests/sign_bip340.py`, checked against the official
  vectors) driving the new `check_authz.py` integration suite.

## [0.0.1] - 2026-09-11

Hardening pass against the external technical review
(`docs/research/REVIEW_HUSH_0.0.1.md`). The plan and the evidence behind each
verdict live in `docs/plan/PLAN_REVIEW_HARDENING.md` and `docs/research/`.

### Added

- Session-token HTTP authentication: constant-time compare, cookie / header /
  bearer / query carriers, Host allowlist, no wildcard CORS.
- Verified launch without `system()` (`hush_launch`), and
  `hush_dir_ensure_private` for owner-only state directories.
- BIP-340 schnorr verification in-repo on OpenSSL BIGNUM/EC (no new
  dependency) with the official test vectors, wired into the relay's signed
  `EVENT` gate: a rejected event answers `OK false` with `invalid: …` and is
  never stored, woken, or fanned out.
- Append-only store log with a versioned magic and periodic snapshots; replay
  skips ids the ring already holds.
- Durable per-thread transcripts and rolling briefs under
  `$HUSH_HOME/threads/`, and a durable-transcript fallback for agent context.
- Provider streaming: `stream:true` plus unbuffered curl, SSE deltas painted
  into the thread as they arrive, and `POST /api/reply` for the live partial.
- `POST /api/cancel {root, robot}`: SIGTERM to the job's process group,
  SIGKILL after the grace period, and an honest "stopped on request" note.
- Shared bounded JSON parser (`hush_json_lookup` / `hush_json_decode`) and a
  full NIP-01 filter (kinds, ids, authors, since/until, `#e/#p/#h/#d`).

### Changed

- The relay binds loopback by default (`--listen` widens it) and no longer
  sends a wildcard CORS header.
- Protocol egress escapes JSON strings, and an oversized line earns a NOTICE
  before the close.
- Per-client bounded outbox with `POLLOUT` backpressure instead of unbounded
  buffering.
- `NOSTR.md`, `SECURITY.md`, and `README.md` describe the real wire and
  threat model.

### Fixed

- Intel hold/follow overflow returns NULL instead of a stale slot; cooldown
  applies only to robot-triggered chains; per-channel job caps; a start
  failure posts a note instead of silence.
- Read-only routes gained GET guards and `POST /api/turn` is reachable.
- Store insert cost fell from 3.4 ms to 0.02 ms median (1 KB events, same
  harness).
