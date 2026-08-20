# RESEARCH — /trouble: Rail, Canvas, Providers, Exit, Titlebar, Call/AV (RDAP Phase 1)

Date: 2026-08-20  
Branch: gb/trouble-rail-canvas-prov (worktree)  
Base: main (post #51)  
Methodology: Research-Driven Adaptive Planning (RDAP) — Phase 1 gate.

## Executive Summary (from four-minds protocol)
All symptoms are small, localized near-misses in launch state, predicate logic, key scoping, CSS, and launch args — not systemic architectural failure. Posterior strongly favors H1 (provider predicate + rail launch state + window feedback) + H2 (canvas Tab scope).

Evidence is 100% verbatim from files in the worktree. make test passes cleanly. No C changes required for core fixes (JS + small provider.c tweak for configured).

## Verbatim Evidence (E# from Phase 1 re-inspection in worktree)
**E1 — Providers incorrectly show Deepseek/API "not configured"**  
hush-c/src/hush_provider.c:62 (family table):
```
{ HUSH_ROSTER_PROVIDER_DEEPSEEK, "Deepseek API", HUSH_PROVIDER_FAMILY_API, HUSH_PROVIDER_HOST_DEEPSEEK, "" }
```
hush-c/src/hush_provider.c:788:
```
static void hush_provider_mark_configured(hush_provider_status_t *st)
{
    ...
    if (st->has_key && st->host[0] != '\0') { st->configured = 1; return; }
    if (st->has_home && strcmp(st->family, HUSH_PROVIDER_FAMILY_HOME) == 0) ...
```
(See: 229 has_key from pass, 196 call site, index.html:2076 statusLine uses configured, 2138-2144 Cline text exists.)

**E2 — rail-min / rail-max appear to do nothing**  
index.html:2799:
```
$("rail-min").addEventListener("click", async () => { try { await api("/api/window", { action: "minimize" }); } catch (err) {} });
```
C: hush_http.c:1486 → hush_win_minimize (XIconifyWindow after _NET_CLIENT_LIST find). Same for max (send_state). No user-visible feedback.

**E3 — Tab does not trap when canvas open (glowing cursor works, selection moves)**  
index.html:3637:
```
$("code-canvas-edit").addEventListener("keydown", (ev) => {
  if (ev.key === "Tab" && activePrediction) { ev.preventDefault(); acceptCanvasFim(); return; }
  ...
```
No broader handler for canvas.show + Tab (only FIM path).

**E4 — Browser/standard bar at top still visible ("windowless")**  
manifest.webmanifest: `"display": "standalone"`.  
hush_relay.c:279: `--app=...`.  
hush_relay.c:296: chromium ... `--ozone-platform=x11` + app_arg.  
hush_win.c + relay: Motif decorations=0 + undecorate on g_saw_app.  
UI_SPEC.md §10 explicitly documents this as the intended "frameless standalone --app".

**E5 — Tool Rail not minimized + docked on launch**  
index.html:4035:
```
function loadRail() {
  ...
  if (saved.collapsed) setRailCollapsed(true);
  if (typeof saved.x ... ) placeRail... else { place default top-right }
```
No force-collapsed + placeRailAtBrand() on first ready hive paint.

**E6 — Exit menu (#hive-leave) buttons massively huge**  
index.html:824 (drawer) + CSS:512:
```
.leave-actions { display:flex; flex-direction:column; gap:10px; margin-top:16px; }
.leave-actions .btn { min-height:44px; width:100%; }
```
Matches UI_SPEC §10 but burns real-estate.

**E7 — Call button + AV per channel**  
Already: call-btn → openStage, openRobotCall (1:1), openChannelVoice (1:n on channel), addTile + RTCPeerConnection + ensureLocal getUserMedia + /api/signal (kind 25000) + tiles with mute.  
chan-voice buttons on rows.  
coturn: hush_turn.c + contrib/turnserver.conf.in + iceServers.  
No pjproject/FFmpeg/SIP sources in tree.

**Additional context from prior RESEARCH/PLAN (re-read in worktree)**
- RESEARCH_RAIL_PROV.md, PLAN_PROVIDER_CONFIGURE.md, RESEARCH_EXIT_CLOSE.md, UI_SPEC.md, PLAN_PWA.md etc. all confirm the surfaces already partially exist.

## Provider Tailoring Matrix (synthesized)
| Family | Examples | Needs | Current Drawer | Required Fix |
|--------|----------|-------|----------------|--------------|
| home   | goose, grok-build, codex | detect ~/.config/goose etc + login binary | use_home + oauth | Keep + improve detection |
| editor | cline | ClinePass or BYO key; path ~/.cline or Documents/Cline | mixed home/api text | Special honest empty + key option |
| api    | deepseek-api, openai-api, ... | ONLY api_key (host/model optional for some) | key+host+user/pass+scan | Key-only for pure API; configured=has_key; + host/model for advanced |

Grok Build / Codex: OAuth via `grok login --oauth` / `codex login` (terminal spawns browser). No saved token in Hush.

Cline (confirmed from code + prior plan): bring-your-own or ClinePass. Not pure OAuth.

## AV / Call Stack Assessment
Current: browser WebRTC (RTCPeerConnection) + coturn for STUN/TURN + /api/signal mesh.  
1:1 and channel conference already implemented in JS.  
pjproject + PJMedia + FFMpeg: heavy (native SIP stack). Not required for the stated "add audio and video controls to each newly created channel" — WebRTC + existing tiles/mute already provides this. Recommend: enhance UI controls + ensure per-channel voice button + global mute; leave native SIP for future if wire protocol demands it. coturn already present and integrated.

## Bayesian + Hypotheses (from protocol)
Top posterior H1 (predicate + launch state + feedback) 0.61.  
H2 (canvas key scope) 0.23.  
Fixes are small, behavior-preserving where possible, with adapters/comments for legacy.

## Risks & Mitigations
- Embed step after HTML/JS: run embed-ui.sh or make will pick up.
- X11 find for min/max may be fragile on some DEs: add logging + fallback note.
- Robot assignment + provider: global config vs per-robot roster field — keep roster "provider" as primary; hub +/– updates roster entries for consistency.
- No direct main writes: all on gb/*.

## Success Criteria (DoD for whole trouble)
- Tab trapped when canvas open (no browser selection move, uses glowing FIM or inserts tab).
- Rail starts minimized + docked (placeRailAtBrand + collapsed) on first launch.
- rail-min/max produce observable effect (or clear status).
- Exit drawer sleek (compact buttons, reasonable real-estate).
- Deepseek etc. show configured when key present; tailored fields per family.
- + / – robot pills in providers hub; consistent from rail/nav/raise.
- Call: audio/video controls (mute per tile, channel voice) present and working on channels.
- make && make test clean.
- Worktree PR merged, deleted; main clean.

