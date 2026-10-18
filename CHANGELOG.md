# Changelog

Notable changes to Hush. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions come from
the top-level `VERSION` file.

## [Unreleased]

### Changed

- A window that is not paired with the relay (no `hush_session` cookie, so every `/api` route but
  `/api/status` answers 401) shows a "Pair this window" card instead of the first-run "Click Begin"
  splash. The card names the two real ways to pair: the one-time `pair:` link a `--no-open` relay
  prints, or `hush-relay --quit <port>` and a fresh start (the window the relay opens is paired). While
  unpaired the page asks only `/api/status` and `/api/session`, backing off from 2 s to a 30 s cap
  (Check again asks at once; focus or a visible tab re-checks within a second, on the 1 s tick), and tries `POST /api/window` once instead of 20
  times: 5 responses 401 in the first 30 s, was 110 on main. Once the cookie is there the card gives way to
  the hive (or the splash on a first run) without a reload. Pinned by `tests/check_pair_ui.cjs` and
  `tests/check_palette.cjs` P1 (all 8 themes x 1440/390).
  Ops r2 (#368): the card leads with a plain step ("quit Hush and start it again from your Hush app or
  shortcut"); terminal commands sit in `<code>` that never breaks inside a flag at 390, and Check again
  has air below it. Profile and Settings opened over the card say "Pair this window first" and their
  actions are disabled (no "no identity yet", no "turn api unreachable" on a 401). The card edge is
  >= 3:1 in all 8 themes (`--pair-edge`). Pinned by `check_pair_ui.cjs` L0/L1 and `check_palette.cjs` P2.
  Gauge r2 (#368): a non-401 error (429, 5xx) from `/api/session` while unpaired keeps the card and its
  backoff instead of dropping to the splash and polling at 1 Hz. Pinned by `check_pair_ui.cjs` U2 (30 s
  cap, focus/visibility re-check, no focus storm), U3 (429 keeps the card), R1 (Check again <= 2 s) and
  R2 (pairing resets the backoff).
- Canvas: the code view is visible again in field-office. The editing textarea had an opaque paper slip
  (`--surface-2`) above the highlight layer, so code painted blank there (Ops #364 follow-up, from #282).
  It is now transparent in every theme, with a `--fg` caret and a translucent accent selection. Highlight
  inks fall back to new theme tokens (`--code-kw`, `--code-str`, `--code-num`; field-office keeps its
  `--fo-tok-*`), so light no longer paints the dark-theme pastels; comments and the FIM ghost use
  `--muted`. Field-office keyword 1.01:1 → 5.91:1, light keyword 1.61:1 → 8.42:1, and every class is >=
  4.5:1 in all 8 themes at 1440 and 390. Pinned by `tests/check_palette.cjs` CC1, which opens a real C
  note in the canvas and also checks the rendered pixels. L1's expected light syntax colours move with it.
- Field office fixes from the Ops back-LOOK of main 900a486d: the Developer Log text box is ink on a paper
  slip instead of a black/green terminal; robot provider pills read 6.32:1 (were 3.17:1 on --accent-dim);
  the thread-pane milestone fields wrap at phone width so each placeholder shows whole; the 390px header
  status lamp is flat muted green / amber (two new field-office tokens) with an ink rim, no gradient and
  no white glint. All themes: robot
  picture cells sit on --surface-2 instead of the native #efefef button grey, and the 1943 edition line
  and HQ stamps show only in field-office. Pinned by `tests/check_palette.cjs` K1-K6.
- A mention of someone not yet in the roster shows a neutral "@unknown" pill instead of vanishing
  (it left a leading blank), and is renamed once the roster has them. The 1 s refresh re-renders such a
  note only when the roster changes, not every second. Per UI_SPEC the unresolved
  @npub1… token itself is still never painted; the short key is only in the pill's tooltip. Field-office mention pills read
  6.66:1 (were 3.17:1). The Developer Log and robot inventory inks are `--fo-*` tokens (other themes
  keep #000/#0f0 and cyan/gold/red as fallbacks). `tests/check_palette_tokens.py` S5-S7 now fail on a
  literal colour in a markup inline style, a script string, or a new unscoped rule (G291-2).
- Field-office no longer paints the shared code-language spines, Canvas syntax colours, call
  tiles, inventory item text, skill slot / doll rims and ALWAYS-ON / ON-CALL chips in their dark-theme
  literals: they read `--fo-*` tokens (text >= 4.8:1, rims >= 4.32:1 there; were down to 1.01:1 and
  1.19:1). Every other shared literal colour sits behind a `--fo-*` fallback too, so the other seven
  themes compute the same values and S7's baseline is empty: any unscoped literal colour now fails.
  Pinned by `tests/check_palette.cjs` L1.
- Field-office call stage: the Muted slip on a call tile is paper ink on the warn slip (5.88:1; was ink
  on warn at 1.73:1). New pins: W1 (no element paints pure white in the 62 field-office views the palette check renders), S8 (no
  white or near-white literal reaches field-office), K9 (the real call stage) and K10 (the real skills
  board and skill sheet). `tests/palette_mutants.py` runs the palette and mention mutants.
- Mentions: the note body text never contains the npub and the @unknown pill's aria-label carries the
  short key (K7). The repaint key is the sorted npub=name pairs, so a same-size roster swap repaints (K7t).
- Mentions: the live robot partial preview renders through the same mention renderer as a note body,
  so an unresolved `@npub1…` shows as an @unknown pill there too (K7p). Each new partial replaces the
  preview instead of piling up after the last one: K7p paints three partials into one preview slot
  and each must read exactly its own text with 2 pills. The roster key's sort and its
  npub and name halves are pinned (K7k); the voice button's fallbacks in the other 7 themes are pinned (K11o).
  The L1 fixture no longer borrows the live robot inventory (its 1 s re-render made L1 flaky at 390)
  and an empty or detached read fails L1. `tests/palette_mutants.py`: 43 of 43 caught.
- Christmas: the selected skills-board facet's ring is light crimson `--accent` (was `--accent-dim`,
  1.01:1 on the greens), pinned by R5 on the skills board view.
- Christmas theme: every red is one crimson hue. Light crimson `--accent` #ff6b7f (dark ink #0f1f14)
  is for accent text, radios, outlines, borders, focus edges and rings, and the on switch track, so a
  checked Settings radio reads 5.77:1 against its panel (was 2.71:1). Deep crimson `--accent-dim`
  #5c1420 (was #7f1d1d) sits only behind accent text: the hover fill (4.86:1, was 1.71:1 on main) and
  the mine bubble. A christmas-only `--accent-fill` #c41e3a paints the primary buttons with light ink
  (5.57:1) and a light-crimson edge. The Thread stamp outline is light crimson (5.77:1, was 1.58:1). The
  switch track is `--faint` when off (4.66:1, was 1.61:1) and light crimson when on (5.77:1, was 1.58:1
  on main); its knob is `--fg` when off (3.23:1 on the track) and dark `--accent-ink` when on (6.25:1).
  Measured borders and rings once drawn in `--accent-dim` use the light crimson (min 5.77:1), including
  the selected avatar tile's ring (5.77:1, was 1.19:1) (#309). The selected avatar tile's 3px ring is
  `--accent` in every theme (was `--accent-dim`, below 3:1 on the grid in six themes), so the picked tile
  is unmistakable: dark 2.45 → 3.98, light 1.43 → 6.90, color-blind 2.16 → 7.62, dracula 1.61 → 11.51,
  desert 1.45 → 4.14, monochrome 2.18 → 13.53; christmas stays 5.77 (its own rule is dropped) and
  field-office keeps its own offset print shadow (5.31). The avatar window's preview ring is
  `--accent` in every theme (was `--accent-dim`, below 3:1 on the panel in six themes): dark 2.45 → 3.98,
  light 1.43 → 6.90, color-blind 2.16 → 7.62, dracula 1.61 → 11.51, desert 1.45 → 4.14, monochrome
  2.18 → 13.53; christmas stays 5.77 and field-office keeps its own offset print shadow (4.88). No other
  element changes in the other themes. `tests/check_palette.cjs` R1 requires a 3:1 checked radio fill in all 8
  themes at both widths; R2 measures christmas accent-painted text and controls on each checked
  surface; R3 hovers each visible Thread and channel voice button with the mouse and needs 4.5:1
  text (R3s pins the robot call button, which no view shows); R4 needs a 3:1 Thread stamp outline;
  R5 needs 3:1 on christmas accent / accent-dim borders and outer rings at rest, focused, and with
  the mouse on each Providers hub row, with at least one measured instance of each claimed rule; R6
  needs a 3:1 switch track and a 3:1 knob on it, off and on; R7 needs the primary buttons on
  `--accent-fill`; R5 also measures the selected tile's ring in the avatar window, opened by its real
  buttons; R8 needs a solid avatar preview ring of at least 3:1 in all 8 themes at both widths, and R9
  the same of the selected tile's ring against the grid. The
  check now fails if it stops before its pass line.

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

### Security

- hush-turn (#353): packages no longer install `/etc/hush/turnserver.conf` with a placeholder TURN
  credential. Run `sudo /usr/share/hush/hush-turn-setup` (fresh 32-hex secret, 0600) before
  `systemctl enable --now hush-turn`; the unit will not start without a generated secret, and coturn
  runs as a transient unprivileged user (`DynamicUser=yes`, systemd 247+). **Upgrading a deb:** the
  postinst replaces the old conf only if it still holds the placeholder; the original is kept as
  `/etc/hush/turnserver.conf.placeholder-<time>.bak` (copy back hand edits such as `external-ip=`).
  hush-turn is never enabled or started for you.

- Copilot and Goose no longer run with tool auto-approve (#333). Copilot
  never gets `--allow-all-tools`: its shell tools are excluded and denied
  on every turn, URL and memory tools are denied, and only a person-gated
  project turn may write files. The child drops `COPILOT_ALLOW_ALL` and
  the `GITHUB_COPILOT_PROMPT_MODE_*` switches. Goose always runs with
  `GOOSE_MODE=chat` (no tools), overriding an inherited mode or
  `config.yaml`. A turn whose mode cannot be set is refused with a
  `HUSH_JOB_ERR` reason.

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
