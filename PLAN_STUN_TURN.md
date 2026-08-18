# PLAN: STUN/TURN, vibe visibility, conference (RDAP)

Branch: `gb/stun-turn`  
Worktree: `worktrees/stun-turn`  
Base: `a67299a7` (main, first-launch UX)

## Scope

See RESEARCH.md §2026-08-17 STUN/TURN. Manage **coturn**, do not vendor it.

## Phase 0 — Isolation

- [x] M0.1 worktree `gb/stun-turn` on clean main.

## Phase 1 — Research

- [x] M1.1 Inspect Hush C (configure, HTTP, launch/vibe, PWA, packaging).
- [x] M1.2 coturn run/daemon/systemd/WebRTC credentials.
- [x] M1.3 Synthesize RESEARCH.md + this plan. **Gate.**

## Phase 2 — Architecture

- [x] M2.1 Lock modules, routes, systemd unit, vibe visibility, signaling kind.

## Phase 3 — Implementation

- [x] M3.1 `./configure --enable-stun-turn` / `--disable-stun-turn` (default on).
- [x] M3.2 `hush_turn` + systemd unit + conf template + install.
- [x] M3.3 Vibe public/private on `hush_launch` + session JSON.
- [x] M3.4 HTTP `/api/turn` `/api/ice` `/api/signal` + status.whisper.
- [x] M3.5 PWA Settings + Call (mesh) + visibility.
- [x] M3.6 Tests + README/SECURITY.

## Phase 4 — Verify / land

- [x] M4.1 `./configure && make && make test` (on and off).
- [x] M4.1b Audit: daemon writes `/etc/hush/turnserver.conf`, systemctl waitpid, host sanitise, peer cap 8.
- [ ] M4.2 Push, PR, auto-merge, delete worktree.

## Locked decisions

- Runtime exec of `turnserver`, never link libturn.
- Child mode (no root) + daemon mode (systemd, sudo install).
- Long-term TURN user/pass (not REST HMAC) for single-operator hosts.
- Vibe visibility extends first-launch vibe; no second catalog.
- Conference = WebRTC mesh, kind 25000 signaling, max 8 peers.
- Whisper is a capability flag, not a bundled model.

Every C change follows write-legible-c. No `-Werror` carve-outs.
