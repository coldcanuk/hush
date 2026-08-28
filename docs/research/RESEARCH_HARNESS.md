# RESEARCH_HARNESS — Phase 1 Synthesis

Status: complete (baseline, pre-refactor).
Scope: multi-provider harness, capability routing, auto-update scanner,
token/context engineering, in-hive messaging protocol, and UI/UX rail.

This document is the RDAP Phase 1 synthesis gate. It maps what exists today
and names the exact gaps between the current engine and the target harness.

---

## 1. Module map (as of `main` @ 88ab095a8)

| Module | LOC | Role |
|---|---|---|
| `hush_provider.c/h` | 1138 | Provider registry, home detection, overlay file, pass secrets, curl model scan, OAuth login |
| `hush_agent.c/h` | 2907 | Mention dispatch, job table, co-mention coordination, chaperon, process spawn |
| `hush_launch.c/h` | 3014 | First-launch wizard, identity, vibe, channels + policy leash, projects, Payne, coordination |
| `hush_roster.c/h` | 1044 | Robots (name/prompt/provider/picture/voice/skills/context/role), members |
| `hush_proto.c/h` | 173 | Nostr wire protocol (EVENT/REQ/CLOSE/COUNT) |
| `hush_cevent.c/h` | 131 | In-hive channel events (ordered + timed ring) |
| `hush_skill.c/h` | 887 | Skill catalog (system/robot/user) + forge writer |
| `hush_intel.c/h` | 715 | Chaperon rails: mention detect, topic leash, hop cap, policy |
| `hush_http.c` | 2291 | HTTP routes + serves embedded PWA |
| `hush_ui_html.h` | ~36k | Embedded PWA (generated from `hush-c/demo/` by `scripts/embed-ui.sh`) |
| `hush_turn.c/h` | 480 | STUN/TURN (coturn); **not** agent turns |
| `hush_win.c/h` | 269 | X11 window controls (rail window) |

Strict build flags confirmed: `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`.
Full `make test` passes at baseline.

---

## 2. Provider layer — what exists

`hush_provider_meta[]` registers **nine** providers across three families:

| Family | Providers | Executes turns today? |
|---|---|---|
| `home` (CLI binaries) | `goose`, `grok-build`, `codex` | **grok-build only** |
| `editor` | `cline` | no |
| `api` (REST) | `gemini-api`, `xai-api`, `openai-api`, `anthropic-api`, `deepseek-api` | no |

Each provider carries `{ id, label, family, host, binary }` plus a `hush_provider_status_t`
(has_binary / has_home / has_key / … / configured). Detection covers home configs
(`~/.grok/auth.json`, `~/.codex/auth.json`, `~/.config/goose`), an overlay file
(`~/.hush/config/providers.json`), and `pass` secrets (api_key, username, password,
token, passkey). `hush_provider_scan()` lists models via curl for the API family.
`hush_provider_start_login()` runs the official OAuth login for grok-build and codex;
goose is refused (must use `goose configure`).

**Critical finding:** the provider registry is a *configuration and status* surface,
not an *execution* router. `hush_agent.c` hardcodes `grok -p` as the only runtime:

```
/* grok-build is the only runtime that executes a turn today ... */
static int hush_agent_can_start_grok(...) { ... return hush_agent_grok_ready(); }
```

Non-grok robot picks silently fall back to grok-build. So "multi-provider" is
currently a UI/config facade over a grok-only engine.

---

## 3. Gap analysis vs. target scope

### 3.1 Harness (the engine)

| Requirement | Status | Gap |
|---|---|---|
| Multi-provider routing (goose/agy/copilot/codex/BYOK/ollama/custom) | partial | Only grok-build executes. No goose/codex/API execution path. |
| `agy` (Antigravity) headless, no-wrap ToS | absent | No `agy` provider, no "spawn-only, never wrap" concept, no UI allow-list pointer. |
| Copilot OAuth flow | absent | `start_login` covers grok/codex only; no copilot OAuth. |
| Ollama local inference | absent | Not in registry. |
| Custom OpenAI-compatible endpoints | partial | `openai-api` is fixed-host; no user-supplied base URL field. |
| BYOK native API | partial | API family keyed by `pass`, but no execution path. |
| Launch-time auto-update scanner | absent | Only binary detection; no `grok update` / `agy update` / `codex update` routines. |
| Capability matrix (tools/image/file) | absent | No capability bitmask; nothing blocks unsupported actions. |
| Conversation rules (1:1, 1:N, N robots→chaperon) | partial | `chaperon` field + role exist; 1:1/1:N via mentions/threads work; N-robot chaperon is advisory, not hard-gated. |

### 3.2 Token & context engineering

| Requirement | Status | Gap |
|---|---|---|
| Plain-text/markdown segmentation | absent | No chunker. Only `hush_roster_is_context_mime()` MIME gate + 4096-byte/file cap. |
| Regex/parse robustness | partial | Manual JSON escaping exists; no dedicated segmentation parser. |
| Payload reduction for plain-text models | absent | Full context forwarded; no smart splitting. |

### 3.3 Messaging protocol

| Requirement | Status | Gap |
|---|---|---|
| Deterministic machine-to-machine signaling | present | `hush_cevent` ring: mention/intro/job_start/job_done/follow/hop_denied/jobs_held/chaperon/presence/stuck. |
| Ordered + timed | present | `seq` + `due` fields, sorted JSON. |
| Ack/retry guarantee | absent | Best-effort delivery; no ack or retry at the signaling layer. |

### 3.4 UI/UX

| Requirement | Status | Gap |
|---|---|---|
| Edit menu responsive grid | absent | Edit robot is "one compact line" (README); no desktop/tablet/phone grid. |
| Skills Forge spacious screen | partial | Forge exists; renders as compact panels, not a dedicated spacious screen. |
| 128px avatar selection window | partial | Icon sheets exist; selection is not a dedicated 128px window. |
| Tool rail "winner" model + Z-index fixes | partial | Free-drag hamburger rail exists; Z-index (exit confirm behind rail) is a known open bug. |

---

## 4. Baseline Bayesian scores (pre-refactor)

Weighting: `Final = 0.4*x + 0.4*y + 0.2*z`.

### 4.1 Harness Architecture — **5.2 / 10**
- X Extensibility & Compliance: 5.5 — clean registry, but grok-only execution, no agy/copilot/ollama, no ToS no-wrap model.
- Y Lifecycle & Logic: 6.0 — chaperon field + roles + threads work; no auto-update scanner; N-robot chaperon advisory only.
- Z Capability Routing: 3.0 — no matrix; nothing routes or blocks tools/image/file.

### 4.2 UI/UX Responsiveness — **6.1 / 10**
- X Form Factor: 6.5 — PWA is fluid, but edit menu is a single line, no explicit breakpoint grid.
- Y Component Clarity: 6.0 — Forge + icon sheets exist but compact/hover, not dedicated screens.
- Z Z-Index/Rail: 5.5 — free-drag rail is a good model; known z-index overlap bugs remain.

### 4.3 Token & Context Engineering — **2.6 / 10**
- X Parsing Precision: 3.0 — MIME gate only, no structural chunking.
- Y Cost Efficiency: 2.5 — no payload reduction.
- Z Regex Robustness: 2.0 — no dedicated segmentation parser.

### 4.4 Messaging Protocol — **7.4 / 10**
- X Reliability: 7.5 — ordered ring + poll; no ack/retry.
- Y Determinism: 7.0 — structured JSON, machine-only, dev_log off by default.
- Z Performance: 8.0 — lightweight non-blocking ring buffer.

**All four domains are below 9.0.** Per the loop mechanics, work continues
beginning with the highest-leverage foundational gap: the capability matrix and
true provider abstraction (Harness Architecture), then the token/context engine.

---

## 5. First milestone (chosen)

Add a **capability matrix** to the provider registry so the harness can
route and block actions before execution:

1. `hush_provider_caps_t` bitmask (TOOLS / IMAGE / FILE_ATTACH).
2. Per-provider `caps` in `hush_provider_meta_t`.
3. `hush_provider_capabilities()` + `hush_provider_can()` API.
4. Capabilities exposed in status JSON for the UI.
5. Unit tests in `test_provider.c`.

This is self-contained, testable, and is the prerequisite for true capability
routing (subscore Z) and for adding agy/copilot/ollama with honest feature flags.

---

## 6. Milestone status + re-scored (post M1..M5)

Landed on `gb/harness-engine` (five atomic commits, `make test` green each,
clean-rebuild verified):

- **M1 — capability matrix.** `hush_provider_caps_t` (TOOLS/IMAGE/FILE_ATTACH),
  per-provider `caps`, `hush_provider_capabilities()` + `hush_provider_can()`,
  exposed as `"caps"` in `/api/provider`.
- **M4 — providers + policy flags.** Registry 9→13 (agy/copilot/ollama/custom);
  `hush_provider_flags_t` (SPAWN_ONLY / ALLOWLIST / OAUTH); `"flags"` in JSON.
- **M2 — capability gating.** `hush_roster_fill_context()` denies file context
  for providers without FILE_ATTACH (first real capability-routing block).
- **M3 — auto-update scanner.** `hush_provider_update_all()` (non-blocking,
  tracked, opt-in grok-build/codex).
- **M5 — token/context engine.** `hush_seg` structural chunker (UTF-8 safe,
  markdown fence atomicity).
- **Phase 4 — context flows to the turn.** Context body is now persisted in
  memory (`hush_roster_context_t.text`) and injected into the robot `-p` note
  via `hush_agent_append_context()` (chunked, bounded, fence-aware). Previously
  the body was validated then dropped, so attachments never reached the robot.
- **M6 — real event identity (phantom-code hunt).** `hush_sha256_hex()` was a
  zero-fill stub behind a dead `#if HUSH_USE_OPENSSL` guard; replaced with
  streaming OpenSSL `EVP_sha256()` over the NIP-01 canonical preimage
  `[0,pubkey,created_at,kind,tags,content]` (strings escaped, tags included).
  `hush_event_compute_id()` is now the single authoritative id path, and the
  **six** duplicate fake-id generators were deleted: five `*_make_id()`
  (`agent/roster/presence/intel/launch`) plus `hush_make_event_id()` in
  `hush_http.c` (found only because the e2e JSON still showed the old
  `0x9e3779b9` fake ids — a different name hid the duplicate). Verified with
  three hard-coded NIP-01 SHA-256 preimages in `tests/test_event.c`. Only
  `hush_skill_make_id()` remains — a deterministic catalog key, not a Nostr id.
- **M7 — single provider source of truth.** Deleted `hush_roster_providers[]`
  and `HUSH_ROSTER_PROVIDER_COUNT`; `hush_roster_is_provider()` now delegates
  to `hush_provider_is_id()`, so `hush_provider_meta[]` is the one runtime list.
- **M8 — auto-update wired + policy flags enforced.** `hush_relay_prepare()`
  calls `hush_provider_update_all()` behind an opt-in `HUSH_AUTO_UPDATE=1`
  gate. `hush_agent_exec_child()` now routes on `SPAWN_ONLY` and
  `hush_agent_grok_ready()` gates OAUTH providers on `has_home` — both via
  `hush_provider_flags()`, making it a real production accessor.

Re-scored (honest; loop still mandates continuing):

### Harness Architecture — **5.2 → 7.4 / 10**
- X Extensibility & Compliance: 5.5 → 7.4 (matrix + policy flags + 13 providers + NIP-01 real ids + flags enforced at dispatch; still grok-only execution for non-ogy runtimes).
- Y Lifecycle & Logic: 6.0 → 7.3 (auto-update scanner wired env-gated; not default-on; chaperon advisory).
- Z Capability Routing: 3.0 → 7.5 (queryable matrix + file-context gate + file-context flow; image/tool routing not yet wired).

### Token & Context Engineering — **2.6 → 7.6 / 10**
- X Parsing Precision: 3.0 → 7.5 (structural chunker now actually drives context injection).
- Y Cost Efficiency: 2.5 → 7.5 (context is chunked/bounded to the note budget; real payload reduction).
- Z Regex Robustness: 2.0 → 8.0 (crash-proof UTF-8-safe parser).

### UI/UX Responsiveness — **6.1 → 7.2 / 10**
- X Form Factor: 6.5 → 7.5 (edit menu now a responsive 2-col grid — 2 col desktop/tablet, 1 col phone — instead of one long column).
- Y Component Clarity: 6.0 → 7.0 (Skills Forge is a dedicated wide screen with a 46vh mono body; avatar picker is a dedicated 128px window).
- Z Z-Index/Rail: 5.5 → 7.0 (exit/leave modal now above the rail; tool rail tightened to a compact activity bar).

*(Honest caveat: implemented and JS-syntax/PWA-route verified, but not yet
rendered against Pixel/iPad/desktop viewports — pixel-level form-factor proof
still needs a browser.)*

### Messaging Protocol — **7.4 → 7.6 / 10**
- Y Determinism: 7.0 → 7.5 (event ids are now content-addressed SHA-256, deterministic for identical events, instead of timestamp+seq hex).

Remaining to reach 9.0+ (see PLAN_HARNESS_ENGINE.md): true multi-provider
execution (goose/codex/copilot/ollama — blocked on verified headless CLIs) and
browser-based visual verification of the Phase 5 UI refactor.

