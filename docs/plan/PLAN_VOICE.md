# PLAN_VOICE — voice target selector (phase 1: research + plan, no code)

Epic VOICE, ordered by CoS 2026-10-17 13:18 EDT (Chuck USER_SPECIFIED).
Base: `origin/main` @ `fa56420a`. Tags: **OBS** = OBSERVED in repo, **EXT** =
VERIFIED_EXTERNAL (cited URL), **INF** = INFERRED, **UNK** = UNKNOWN.
Claim ids `[Cn]` resolve in the appendix.

Feature: one control that picks who hears the human's voice:
**Chief of Staff | All robots | One robot | None (mic off)**.

## 1. Inventory (existing plumbing)

| # | Item | Where | Tag |
|---|------|-------|-----|
| I1 | `whisper` boolean in `/api/status`; true when `HUSH_WHISPER` set (≠"0") or `/usr/bin/whisper` / `/usr/local/bin/whisper` executable. Detection only. [C1] | `hush-c/src/api_status.c:63-78`, `hush-c/src/hush_turn.c:202-215` | OBS |
| I2 | `/api/turn` (GET) also reports `whisper`. [C2] | `hush_turn.c:270-278`, `hush_http.c:323` | OBS |
| I3 | No transcription or TTS code path found in `hush-c/src` (rg `whisper\|speech` hits only I1/I2 and voice-id validation). [C3] | rg over `hush-c/src` | INF |
| I4 | Fixed robot voice ids: alloy, echo, fable, onyx, nova, shimmer (6; OpenAI-style names). Agent `voice` field validated against them. [C4] | `hush_skill.c:57-64`, `api_agents.c:32,415-416,939-940` | OBS |
| I5 | PWA gates everything on `whisperReady = !!st.whisper`. [C5] | `index.html:3073,9085` | OBS |
| I6 | `.chan-voice` ♫ button per channel → `openChannelVoice(c)`; `.robot-call` ☎ per robot card ("One-to-one with X") → `openRobotCall(bot)`; both only when `whisperReady`. [C6] | `index.html:3596-3608,4130-4141,6320-6350` | OBS |
| I7 | `#robot-list` is a hidden legacy container (`style="display:none"`). [C7] | `index.html:2272,1757` | OBS |
| I8 | Calls are WebRTC mesh: `signal()` POSTs `/api/signal` with `channel`, `from`, `to:"*"`; `ensureLocal()` calls `getUserMedia({audio:true, video:true})`. Robots join as tiles with no media stream. [C8] | `index.html:6362-6372,6329-6331` | OBS |
| I9 | Robot editor "Voice" `<select id="agent-voice">` shown only when Whisper ready; fallback list = the 6 ids. [C9] | `index.html:2638-2642,8217-8232,5267-5268` | OBS |
| I10 | Status line "Voice ready/off · Calls on/off". [C10] | `index.html:4234,7679` | OBS |
| I11 | Thread routing rule: **1:1** (you + one robot) and **1:n** (you + every mentioned/authoring robot); subline `1:1 with Happy` / `1:n · you + Happy, Payne`; 1:1 inherit attaches the sole member as `mention_0`; never mention the human. [C11] | `UI_SPEC.md:760-783` | OBS |
| I12 | Chief of Staff = Major (team / hop fences; owner Yes required). [C12] | `docs/ROBOT_TO_ROBOT.md:204-233` | OBS |
| I13 | Field-office theme tokens: ink `#2a241e`, paper `#e2d5b8`, stamp red `#a3352c`, `#5c4a3d` borders, `2px 2px 0` shadow, uppercase telegram labels; S1 gate = no literal colours in field-office rules (`check_palette_tokens.py`). [C13] | `UI_SPEC.md:1108-1120,1454-1456` | OBS |
| I14 | Keys: pass → 1Password → keyring → `keys.vault` (AES-256-GCM, 0600); never plaintext. [C14] | `HowToUsePrivateKeys.md:7,38,40` | OBS |
| I15 | Branch rule: never commit/push `main`; `gb/<name>` worktrees. [C15] | `BRANCHING.md:7-11` | OBS |

Gap (INF): Whisper is an STT gate, yet it also gates the TTS voice picker, so
robots cannot speak without an STT binary present.

## 2. Research

### 2.1 Distinct per-robot voices (TTS)

| Option | Voices | License | Offline | Cost | Latency | Linux reality | Tag |
|---|---|---|---|---|---|---|---|
| Browser `speechSynthesis` (Chrome) | system voices via speech-dispatcher | browser | yes | 0 | low | empty `getVoices()` unless speech-dispatcher + module (espeak-ng) installed; reports Chrome may return none on Linux [C16] | EXT |
| Browser `speechSynthesis` (Firefox) | speech-dispatcher default module | browser | yes | 0 | low | works when speech-dispatcher + espeak-ng present; `resistFingerprinting` empties list [C16] | EXT |
| espeak-ng (local) | many langs/variants, robotic | GPL-3.0 | yes | 0 | very low | packaged on Arch/Omarchy (INF) | INF |
| Piper (OHF-Voice/piper1-gpl) | many ONNX voices; per-voice licences vary | GPL-3.0-or-later (old rhasspy/piper MIT, archived) | yes | 0 | low on CPU (INF) | `pip install piper-tts`, HTTP server mode [C17] | EXT |
| xAI TTS `grok-voice-tts-1.0` | 5 core (eve, ara, rex, sal, leo); third-party page says 28 as of Oct 2026 | proprietary API | no | $15 / 1M chars; 50 RPS, 100 concurrent | streaming supported | needs key [C18] | EXT (28 count: UNK) |
| OpenAI `gpt-4o-mini-tts` | 13 (alloy…cedar); `tts-1` 9 | proprietary API | no | $0.60/1M in + $12/1M audio out | streaming (INF) | needs key [C19] | EXT |
| Others (ElevenLabs, Kokoro, Coqui) | — | — | — | — | — | not researched | UNK |

Note (OBS+EXT): repo ids I4 are all valid OpenAI voices [C4][C19] but are
not xAI ids [C18], so a provider map is needed.

### 2.2 Detect voice capability

| Capability | Probe | Notes | Tag |
|---|---|---|---|
| Mic present | `navigator.mediaDevices.enumerateDevices()` kind `audioinput` | labels hidden before grant (INF) | INF |
| Mic permission | `navigator.permissions.query({name:"microphone"})` | Chrome 64+, Firefox 132+ [C20] | EXT |
| Mic open | `getUserMedia({audio:true})` | today repo also requests video [C8] | OBS |
| Browser STT | `"SpeechRecognition" in window \|\| "webkitSpeechRecognition" in window` | Chrome partial; Firefox disabled by default [C21]; Chrome sends audio to a service unless on-device pack (INF from MDN) | EXT/INF |
| Server STT | `/api/status.whisper` | exists [C1] | OBS |
| Browser TTS | `"speechSynthesis" in window` and `getVoices().length>0` after `voiceschanged` or 2 s timeout | [C16] | EXT |
| Provider TTS | relay-side: provider has key in keystore + TTS model | no such probe today | UNK |

### 2.3 Multi-voice support

- Count: distinct `voiceURI` from `getVoices()` (browser); provider list
  length (server). EXT [C16][C18][C19]
- Concurrency: `speechSynthesis.speak()` appends to one utterance queue per
  page → queued, never concurrent. EXT [C22]. Provider audio as separate
  `<audio>`/WebAudio sources can overlap (INF).
- Fallback ladder (INF): provider voice → Piper voice → browser voice →
  espeak-ng → text only (caption badge "NO VOICE").
- Distinctness when voices < robots: assign by stable hash of robot slug,
  pitch/rate offsets for browser voices (INF).

## 3. Recommendation

1. **V-target is a routing feature first:** a per-channel selector whose
   output is the existing mention/recipient set (1:1 / 1:n), not a new
   transport. Transcribe locally (Whisper, already detected) → post as a
   normal note with explicit `mention_*` recipients.
2. **TTS: relay-side local Piper by default** (offline, Linux-first, free),
   provider TTS (xAI / OpenAI) opt-in per robot with keys from the existing
   keystore chain [C14]; browser `speechSynthesis` only as last fallback
   because Linux Chrome often has zero voices [C16].
3. **Split gates:** `stt` and `tts` capabilities reported separately in
   `/api/status`; stop gating the voice picker on Whisper.

Alternatives: (A) browser-only (Web Speech both ways): zero server work,
but Firefox has no STT and Linux voices are often empty → rejected as
primary. (B) provider realtime speech-to-speech (xAI grok-voice-3.0
$0.08/min [C18]): best latency, but cloud, costly, and bypasses the
note/recipient record → later milestone, opt-in.

## 4. Data model and routing

```
voice_target := { mode: "cos" | "all" | "one" | "none",
                  robot: <slug>|null,      // mode=="one" only
                  channel: <slug> }        // per channel, localStorage hush-voice-target
robot.voice  := { engine: "piper"|"xai"|"openai"|"browser"|"espeak",
                  id: <voice id> }         // extends today's string field (I4)
/api/status  += "stt": bool, "tts": ["piper","browser",...], "voices": n
```

Routing (mirrors UI_SPEC 1:1 / 1:n, [C11]):

| Mode | Recipients (mention_*) | Visibility label | Robots that speak back |
|---|---|---|---|
| cos | Major (CoS) only | `1:1 with Major` | Major |
| all | every robot on the channel roster | `1:n · you + A, B, C` | each, queued in roster order |
| one | chosen robot | `1:1 with <name>` | that robot |
| none | mic track disabled, no transcript | `MIC OFF` | none (text still works) |

Rules: the transcript is a normal channel note visible to all channel
humans (same as typed chat); recipients are only who is *asked to answer*.
Never mention the human. No silent fan-out: label always lists names.
Keys never leave the relay; the PWA never sees provider keys.

## 5. UI sketch (words)

Composer strip, left of Send: a stamp group labelled `VOICE TO:` with four
S1 stamp buttons `C/S` · `ALL` · `ONE ▾` · `MUTE` (uppercase mono, `#5c4a3d`
2px border, `2px 2px 0` shadow, active = stamp red `#a3352c` ink, tokens
only so S1 passes). `ONE ▾` opens a dossier slip listing robots (radio).
Under it a telegram line: `TRANSMITTING TO: 1:1 WITH MAJOR` or `MIC OFF`.
A red `● LIVE` stamp shows only while the mic track is enabled. Robot cards
keep ☎ (`.robot-call`) as a shortcut that sets mode=one for that robot.
Capability chip in the relay drawer: `STT whisper · TTS piper (12 voices)`.
Hick: 4 choices ≤5 [UI_SPEC.md:18].

## 6. Milestones and acceptance pins

**V1 — capability probe (no audio out).** `/api/status` gains `stt`, `tts`,
`voices`; PWA `voiceCaps()` combines with Permissions/SpeechRecognition/
speechSynthesis probes.
- P1.1 `curl /api/status` has keys `stt` (bool), `tts` (array), `voices` (int).
- P1.2 With no whisper and no piper: `stt:false,tts:[]`; selector shows only `MUTE` enabled, others disabled with title explaining why.
- P1.3 Headless Chrome test: `voiceCaps()` resolves within 2.5 s even when `getVoices()` stays empty.
- P1.4 Voice picker no longer hidden when `whisper:false` but `tts` non-empty.

**V2 — target selector + routing (text path).** Selector UI; typed or
transcribed text uses the mode to build `mention_*`.
- P2.1 mode=cos → posted event has exactly one mention = Major pubkey.
- P2.2 mode=all on a 3-robot channel → 3 mentions; label `1:n · you + …` lists 3 names.
- P2.3 mode=one(X) → 1 mention X; label `1:1 with X`.
- P2.4 mode=none → `getUserMedia` never called; existing tracks `enabled=false`.
- P2.5 Mode persists per channel across reload.
- P2.6 `check_palette_tokens.py` S1–S4 and `check_palette.sh` C1/C2 pass at 1440 and 390.

**V3 — STT via Whisper.** Push-to-talk records audio-only (no video), relay
transcribes, posts note per V2.
- P3.1 `getUserMedia` constraint is `{audio:true}` only for voice notes.
- P3.2 A 5 s fixture WAV transcribes and posts with correct mentions.
- P3.3 Audio buffer deleted after transcription (no file left in `$HUSH_HOME`).

**V4 — TTS replies, distinct voices.** Piper default; robot `voice` → engine+id.
- P4.1 Two robots with different voice ids produce audio with different engine/voice metadata.
- P4.2 mode=all replies play queued, never overlapping (event log order = roster order).
- P4.3 With no TTS engine, reply shows text + `NO VOICE` badge, no error.
- P4.4 rg finds no API key literal in repo or `$HUSH_HOME` plaintext.

**V5 — provider TTS opt-in (xAI/OpenAI).** Keys via keystore chain only.
- P5.1 Provider TTS only when key resolves from pass/1Password/keyring/vault.
- P5.2 Voice id mapping table covers all 6 legacy ids.
- P5.3 Provider failure falls back to Piper within one reply.

**V6 (later, optional) — realtime speech-to-speech.** Out of scope pending CoS.

## 7. Risks

| Risk | Mitigation | Tag |
|---|---|---|
| Linux Chrome returns zero browser voices | server TTS default | EXT [C16] |
| Firefox has no SpeechRecognition | server Whisper STT | EXT [C21] |
| Chrome webkitSpeechRecognition ships audio off-box | do not use by default | INF |
| Piper GPL-3.0 vs repo licence; voice model licences vary | ship as optional external binary, not linked; check LICENSE + model cards | EXT [C17] / UNK |
| Mic left hot | `MUTE` default, LIVE stamp, auto-off on tab hide | INF |
| Fan-out cost with `all` + provider TTS | per-reply char cap, Piper default | INF |
| Legacy ids not valid on xAI | mapping table | OBS+EXT |

## 8. Open questions for CoS

1. Is "Chief of Staff" always Major, or a per-hive role setting?
2. Default mode on first open: `MUTE` (proposed) or `C/S`?
3. Push-to-talk only, or also continuous (VAD) listening?
4. Should transcripts post as visible channel notes (proposed) or private to recipients?
5. Is Piper (GPL-3.0, external binary) acceptable as default TTS?
6. Provider TTS: which providers are in scope (xAI only, or OpenAI too)?
7. In `all` mode, should every robot answer aloud, or only the first/lead?
8. Should existing call/mesh (`.chan-voice`, ☎) be folded into this selector or stay separate?

## 9. UNKNOWNs

- Exact current xAI built-in voice count (5 in official/Cloudflare docs; 28 claimed by a third-party page).
- Piper CPU latency on Omarchy target hardware (not measured; no run per order).
- Whether Omarchy ships speech-dispatcher by default.
- Licence compatibility of Piper with repo LICENSE.
- Other providers (ElevenLabs, Kokoro, Coqui) not researched.

## Appendix — claim trace

| Id | Claim | Source | Tag |
|---|---|---|---|
| C1 | whisper detection in status | hush-c/src/api_status.c:63-78; hush-c/src/hush_turn.c:202-215 | OBS |
| C2 | /api/turn reports whisper | hush-c/src/hush_turn.c:270-278; hush-c/src/hush_http.c:323 | OBS |
| C3 | no STT/TTS pipeline in C | `rg -i "whisper\|speech" hush-c/src` at fa56420a | INF |
| C4 | 6 voice ids, validated | hush-c/src/hush_skill.c:57-64; hush-c/src/api_agents.c:32,939-940 | OBS |
| C5 | whisperReady gate | hush-c/demo/index.html:3073,9085 | OBS |
| C6 | .chan-voice / .robot-call | hush-c/demo/index.html:3596-3608,4130-4141,6320-6350 | OBS |
| C7 | hidden #robot-list | hush-c/demo/index.html:2272 | OBS |
| C8 | signal + getUserMedia audio+video | hush-c/demo/index.html:6362-6372 | OBS |
| C9 | voice picker gated | hush-c/demo/index.html:2638-2642,8217-8232 | OBS |
| C10 | status voice line | hush-c/demo/index.html:4234,7679 | OBS |
| C11 | 1:1 / 1:n rules | UI_SPEC.md:760-783 | OBS |
| C12 | Chief of Staff = Major | docs/ROBOT_TO_ROBOT.md:204-233 | OBS |
| C13 | field-office tokens, S1 gate | UI_SPEC.md:1108-1120,1454-1456 | OBS |
| C14 | keystore chain, no plaintext | HowToUsePrivateKeys.md:7,38,40 | OBS |
| C15 | branching | BRANCHING.md:7-11 | OBS |
| C16 | Linux speechSynthesis needs speech-dispatcher; async getVoices | https://stackoverflow.com/questions/46863170 ; https://unix.stackexchange.com/questions/349611 ; https://blog.crawlex.net/blog/speech-synthesis-voices-fingerprint/ | EXT |
| C17 | Piper GPL-3.0, offline, moved to OHF | https://github.com/OHF-Voice/piper1-gpl ; https://pypi.org/project/piper-tts/ ; https://github.com/rhasspy/piper | EXT |
| C18 | xAI TTS $15/1M chars, 5 voices, limits; S2S $0.08/min | https://docs.x.ai/developers/models/text-to-speech ; https://docs.x.ai/developers/pricing ; https://developers.cloudflare.com/ai/models/xai/grok-tts/ | EXT |
| C19 | OpenAI 13 voices, pricing | https://developers.openai.com/api/docs/guides/text-to-speech ; https://developers.openai.com/api/docs/models/gpt-4o-mini-tts | EXT |
| C20 | Permissions microphone support | https://caniuse.com/mdn-api_permissions_permission_microphone | EXT |
| C21 | SpeechRecognition support | https://caniuse.com/speech-recognition ; https://developer.mozilla.org/en-US/docs/Web/API/Web_Speech_API/Using_the_Web_Speech_API | EXT |
| C22 | speak() queues utterances | https://developer.mozilla.org/en-US/docs/Web/API/SpeechSynthesis/speak | EXT |
