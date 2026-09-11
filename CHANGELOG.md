# Changelog

Notable changes to Hush. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions come from
the top-level `VERSION` file.

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
