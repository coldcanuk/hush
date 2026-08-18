# Hush UI Spec — Onboard + Profile + Vibe Members + Agent Create
Version: 2026-08-18 (RDAP M2.1, gb/robot-cards-ux)
Authoritative for this slice. Supersedes splash-only notes in the 2026-08-18
M2.1 splash spec and the onboard raise-form notes where they conflict.
Quinn + Parker + Payne. No feline.

## Core Principles (Quinn)
- Cognitive Load Index ≤ 3/10. Gestalt Clarity ≥ 85/100.
- Hick: ≤5 primary visible choices. Themes live in Settings, not header.
- Fitts: min 44px tap, 10px padding.
- Recognition > recall. One primary CTA per onboard step.
- Error prevention: confirm logout; nsec never in the DOM after ack;
  context files re-checked on the server.

## Payne Voice
- "At ease." "Mission first." "Report when ready." "Find or raise the right robot."
- One directive per major screen. Precise. No fluff. "Carry on."

## Product JTBD (Parker)
"As a human lead I stand up a local hive, name myself, pick a look, and
raise humans + robots that share channels."

## Flows (locked)

### 1. Splash (detect)
- Feather logo: `<img src="/icon-192.png" alt="hush" class="feather">`.
  Not a new illustration. Not the 193 KiB source PNG inline.
- Line: "Sgt Major Payne reporting for duty."
- Sub: "Detecting identity and vibe…"
- Poll `/api/session`. If `ready` → hive. Else **Begin** → wizard step 1.
- Header always: brand | Install | Profile | Settings | (Call if ready).

### 2. Onboarding wizard (no user or no vibe)
Linear 4 steps with progress `1 / 4` … `4 / 4` and four dots.

1. **Identity** — Create new (primary) or Import nsec. Help: “What’s an identity key?”
2. **Backup** — Masked nsec, Reveal, Copy. Checkbox **checked**:
   `Checked to save password to Unix Password Manager. Retrieve with: pass show hush/identity/nsec`
3. **Vibe** — Name (default `local hive`), about, public / private radios.
   CTA: **Stand up the hive**.
4. **Meet Payne** — After vibe exists (session `ready` but page stays
   `payne` until the human confirms). Show name, about, npub short,
   welcome quote. CTA: **Carry on.** Then hive.

`tick()` must not force `page = "hive"` while `page === "payne"`.

### 3. Resume
`logged_in && backup_acked && has_vibe` → splash detects → hive.

### 4. Hive
- Header: hush + vibe name + Install + Profile + Settings + Call.
- Sidebar: Channels; Create channel/project; **Raise a robot**;
  **Invite human**; **Robots** list (Payne first, then raised agents).
  Each robot is its own card with a 44px `+`/`-` expand/collapse.
- Stream + composer unchanged in spirit. Empty: Payne directive.

### 5. Profile (always reachable)
Drawer fields:
- Avatar upload (JPEG/PNG/WebP client; server stores JPEG/PNG, kind 0
  `picture` = `http://127.0.0.1:<port>/avatar/<pubkey>`).
- First name, last name, email, organization.
- npub (copy). Never nsec after ack.
- **Logout** — confirm, then `POST /api/identity {action:"logout"}`.
  Server clears human login; vibe/agents stay. Client returns to splash.

Email is **session-only**. Never written into kind 0.

Kind 0 for the human:
`name` = first or slug; `display_name` = "First Last";
`about` may append organization; `picture` = avatar URL.

### 6. Settings
- Existing STUN/TURN + vibe public/private + join token.
- **Theme** radios (only place themes appear):
  `dark` | `light` | `color-blind` | `dracula` | `desert` |
  `monochrome` | `christmas`
- Persist `localStorage.hush-theme` immediately and `POST /api/profile {theme}`.

### 7. Manage vibe
This process **is** the vibe. No second relay.
- Visibility public / private.
- Private: show join token + copy.
- Rename allowed via same `/api/vibe` name field later if cheap; not required
  for DoD if visibility + invite work.

### 8. Invite human
Drawer: npub (or hex pubkey) + optional display name.
Private vibe also shows the join token.
`POST /api/member {npub, role:"human", name?}`.

### 9. Raise a robot (agent create)
Payne walks the fields. Each commit-field uses `+` to freeze a pill
and a pencil to edit it again.
- Name. Input + `+` → pill + pencil. “State the robot’s name.”
  Empty on submit → auto-name `Robot-XXXX`.
- **System Prompt** (required; replaces “standing orders”).
  Multiline + `+` → pill + pencil + `-` (clears).
  “Write its system prompt.”
- Context files. Max **3**. Plaintext and Markdown only.
  `accept=".txt,.md,text/plain,text/markdown"`.
  `+` opens the file browser. `-` removes the selected file.
  “Attach only plain text or Markdown. I will refuse the rest.”
- **AI provider** (required). One of:
  Goose, Grok Build, Codex, Cline, Gemini API, xAI API,
  OpenAI API, Anthropic API.
  Wire ids: `goose`, `grok-build`, `codex`, `cline`,
  `gemini-api`, `xai-api`, `openai-api`, `anthropic-api`.
- pass checkbox default-on:
  `Checked to save password to Unix Password Manager. Retrieve with: pass show hush/agents/<slug>/nsec`
- CTA: **Raise this robot**.
- Red **Delete this robot** at the bottom. Disabled on a fresh raise.
  Enabled when editing an existing non-Payne robot. Payne cannot be deleted.

Client rejects other MIME. Server re-checks. Max 3 files, 4096 bytes each.

`POST /api/agent {name, system_prompt, provider, save_pass, picture?, context_name_0, context_mime_0, context_text_0, …_2}`.

Delete: `POST /api/agent {action:"delete", slug}`.

Goose/Payne skill: `.goose/skills/agent-create/SKILL.md`.

## Data / API (additive)

| Route | Role |
|---|---|
| `GET /api/session` | existing + `profile`, `theme`, `agents[]`, `members[]` |
| `POST /api/identity` | `create` \| `import` \| `ack_backup` \| **`logout`** |
| `POST /api/profile` | first/last/email/org/theme; optional avatar b64 |
| `POST /api/agent` | create agent + context |
| `POST /api/member` | add human (npub) |
| `GET /avatar/<64-hex>` | stored picture bytes |

Session `ready` unchanged: `logged_in && backup_acked && has_vibe`.

## Visual language
- Dark default tokens stay. Themes override CSS variables on `html[data-theme]`.
- Color-blind: blue / orange, never green / red as the sole pair.
- Feather 36–72px on splash, no animation beyond a quiet fade if cheap.
- Author pills: you / Payne / agent / human.

## Hick cuts
- Header ≤5 actions.
- Onboard: one primary button.
- Themes only in Settings.
- Agent + human add are drawers, not splash steps.

## Caps (named, one site each)
- HTTP recv `HUSH_BUF_SZ = 65536` (was 8192; HTTP JSON + small avatar).
- Session JSON `HUSH_LAUNCH_JSON_MAX = 16384`.
- Context: 3 files × 4096 bytes.
- Avatar on disk: sniffed JPEG/PNG only; client downscales ≤96px.
- Kind 0 `picture` is a URL, never a data URI (`HUSH_EVENT_MAX_CONTENT = 4096`).

## Non-negotiables
- pass checkbox default checked + retrieve CLI.
- Profile/Settings visible before ready.
- Payne always first in the robot card list when vibe present.
- A robot cannot be raised without a system prompt and an AI provider.
- No catfu / Griffe / Scout / Brave / feline words.
- Embed after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo` from the worktree root.
- write-legible-c §14 on every C commit.
