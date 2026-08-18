# Hush UI Spec — Onboard + Profile + Vibe Members + Agent Create + Close/Exit + Provider Configure + Mentions + Channel Groups
Version: 2026-08-18 (RDAP M2, gb/oauth-mention-groups)
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

`has_vibe` is restored from `$XDG_CONFIG_HOME/hush/vibe.json` (else
`$HOME/.config/hush/vibe.json`) after identity restore or nsec import.
`make clean` / `make install` must not force “Name your vibe” again.
Tests override the directory with `HUSH_CONFIG_DIR`. The file never
holds an nsec or provider secret.

### 4. Hive
- Header: hush + vibe name + Install + Profile + Settings + Call
  + **Close** + **Exit** + badge.
- Sidebar: Channels; Create channel/project; **Raise a robot**;
  **Invite human**; **Robots** list (Payne first, then raised agents).
  Each robot is its own card with a 44px `+`/`-` expand/collapse.
- Stream + composer unchanged in spirit. Empty: Payne directive.

### 10. Close vs Exit (process lifecycle)

The OS/PWA window `×` is **not** our Close and **not** our Exit.
The hive ships two labeled buttons in the header, always visible
(including splash/gate).

| Button | Id | Meaning |
|---|---|---|
| **Close** | `#hive-close` | Detach the GUI. Relay stays up. Next launcher click re-attaches. |
| **Exit** | `#hive-exit` | Quit. Every process stops. Exit code 0. |

- Close: `POST /api/close` then `window.close()`. If the window stays
  (a tab the script did not open), show `#hive-banner`:
  "Window stays open here. Close this window. The hive is still standing."
- Exit: confirm "Quit the hive? Every process stops." then
  `POST /api/exit` then `window.close()`.
- Close is ghost `iconbtn`. Exit is danger `iconbtn`. Both ≥44px.
- Titles: Close = "Close the window. Hive stays standing."
  Exit = "Quit the hive. Every process stops."
- Drawer "Close" buttons stay local (Settings / Profile / Raise).
  They never quit the process.

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
- Hive metadata (name, about, visibility, token, channels, projects,
  profile sans email, members, raised-robot labels) persists in
  `~/.config/hush/vibe.json` (0600) and survives rebuild / Exit.

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
  Selecting a radio reveals a 44px pencil (`#provider-cfg`) on that
  row. Pencil opens `#provider-drawer` (see §11). Configure is
  optional for Raise — the robot still stores only the provider id.
- pass checkbox default-on:
  `Checked to save password to Unix Password Manager. Retrieve with: pass show hush/agents/<slug>/nsec`
- CTA: **Raise this robot**.
- Red **Delete this robot** at the bottom. Disabled on a fresh raise.
  Enabled when editing an existing non-Payne robot. Payne cannot be deleted.

Client rejects other MIME. Server re-checks. Max 3 files, 4096 bytes each.

`POST /api/agent {name, system_prompt, provider, save_pass, picture?, context_name_0, context_mime_0, context_text_0, …_2}`.

Delete: `POST /api/agent {action:"delete", slug}`.

Goose/Payne skill: `.goose/skills/agent-create/SKILL.md`.

### 11. Provider configure (pencil per runtime)

The OS/PWA `×` is not Configure. Drawer Close on this panel only
dismisses it. Hive Close/Exit stay in the header.

Selecting a provider radio shows `#provider-cfg` (pencil ✎, ≥44px,
title “Configure this provider.”) next to that label. Click opens
`#provider-drawer`. Fields depend on the family. Status is loaded
from `GET /api/provider`. Save is `POST /api/provider`. Scan is
`POST /api/provider/scan`. Official CLI login is
`POST /api/provider/login`.

| Family | Ids | Drawer |
|---|---|---|
| OAuth CLI | `grok-build`, `codex` | Status + one **Log in with OAuth** button. No API-key dump, no Save. The button `POST`s `/api/provider/login` then records `use_home`. Grok runs `grok login --oauth`. Codex runs `codex login`. Hush never writes `~/.grok` or `~/.codex`. |
| Home-config CLI | `goose` | Status (binary / home config / active model). Primary: **Use existing configuration**. Secondary: optional API key + host + model as `+`/`−` pills. Copy: `goose configure`. Never write Goose homes. |
| HTTP API | `gemini-api`, `xai-api`, `openai-api`, `anthropic-api` | API key, username, password, token, passkey, host, and model as `+`/`−` pill rows (same pattern as Raise-robot name / system prompt). Host URL defaulted. **Scan models** then type-in. |
| Editor agent | `cline` | Honest empty state if Cline is missing. Cline authenticates with **ClinePass**, usage billing, or **bring-your-own provider key** — not a Grok/Codex-style OAuth-first CLI. Optional credentials as the same `+`/`−` pills. |

Goose home is `~/.config/goose/config.yaml` (official). Legacy
`~/.goose` is probed and mentioned only if it exists.
Grok home is `~/.grok/auth.json`. Codex home is `~/.codex`.

Payne copy:

- Existing Goose: “Use the Goose already standing in ~/.config/goose.”
- Missing Goose: “Goose is not configured here. Run goose configure, or enter a key.”
- Scan fail: “Could not list models. Type the model name.”
- Key saved: “Key is in pass. Retrieve with: pass show hush/providers/<id>/api_key”
- Other secrets: “Stored in pass. Never shown again.” After save, retrieve CLI for each `has_*`.
- Payne: “Credentials live in pass. I will not show them twice.”

Provider secrets Hush accepts (API key, username, password, token,
passkey) live only in `pass`:

| Kind | Retrieve |
|---|---|
| API key | `pass show hush/providers/<id>/api_key` |
| Username | `pass show hush/providers/<id>/username` |
| Password | `pass show hush/providers/<id>/password` |
| Token | `pass show hush/providers/<id>/token` |
| Passkey | `pass show hush/providers/<id>/passkey` |

Foreign homes (`~/.config/goose/secrets.yaml`, `~/.grok/auth.json`,
`~/.codex`, Cline editor store) are never copied into `pass`.

Keys never appear in `GET /api/session` or `GET /api/provider`.
`has_key`, `has_username`, `has_password`, `has_token`, `has_passkey`
are booleans. Secret kind names are never GET JSON keys. Deepseek is
not a radio this slice.

## Data / API (additive)

| Route | Role |
|---|---|
| `GET /api/session` | existing + `profile`, `theme`, `agents[]`, `members[]` |
| `POST /api/identity` | `create` \| `import` \| `ack_backup` \| **`logout`** |
| `POST /api/profile` | first/last/email/org/theme; optional avatar b64 |
| `POST /api/agent` | create agent + context |
| `POST /api/member` | add human (npub) |
| `POST /api/close` | acknowledge Close; does **not** stop the process |
| `POST /api/exit` | acknowledge Exit; sets shutdown flag; process exits 0 |
| `GET /api/provider` | status per id: family, has_binary, has_home, has_key, has_username, has_password, has_token, has_passkey, use_home, host, model, configured. Never secret values or secret field names. |
| `POST /api/provider` | `{provider, use_home?, host?, model?, api_key?, username?, password?, token?, passkey?}` save overlay + optional secrets to pass |
| `POST /api/provider/scan` | `{provider, host?, api_key?}` → `model_0`…`model_31` or `{ok:false,error}`. Empty key loads `api_key` then `token` from pass. |
| `POST /api/provider/login` | `{provider}` starts the official CLI login. `grok-build` → `grok login --oauth`. `codex` → `codex login`. Goose and unknown ids return `{ok:false,error}`. Does not wait. With `DISPLAY`, spawn `xterm -hold -e` (not `x-terminal-emulator`: COSMIC Term ignores `-e`). Tests set `HUSH_PROVIDER_TERM`. |
| `POST /api/channel` | `{action:"create"\|"delete"\|"group"\|"ungroup"\|"manage", name?, slug?, group?, group_id?, human_0…human_7?, robot_0…robot_7?}`. Create needs `name`. Delete/group/ungroup/manage need `slug`. |
| `POST /api/group` | `{name}` creates a parent group with its own UUID. |
| `POST /api/event` | existing + optional `mention_0`…`mention_7` (npub or hex) stored as `p` tags. Content may contain `nostr:npub1…`. |
| `GET /avatar/<64-hex>` | stored picture bytes |

Session `ready` unchanged: `logged_in && backup_acked && has_vibe`.
Session `channels[]` now include `id` (32-hex UUID), `group_id`,
`humans[]`, `robots[]`. Session `groups[]` is `{name,id}`.

### 12. OAuth signed-in (Grok Build / Codex)

`has_home` is the authenticated signal. `use_home` and `configured` are
not enough — login sets `use_home` before the CLI writes the home file.

- After **Log in with OAuth**, poll `GET /api/provider` every 2s for 90s.
- While waiting: “A terminal is running the official login. Finish
  sign-in in that window (a browser should open).”
- When `has_home` is true: “Grok Build is authenticated. Close the
  login browser and the terminal. Carry on.” (Codex: same shape.)
- That provider radio gets class `ready`: accent-tinted box, a
  checkmark after the label, and a one-line “authenticated” hint.
- Color-blind theme uses `--accent` (blue) / `--warn` (orange). Never
  green/red as the only pair.
- Timeout without `has_home`: “Still waiting on the official login.
  Close extra windows when it says you are signed in, then reopen
  Configure.”

### 13. @ mentions (NIP-27)

Composer `@` after start-of-input or whitespace opens `#mention-box`.
Roster: the signed-in human, Payne, raised robots, invited humans.
Arrow keys + Enter or click. Inserts `nostr:<npub>` in the posted
content and records that pubkey as `mention_N`. Render replaces
`nostr:npub1…` with `@Name`. Mentioning a robot addresses it; this
slice does not spawn a live reply.

### 14. Channel groups + manage (NIP-29 parent)

A Hush channel is a NIP-29-shaped group: random 32-hex `id`, `#h` on
the wire stays the **slug** so existing notes keep matching. A Hush
**Group** is a NIP-29 parent (`groups[].id`). `channel.group_id` empty
means ungrouped. Membership does not inherit (NIP-29 subgroup rule).

Sidebar: grouped channels under the group name, then ungrouped.
Each row: name, red `−` (`#chan-del`, ≥44px) deletes after confirm
(“Delete #slug? Notes stay on disk.”). Last channel cannot be deleted.

Right-click a channel (`#chan-menu`):

- Add To Group — pick an existing group or “New group…”
- Remove From Group — no-op if ungrouped
- Delete Channel — same confirm as `−`
- Manage Channel — `#manage-chan` modal

==Manage Channel==

- Add/Invite/Remove Humans (hive members + free npub field)
- Add/Invite/Remove Robots (Payne + raised)
- Empty lists mean the whole hive (open, current behavior)
- [Save] [Close]

Payne: “Name the group. Put the right humans and robots on the
channel. Delete only when the room is done.”

## Visual language
- Dark default tokens stay. Themes override CSS variables on `html[data-theme]`.
- Color-blind: blue / orange, never green / red as the sole pair.
- Feather 36–72px on splash, no animation beyond a quiet fade if cheap.
- Author pills: you / Payne / agent / human.

## Hick cuts
- Header primary choices: Profile, Settings, Call-when-ready, Close, Exit.
  Install is opportunistic. Badge is status.
- Onboard: one primary button.
- Themes only in Settings.
- Agent + human add are drawers, not splash steps.

## Caps (named, one site each)
- HTTP recv `HUSH_BUF_SZ = 65536` (was 8192; HTTP JSON + small avatar).
- Session JSON `HUSH_LAUNCH_JSON_MAX = 32768`.
- Groups: 8. Humans per channel: 8. Robots per channel: 8.
- Channel / group id: 32 hex (`HUSH_LAUNCH_ID_HEX`).
- Context: 3 files × 4096 bytes.
- Provider models: 32 names × 64 bytes (`HUSH_PROVIDER_MODELS_MAX`).
- Provider overlay: `$XDG_CONFIG_HOME/hush/providers.json` (0600).
- Vibe overlay: `$XDG_CONFIG_HOME/hush/vibe.json` (0600). Override
  directory with `HUSH_CONFIG_DIR` (tests). Never nsec.
- Avatar on disk: sniffed JPEG/PNG only; client downscales ≤96px.
- Kind 0 `picture` is a URL, never a data URI (`HUSH_EVENT_MAX_CONTENT = 4096`).

## Non-negotiables
- pass checkbox default checked + retrieve CLI.
- Profile/Settings visible before ready.
- Payne always first in the robot card list when vibe present.
- A robot cannot be raised without a system prompt and an AI provider.
- Provider secrets live only in `pass` (`hush/providers/<id>/{api_key,username,password,token,passkey}`).
- Hush never writes `~/.config/goose`, `~/.grok`, or `~/.codex`.
- Close and Exit are distinct labeled hive buttons. The OS `×` is neither.
- Close never kills the relay. Exit always does, with exit code 0.
- No catfu / Griffe / Scout / Brave / feline words.
- Embed after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo` from the worktree root.
- write-legible-c §14 on every C commit.
