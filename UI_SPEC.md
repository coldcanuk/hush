# Hush UI Spec — Onboard + Profile + Vibe Members + Agent Create + Close/Exit + Provider Configure + Mentions + Channel Groups + Channel Policy
Version: 2026-08-19 (RDAP M2, gb/rail-prov)
Authoritative for this slice. Supersedes splash-only notes in the 2026-08-18
M2.1 splash spec, the onboard raise-form notes, the oauth-mention-groups
header/mention/manage rows, the oauth-mention-rail indent-only thread,
the thread-think-hygiene Close/Exit paragraph, the oauth-mention-rail
six-dock tool-rail paragraph, the membership-only Manage Channel
paragraph, the Deepseek radio slice, the 1:1 follow-up inherit
slice, the Payne provider-order slice, the one-joke-snip
paragraph, the thread-ux rail/minimize paragraph, the
rail-win canvas paragraph, and the canvas-fim rail paragraph
where they conflict.
Quinn + Parker + Payne. Robot pictures may use the shipped cats sheet
(`icon_panel_cats.png`); other UI chrome stays non-feline.

## Core Principles (Quinn)
- Cognitive Load Index ≤ 3/10. Gestalt Clarity ≥ 85/100.
- Hick: ≤5 primary visible choices. Themes live in Settings, not header.
- Fitts: min 44px tap on `#install` and `#rail-toggle`. Compact rail
  pair buttons may be 32px tall. 10px padding.
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
- Line: "Major reporting for duty."
- Sub: "Detecting identity and vibe…"
- Poll `/api/session`. If `ready` → hive. Else **Begin** → wizard step 1.
- Header always: brand + badge. Actions live on `#tool-rail` (§15).

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

`has_vibe` is restored from `$HUSH_HOME/config/vibe.json` (else
`$HOME/.hush/config/vibe.json`). If that file is missing and
`HUSH_CONFIG_DIR` is unset, first-run still reads the legacy
`$HOME/.config/hush/vibe.json`. Writes go to the new/override path.
`make clean` / `make install` must not force “Name your vibe” again.
Tests override with `HUSH_CONFIG_DIR` / `HUSH_HOME`. The file never
holds an nsec or provider secret. Install and first-run create
`~/.hush/config/` and `~/.hush/agents/`.

### 4. Hive
- Header: hush + vibe name + badge. Actions live on `#tool-rail` (§15).
- Sidebar: Channels; **Robots inventory** (Diablo/Vein style spatial grid).
  Compact default is **4 cols × 3 rows**, centered, and must not exceed
  the sidebar/nav width. Expand opens a larger 8×5 grid in a drawer.
  Every robot occupies **one equal 1×1 cell** the size of the avatar icon.
  A chosen avatar **is** the tile. Hover shows the robot name (`title`).
  With no avatar, the cyan border/highlight and name label remain.
  Right-click (macOS: meta+click) opens `#inv-menu` with one action: `edit`.
  Click outside closes the menu. `edit` opens `#agent-drawer`.
  Grid snap + occupancy. Neon/brass cyberpunk-steampunk theme.
  Create/raise from rail or inventory header. Payne (`Major`) is a live
  roster tile. Persist slot layout by slug (always 1×1).
  Seed / Clear / Raise are one equal button class. Seed is a Payne-gated
  team briefing (not local Cosplay tiles).
  Create lives on `#tool-rail` (§15) and inside inventory.
  Skills do **not** live on this nav. Edit (right-click, or `i` on a selected
  robot tile) is the Diablo II inventory screen. Raise is the same panel
  for a new robot.
- Stream + composer unchanged in spirit. Empty: Payne directive.

### 10. Close vs Exit (process lifecycle)

The OS/PWA window `×` belongs to the Chromium-family `--app` window. It
is **not** our chrome. Page JS cannot put three labeled buttons on that
close-box (`beforeunload` is a two-button "Leave site?" at best, often
suppressed). `--open` launches with `--ozone-platform=x11` and the
relay Motif-undecorates the `WM_CLASS=hush-relay` window so that bar
is gone. Close and Exit stay on the rail. This is **frameless
standalone `--app`**, not `display: fullscreen` and not a
window-controls-overlay PWA. Manifest stays `"display": "standalone"`.

The hive ships two labeled buttons on `#tool-rail`, always reachable
(including splash/gate). Both open one chooser, `#hive-leave`.

| Control | Id | Meaning |
|---|---|---|
| **Close** (rail) | `#hive-close` | Open `#hive-leave`. Does not detach by itself. |
| **Exit** (rail) | `#hive-exit` | Open `#hive-leave`. Does not quit by itself. |
| **Exit the application** | `#leave-exit` | `POST /api/exit` then `window.close()`. No second confirm. Every process stops. Exit code 0. |
| **Close the window** | `#leave-close` | `POST /api/close` then `window.close()`. Relay stays. Next launcher click re-attaches. |
| **Cancel** | `#leave-cancel` | Hide `#hive-leave`. Window stays. Relay stays. |

- `#hive-leave` is a `.drawer` / `.panel` titled "Leave the hive?".
  Help: "Exit stops every process. Close leaves the hive standing. Cancel stays."
  The three action buttons stack, each Fitts ≥44px. Exit is `.btn.danger`.
  Close is `.btn`. Cancel is `.btn.ghost`. Escape = Cancel.
- `#leave-close`: if `window.close()` leaves the document up (a tab the
  script did not open), show `#hive-banner`:
  "Window stays open here. Close this window. The hive is still standing."
- Rail Close is ghost `iconbtn`. Rail Exit is danger `iconbtn`. Both ≥44px.
  Titles: Close = "Close the window. Hive stays standing."
  Exit = "Quit the hive. Every process stops."
- Drawer "Close" / `[x]` buttons on Settings / Profile / Raise / Thread /
  Relay-live stay local. They never open `#hive-leave` and never quit.
- Last `--app` child gone: the relay notices with `kill(pid, 0)` in the
  poll pump (`SIGCHLD` stays ignored). `g_saw_app` latches only after a
  tracked child is a live `--app` cmdline (`--class=hush-relay` plus
  `--app=http://127.0.0.1:<port>/`). The launcher fork is not that
  window. Launch must not raise zenity. If the latch was set, no
  `/api/close` or `/api/exit` ran this session (`g_leave_ack` clear),
  and `g_shutdown` is clear, the pump forks `zenity --question`
  (non-blocking) with the same three verbs.
  Missing zenity: print the attach hint and leave the hive standing.
  Zenity **Exit the application** sets shutdown. **Close the window**
  acks and stays. **Cancel** re-opens the `--app` window.
- Close never kills the relay. Exit always does. Attach on EADDRINUSE
  stays. Do not use `beforeunload` as this UI.

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
- **Developer Logging** switch (default: off/disabled).
  When enabled: opens a separate "Developer Log" drawer/panel (syslog format: `timestamp [robot-or-system] message`).
  All "Mention received.", on-deck intros, debug/ack lines, internal notes route exclusively here.
  Main chat and threads never show these when disabled.
  Toggle persisted in session/profile; separate window-like drawer (similar to `#thread-pane` or `#code-canvas`).
  "Mention received" and robot intros are **never** normal chat notes.

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
  OpenAI API, Anthropic API, Deepseek API.
  Wire ids: `goose`, `grok-build`, `codex`, `cline`,
  `gemini-api`, `xai-api`, `openai-api`, `anthropic-api`,
  `deepseek-api`.
  Selecting a radio reveals a 44px pencil (`#provider-cfg`) on that
  row. Pencil opens `#provider-drawer` (see §11). Configure is
  optional for Raise — the robot still stores only the provider id.
- pass checkbox default-on:
  `Checked to save password to Unix Password Manager. Retrieve with: pass show hush/agents/<slug>/nsec`
- Footer `#agent-drawer .actions` is one compact line (no wrap):
  **Raise Robot** (edit: **Save Robot**), **Close**, **Delete Robot**.
  Delete is disabled on a fresh raise. Enabled when editing an
  existing non-Payne robot. Payne cannot be deleted. Confirm stays
  `Delete this robot?`. Compact min-height 36px in this drawer only.

**Payne Edit.** Drawer title is `Edit Major`. Display name is locked
`Major` (`HUSH_LAUNCH_PAYNE_NAME`). Standing orders are locked
(`HUSH_LAUNCH_PAYNE_ABOUT`). Name and system-prompt fields are
read-only. Slug stays `sgt-major-payne`. Delete stays disabled.
Every robot, including Major, has an Enable/Disable switch
(`#agent-enabled`). Disabled robots use `.inv-item.disabled`
(greyscale) and do not answer mentions. Raise and Edit also have
`#agent-intro-enabled` (on by default) and `#agent-intro` (default
“I am on deck. Standing orders are noted.”). The intro posts once per
thread. Major’s intro switch and text are locked. Ranked providers remain.
Skills live only in Raise / Edit (`#agent-drawer`), never on the hive nav.
Edit is the inventory: `i` toggles it for the selected or hovered robot
tile. Cycle `#skill-cycle`; armory groups **System** (application-wide) and
**This robot** only. `#skill-loadout` is a 3×3 doll around the portrait.
Pick a gem, drop it on an empty socket. Lift a worn gem to prune. Forge
radios: System (`scope=user`, hive-wide file) / This robot (`scope=robot`).
A `robot:<other>:<name>` skill is refused on this slug. Forge still writes
a SKILL.md. `POST /api/agent` with the
Payne slug updates providers, picture, voice, skills, and `enabled`.
Name and `system_prompt` on that POST are ignored. Session
`payne.provider` is `providers[0]`; `payne.providers` is the full
array. Missing list restores to `["goose"]`. `payne.enabled` defaults
true.

Client rejects other MIME. Server re-checks. Max 3 files, 4096 bytes each.

`POST /api/agent {name, system_prompt, provider, save_pass, picture?, context_name_0, context_mime_0, context_text_0, …_2}`.
Payne update: `POST /api/agent {slug:"sgt-major-payne", provider_0…N}`.

Delete: `POST /api/agent {action:"delete", slug}`. Payne slug is refused.

Goose/Payne skill: `.goose/skills/agent-create/SKILL.md`.

### 11. Provider configure (hive-wide desk + pencil)

The OS/PWA `×` is not Configure. Drawer Close on this panel only
dismisses it. Hive Close/Exit stay in the header.

**Two surfaces, one secret store.** Credentials, OAuth, host, and
model are hive-global (`providers.json` + `pass` + home detect).
A robot stores only a provider id (Payne stores a ranked list).
There is no per-robot secret overlay this slice.

**Central desk.** `#providers-btn` on the rail opens `#providers-hub`.
The hub lists every named runtime from `GET /api/provider`. Each row
shows label, family, `providerStatusLine`, OAuth `ready` when
`has_home`, and the robots (including Payne) that currently point at
that id. Row click or the row ✎ calls `openProviderDrawer(id)` and
opens the existing `#provider-drawer` for that id. The hub stays
under the detail drawer (hub earlier in the DOM). Close on the
detail returns to the hub.

**Per-robot pick.** Left-nav Edit / Raise still chooses which id a
robot uses. Selecting a provider radio shows `#provider-cfg` (pencil
✎, ≥44px, title “Configure this provider.”) next to that label.
Click opens the same `#provider-drawer`. Configure is optional for
Raise — the robot still stores only the provider id.

Fields depend on the family. Status is loaded from `GET /api/provider`.
Save is `POST /api/provider`. Scan is `POST /api/provider/scan`.
Official CLI login is `POST /api/provider/login`.

| Family | Ids | Drawer |
|---|---|---|
| OAuth CLI | `grok-build`, `codex` | Status + one **Log in with OAuth** button. No API-key dump, no Save. The button `POST`s `/api/provider/login` then records `use_home`. Grok runs `grok login --oauth`. Codex runs `codex login`. Hush never writes `~/.grok` or `~/.codex`. |
| Home-config CLI | `goose` | Status (binary / home config / active model). Primary: **Use existing configuration**. Secondary: optional API key + host + model as `+`/`−` pills. Copy: `goose configure`. Never write Goose homes. |
| HTTP API | `gemini-api`, `xai-api`, `openai-api`, `anthropic-api`, `deepseek-api` | API key, username, password, token, passkey, host, and model as `+`/`−` pill rows (same pattern as Raise-robot name / system prompt). Host URL defaulted. **Scan models** then type-in. Deepseek host `https://api.deepseek.com`. Models `deepseek-v4-pro` / `deepseek-v4-flash`. |
| Editor agent | `cline` | Honest empty state if Cline is missing. Cline authenticates with **ClinePass**, usage billing, or **bring-your-own provider key** — not a Grok/Codex-style OAuth-first CLI. Optional credentials as the same `+`/`−` pills. |

Goose home is `~/.config/goose/config.yaml` (official). Legacy
`~/.goose` is probed and mentioned only if it exists.
Grok home is nonempty `~/.grok/auth.json`. Codex home is nonempty
`~/.codex/auth.json` or `~/.codex/config.toml`.

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
wire id `deepseek-api`, family `api`, same drawer as OpenAI.

**Channel topics & per-channel system pointers:** Channels store
an `about` / topic string (set via pills or manage-chan). This is
injected into every robot prompt on that channel as a short "Channel
topic: ..." pointer (quick context for the LLM). Topics are first-class
pills in the UI. Per-channel `robot_reply` / `robot_talk` policy also
lives here and affects deliberation.

**Inventory robots decision (M2):** Primary implementation is a web
/CSS/JS grid inside the existing embed (compatible with current
single-binary model, no new native deps). Aesthetic and interaction
exactly match the provided Raylib reference (dark industrial,
neon/brass borders, drag-snap-occupancy, variable sizes, names).
Optional: `examples/inventory-raylib/` standalone prototype (only
built if raylib present; copies user atlases from ~/Pictures for
demo). No Raylib dependency on the main hush-relay.

## M2 Architecture Decisions (locked for this feature)
- **Mention fidelity & UX**: Composer keeps `@Name` in the typed sentence (pills list who is in the bubble). Stored content uses `nostr:` at those offsets. Render turns those tokens into in-sentence pills (do not run `prettyMentions` first). Progressive states ("thinking" → "reacting" → emoji); notes older than 2s skip to emoji unless a live job exists. One short intro per robot+thread is chat; repeats are not posted. "Mention received" is a developer log only.
- **Developer Logging**: Dedicated toggle (default off) + separate panel. Syslog format. Log notes never appear in main chat or threads, even when the toggle is on.
- **Multi-robot deliberation**: Co-mention prompt + hygiene + peer p-mention support. Robots decide strategy (individual/coop/split/convo).
- **Channel topics**: `about` string injected as "Channel topic: ..." quick pointer. Pills in UI.
- **Tool rail**: Micro UX (grip affordance, consistent i buttons, labels, progressive disclosure).
- **Robots inventory**: Web grid primary (CSS/JS 2D spatial, drag-snap-occupancy, variable sizes, cyber/steampunk). Matches Gemini/Raylib spec. Raylib optional standalone example only (no dep on core relay). Create/raise populates slots.
- **Progressive acks**: Reuse/extend existing `status.thinking` + client transient chips before final emoji.
- Decision rationale: Preserve embed model (no new hard deps for core), maximize compatibility, follow RDAP small atomic wins + user "Diablo/Vein" + "natural conversation flow" requirements.

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
| `POST /api/channel` | `{action:"create"\|"delete"\|"group"\|"ungroup"\|"manage", name?, slug?, group?, group_id?, human_0…human_7?, robot_0…robot_7?, kind?, robot_reply?, robot_talk?, burst_ms?, max_jobs?, cooldown_s?}`. Create needs `name`. Delete/group/ungroup/manage need `slug`. Manage may also set the policy leash (§20). |
| `POST /api/group` | `{name}` creates a parent group with its own UUID. |
| `GET /api/status` | existing + `thinking` (JSON array of `{name,parent}` for in-flight robot jobs). |
| `GET /api/events` | notes plus `reply_to` (first `e` tag; empty when the note is not a reply). |
| `POST /api/event` | existing + optional `mention_0`…`mention_7` (npub or hex) stored as `p` tags + optional `reply_to` stored as `e` (the **root** id). Content may contain `nostr:npub1…`. Mentions enter `hush_intel` (burst / policy) before `hush_agent`. |
| `POST /api/canvas` | `{project, path, content}` writes `content` to `path` under that launch project's directory. `project` is a slug. `path` is a relative file (no `..`, no absolute). Missing project, empty content, or a path that escapes the project → `{ok:false,error}`. |
| `POST /api/fixup` | `{instruction, text}` runs a one-shot `grok -p` and returns `{ok:true,text}` or `{ok:false,error}`. Does **not** insert a hive note. Instruction max 500. Text max `HUSH_EVENT_MAX_CONTENT`. |
| `POST /api/complete` | `{prefix, suffix}` starts a Fill-in-the-Middle job and returns `{ok:true,token}` immediately. Does **not** wait on Grok. Does **not** insert a hive note. A new start replaces any in-flight job. |
| `GET /api/complete?t=` | `{ok:true,pending:true}` while the token is busy, `{ok:true,text}` when ready (max `HUSH_CANVAS_PRED_MAX` = 512), or `{ok:false,error}`. |
| `POST /api/window` | `{action:"minimize"\|"maximize"}`. Iconify or toggle WM maximize on the `--app` window (`WM_CLASS=hush-relay`). Does **not** insert a note and does **not** shut down. No window / no X11 → `{ok:false,error}`. |
| `GET /avatar/<64-hex>` | stored picture bytes |

Session `ready` unchanged: `logged_in && backup_acked && has_vibe`.
Session `channels[]` now include `id` (32-hex UUID), `group_id`,
`humans[]`, `robots[]`, `kind`, `robot_reply`, `robot_talk`,
`burst_ms`, `max_jobs`, `cooldown_s`. Session `groups[]` is `{name,id}`.

### 12. OAuth signed-in (Grok Build / Codex)

`has_home` is the authenticated signal, and it is **per provider**.
`use_home` and `configured` are not enough — login sets `use_home`
before the CLI writes the home file. A leftover sibling home directory
must not light up another radio.

- Grok Build `has_home`: nonempty regular file `~/.grok/auth.json`.
- Codex `has_home`: nonempty regular file `~/.codex/auth.json` or
  `~/.codex/config.toml`. `stat(~/.codex)` on the directory is not auth.
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

### 13. @ mentions, thinking, thread pane

`#msg` and `#thread-msg` are `<textarea rows="6">` (not a one-line
`<input>`). They word-wrap. Height is six line-boxes; overflow-y
scrolls after that. `resize: none`. Enter submits. Shift+Enter
inserts a newline. Mention Enter still applies while `#mention-box`
/ `#thread-mention` is open.

Composer `@` after start-of-input or whitespace opens `#mention-box`.
Roster: the signed-in human, Payne, raised robots, invited humans.
Arrow keys + Enter or click. The visible composer shows an `@Name`
pill (`.composer-pill`). The input never displays `nostr:npub1…`.
Submit serializes each pill to `nostr:<npub>` in the posted content
and records that pubkey as `mention_N`.

**Mention fidelity (order & position critical):** The original text
positions of `@Name` (or raw `nostr:npub`) in the human's sentence are
preserved in rendering. Pills are non-destructive overlays or styled
spans; they do not globally replace and reorder the source content.
E.g. "@Happy tell me a joke. @Sgt Major Payne was it funny?" keeps
the exact sequence and locations so intent ("tell Happy the joke,
then ask Sgt about it") is not lost. `prettyMentions` or equivalent
must be order-preserving; `paintNote` / `paintThreadStream` render
the original text flow with mention markers in place. Mention pills
resolve `nostr:npub1…` and `@npub1…` with the same prefix match as
`who()` (`sameKey`), so a truncated key still paints `@Major`. Robot
notes rewrite `@Name` and truncated npubs to full `nostr:<npub>`
before store. The last robot on a note is told to stop after its
assignment.

**Progressive mention acknowledgment UX:** On mention dispatch:
1. Immediate visual "Happy is thinking..." (or per-robot status chip)
   using existing `status.thinking` + client transient state.
2. "Happy is reacting..." when job starts producing.
3. Final robot-ack only via emoji pill (👍 🎯 etc.) in `.robot-acks`.
   "Mention received." text is **never** a visible chat note (see
   Developer Logging).

Robot intros ("At ease...") appear at most once per robot per session
or thread root (single on-deck guard). Subsequent mentions use only
emoji ack + normal reply.

Mentioning a robot starts a thread. The channel `#stream` lists **root**
notes only (empty `reply_to`). A root with replies or a live job shows
`.thread-btn` (“Thread · N”). Click opens `#thread-pane`.

`#thread-pane` is a floating hive panel (same `--surface` / `--line` /
`.note` / composer tokens as the channel), not a dimmed 28rem modal.
Default size `min(42rem, 92vw)` × `min(70vh, 640px)`. `#thread-resize`
(bottom-right, ≥24px) lets the human drag a new size. Persist
`{w,h}` in `localStorage.hush-thread`. Clamp to the viewport.

The pane supports **1:1** (you + one robot) and **1:n** (you + every
robot that was mentioned on the root or that authored a descendant).
Title: `Thread · Happy` or `Thread · Happy, Payne`. Subline is
`1:1 with Happy` or `1:n · you + Happy, Payne` — not a Payne voice
line and not a chat bubble. Stream: the root plus descendants from
any of those participants. Notes in `#thread-stream` are sided
bubbles (`.note.mine` for the signed-in human, other notes left).
`who()` matches event hex pubkeys to roster `pubkey` / `npub`.
`paintThreadStream` rebuilds the stream only when the note/think key
changes. It pins `scrollTop` to the bottom only when the pane just
opened or the human is already within `THREAD_PIN_PX` (48) of the
bottom. A 1 s `tick()` must not yank a scrolled-up thread back down.
A Grok follow-up receives the last few thread turns so it does not
repeat a prior joke.

Composer inside the pane reuses the hive `.composer-box`:
`#thread-pills` + `#thread-msg` leftover + `#thread-mention` `@`
box (full hive roster, so a 1:1 thread can become 1:n). Submit posts
`reply_to=<root id>` and `mention_0…N` for **new** pills typed in
this send. **1:1 inherit:** when this send has no robot pill and
`threadMembers` is exactly one robot, attach that sole member as
`mention_0` (and prefix `nostr:<npub>` so the bubble reads `@Happy`).
Do not re-mention every robot already in a 1:n member set.
Do not mention the human. After a send that attached a mention, paint
an optimistic `#thread-think` chip until live `status.thinking` or a
new robot reply arrives. `#thread-close`
[x] and Escape return to the channel. The same Thread button reopens
the same root.

While the pane is open the tool rail is forced to its hamburger and
parked at the brand home (§15). It must not paint inside the pane.

A Grok Build robot with `has_home` is invoked via `grok -p` in an
empty `--cwd` (no `AGENTS.md`), `--max-turns 2`
(named `HUSH_AGENT_GROK_TURNS`; one turn was enough for a joke and
not enough for a multi-part ask), `--reasoning-effort low` (named
`HUSH_AGENT_GROK_EFFORT`; grok 1.0.4 rejects `none`), `--no-memory`,
`--no-subagents`, `--disable-web-search`, and `--disallowed-tools`
covering shell / web / files / Agent. The override plus `--rules`
demand the note fulfill the last human ask (include any asked code;
no preamble-only replies), address the human by profile first name
(else “you”), and forbid status banners, thoughts, and npubs. If the
last human ask is a joke, reply with exactly one joke. The thread
transcript passed as `-p` is built by `hush_agent_snip_line`: each
prior note is flattened (space / tab / CR / LF → one space, 160
visible chars) so a second joke after a newline is still visible to
“Do not repeat a prior joke.” Still one job → one kind-1 note.
The reply is kind 1, `e` = **root** (the triggering note’s `e` if
already set, else that note’s id), `p` human. Other mentioned robots
post a short on-deck note so the thread still appears.

While a job is busy, `GET /api/status.thinking[]` lists
`{name,parent}`. The matching hive root shows `.think`: an 8px pulsing
accent dot and “\<name\> is thinking” (`aria-live="polite"`). Inside
`#thread-pane` the same chip sits in `#thread-think` above the
composer — not on the scrolled-away root — and Send is disabled until
the job leaves the array. A second mention of that robot on the same
root while the job is busy does not start another grok child.

JSON strings on `GET /api/events` (and every other `hush_json_escape`
site) must be RFC 8259: `"`, `\`, `\n`, `\r`, `\t`, and every other
C0 byte as `\u00HH`. A raw TAB in a Go snippet must not break
`Response.json()`. A failed events parse must not leave the thinking
chip stuck (the chip keys on `status.thinking` plus a successful
events tick).

Fenced code in a note (` ```lang ` at line start … closing fence)
paints as `<pre class="code-block" data-lang><code>`. Prose stays a
`.body` node. A note with one or more fences grows Download and
Canvas actions. `#code-canvas` is a right-hand pane
(`min(36rem, 46vw)` × full height): file `<select>` when there are
two or more fences, a wrapping monospace textarea (`#code-canvas-edit`)
with a read-only `pre#code-canvas-hi` highlight overlay behind it,
Download, Save to project (first recorded `session.projects[]` path),
and [x]. Save POSTs `/api/canvas`. No CDN highlighter (no Highlight.js,
no Prism). An in-page scanner colors strings, comments, numbers, and
keywords for: LaTeX, R, Bash, Zsh, PowerShell, Plaintext, Markdown, C,
C++, Rust, PHP, Python, Go, Ruby, HTML, JavaScript, TypeScript, React,
Node.js. Fence aliases `go` / `golang` use the Go keyword set.
Persist `{w}` in `localStorage.hush-canvas`.

Ctrl+K (also Cmd+K) on the canvas opens `#canvas-k`: a small
instruction box over the selection. Apply POSTs `/api/fixup`
`{instruction, text}` (selection, or the whole buffer if empty) and
replaces that span with the returned `text`. Esc / Cancel closes.
`/api/fixup` runs a one-shot `grok -p` (`HUSH_AGENT_FIXUP_TURNS` =
`"1"`) and must **not** insert a kind-1 hive note.

Pause on `#code-canvas-edit` (`CANVAS_FIM_MS` = 300) POSTs
`/api/complete` `{prefix, suffix}` (text before / after the caret)
and polls `GET /api/complete?t=` until a ghost is ready. State is
`activePrediction` + `predictionPos`. While the request is in
flight, `pre#code-canvas-hi` paints a `.fim-caret` pulse at the
caret using `var(--accent)` and `var(--accent-dim)`. The returned
middle paints as `.tok-ghost` (`var(--faint)`). Tab inserts the
ghost at `predictionPos`, moves the caret, and clears state. Tab
with no prediction is left to the textarea. Escape clears. A range
selection does not start FIM. The pump must not `nanosleep` on
this path (`hush_canvas_poll` next to `hush_agent_poll`). No
in-process libcurl.

### 14. Channel groups + manage (NIP-29 parent)

A Hush channel is a NIP-29-shaped group: random 32-hex `id`, `#h` on
the wire stays the **slug** so existing notes keep matching. A Hush
**Group** is a NIP-29 parent (`groups[].id`). `channel.group_id` empty
means ungrouped. Membership does not inherit (NIP-29 subgroup rule).

Sidebar: grouped channels under the group name, then ungrouped.
Each row: name, compact red `−` (`.chan-del`, 24×24) deletes after
confirm (“Delete #slug? Notes stay on disk.”). Last channel cannot
be deleted. When `status.whisper` is true a Voice icon (`.chan-voice`)
sits on the row (§17).

Right-click a channel (`#chan-menu`):

- Add To Group — pick an existing group or “New group…”
- Remove From Group — no-op if ungrouped
- Delete Channel — same confirm as `−`
- Manage Channel — `#manage-chan` modal

==Manage Channel==

- Add/Invite/Remove Humans and Robots with the same `+` / `−` pill
  language as Raise-robot name. No checkboxes.
- Unused pool: name + `+`. Added names become `.pill` with `−`.
- Invite npub `+` commits a human pill.
- Empty lists mean the whole hive when `kind` is `open` (current
  behavior). `humans` / `robots` / `mixed` require the matching pills.
- **Policy** (`#manage-policy`) — see §20. Kind radios + reply radios
  are always visible. Burst / jobs / cooldown live in `<details>`.
- [Save] [Close]

Payne: “Leash the robots. They speak when mentioned, or not at all.”

### 15. Tool rail

`#tool-rail` is a `position:fixed` strip the human can drag by
`#rail-grip` (≥44px, `touch-action:none`). Pointer tracking is on
`window`, not the grip alone. Collapse (`#rail-toggle`) shrinks it to
a hamburger. There are **no docks**, no snap squares, and no
`#rail-docks`. The rail stays where the human drops it and is clamped
on screen. `localStorage.hush-rail` stores `{x,y,collapsed}` only.

**Micro-UX iterations (human-friendly tweaks):** Grip has visible
"drag handle" pattern + title="Drag to move". All rail-i info buttons
use consistent 22px, high-contrast. Popovers have 1-line summaries
first. "New Robot" and inventory openers are clearly labeled. Collapse
state + position restored per-vibe where possible. 44px minimum on
primary actions. Progressive disclosure for advanced (STUN host,
policy details). Small labels/hints added for first-time users without
increasing cognitive load.

**Brand home.** `placeRailAtBrand()` puts the collapsed hamburger
immediately to the left of `.brand` (`hush` / vibe name, e.g.
`LOCAL HIVE`). Double-click `#rail-toggle` collapses (if needed) and
homes there.

**Thread park.** Opening `#thread-pane` forces collapsed + brand home
so the rail never sits inside the thread chrome. Closing the pane
restores the pre-thread `{x,y,collapsed}` unless the human dragged
during the thread (then keep the new free position).

Expanded rail, in order:

1. **Install** — first, full width, current size. Position stays.
   `#rail-info` (`i`) toggles `#install-help`.
2. Divider.
3. Two-column: **Profile** (`#profile-btn`) + `#profile-info` `i` |
   **Settings** (`#settings-btn`) + `#settings-info` `i`.
4. Two-column: **Call** (`#call-btn`, when `session.ready`) +
   `#call-info` `i` | **Invite** (`#invite-human`) + `#invite-info` `i`.
5. Divider.
6. Two-column: **Add Channel** (`#add-chan`) + `#chan-info` `i` |
   **Configure Providers** (`#providers-btn`) + `#prov-info` `i`.
   `#new-chan` lives in the channel popover. `#providers-btn`
   opens `#providers-hub` (§11).
7. Divider.
8. Two-column: **New Robot** (`#raise-agent`) + `#robot-info` `i` |
   **New Project** (`#add-proj`) + `#proj-info` `i`. `#new-proj`
   lives in the project popover.
9. Divider.
10. Two-column: **Minimize** (`#rail-min`, `POST /api/window`
    `{action:"minimize"}` — WM iconify) | **Maximize** (`#rail-max`,
    `POST /api/window` `{action:"maximize"}` — toggle WM maximized).
    Not rail-collapse. Not the Fullscreen API.
11. Two-column: **Close** | **Exit** (same `#hive-leave` as today).
    Min/Max and Close/Exit are two separate `.rail-grid`s.

There is no `#blank-btn`. There is no left-nav `.create` block.

Pair buttons are normal size (32px tall, 8px radius), not 44px
pills. `#install` and `#rail-toggle` stay ≥44px.

`#install-help` copy (popover only):
“Install puts Hush on your app launcher as its own window. It does
not start a second hive.”

`#profile-help`: “Your name, npub, and logout. This is you, not a robot.”
`#settings-help`: “Theme, vibe visibility, and the local STUN/TURN server for calls.”
`#call-help-pop`: “Start a mesh conference on this channel. Agents need Whisper to hear.”
  (id is not `#call-help` — that string already labels the stage.)
`#invite-help`: “Click to invite a human to your vibe.”
`#chan-help`: “A channel is a named room in this hive. Notes you
post there stay on that channel.”
`#prov-help`: “Hive-wide credentials and OAuth for every runtime. Robots pick which one they use; secrets live once, here.”
`#robot-help`: “Raise a new robot. It gets its own name, provider,
and standing orders.”
`#proj-help`: “A project is a folder the hive can write to. Create
one to open files in the canvas.”

Header Hick: brand + badge only. Drawer Close buttons stay local.

### 16. Mention + manage pills

Same `.pill` / `.icon-plus` / `.icon-minus` language as Raise-robot.
Composer pills: `.composer-pill` inside `#composer-pills` next to
`#msg`. Manage Channel lists: `#manage-humans` and `#manage-robots`
render pills, not checkboxes.

### 17. Whisper-gated call and channel voice

`hush_turn_whisper_available()` already feeds `/api/status` and
`/api/turn` as `whisper`. Cache it from `tick()`.

When `whisper` is true:

- Each robot card shows `.robot-call` (1:1). Opens `#stage`, joins,
  signals `{t:"join", role:"agent", from: slug}`. Copy: “One-to-one
  with <name>. Mute any voice you do not want.”
- Each channel row shows `.chan-voice`. Opens `#stage` for that
  channel and invites roster robots (Payne if the roster is empty).

When `whisper` is false those icons are absent. Header/rail Call
stays the generic conference entry.

Each conference tile has `.tile-mute`. Mute is local: disable that
tile’s inbound audio. Robots mute the same way humans do. This slice
does not run Whisper STT/TTS inside the relay.

### 18. Exit reaps children

Exit / `--quit` / SIGTERM call `hush_relay_reap_children()` from
`hush_relay_cleanup`. Tracked pids (cap 8) come from
`hush_open_app_window` and `hush_provider_spawn_login` via
`hush_relay_track_child`. Linux also sweeps `/proc` for leftover
`--class=hush-relay` plus this port’s `--app=` URL.

Close and attach never reap. Attach copy:
“This is the process already listening. Exit or hush-relay --quit
before a new install can take the port.”

### 19. Relay-live details

`#stats` (sidebar “relay live / N stored / N projects / N sockets”)
is a control: `role="button"` `tabindex="0"`, Fitts ≥44px tall.
Click or Enter opens `#relay-drawer`. The panel lists version, listen
port, stored note count, project names (or “no projects”), socket
count, whisper, and turn. `#relay-close` [x] sits at the right of the
title row and dismisses the drawer. Nothing on `#stats` is a link
off-hive.

### 20. Channel policy + conversation intelligence

A robot spends tokens only when all of these hold:

1. It is on the channel roster, or `kind` is `open`.
2. `robot_reply` is not `off`.
3. A human mentioned it (`p` tag).
4. The note is not mention-only.
5. The burst window has closed, or the human confirmed.
6. `max_jobs`, `cooldown_s`, and `robot_hops` allow it.

Otherwise the robot stays silent or posts one on-deck line. Recap /
confirm notes are kind 1, `e` = root, `t` = `hush-confirm`. Intel
ignores those tags so a confirm does not start another hold.

| Field | Values | Default |
|---|---|---|
| `kind` | `open` `humans` `robots` `mixed` | `open` |
| `robot_reply` | `off` `mention` `confirm` | `mention` |
| `robot_talk` | `0` `1` | `0` |
| `burst_ms` | `500` `2000` `5000` | `2000` |
| `max_jobs` | `1` `2` `4` | `2` |
| `cooldown_s` | `0` `10` `30` | `10` |
| `robot_hops` | `0` `1` | `0` |

`#manage-policy` radios (Fitts ≥44px):

- Kind: Open hive / Humans / Robots / Mixed.
- Robots may reply: Off / When mentioned / Confirm first.
- `<details id="manage-policy-more">` Advanced: robots may talk to
  robots (checkbox; visible for `robots` / `mixed`), burst wait
  (0.5s / 2s / 5s), max live jobs (1 / 2 / 4), cooldown (off / 10s /
  30s).

Chatty path: same human, same `(channel, root, robot)`, notes inside
`burst_ms` fold (cap 8). One clear ask starts the job. Two or more
notes post a recap and wait for `yes` / `y` / `confirm` / `go` /
`do it` / `1`… or a correction. `robot_reply=confirm` always recaps
first. Duplicate content <1s is dropped. In-flight Grok is not killed
when policy flips; new considers honor the new leash.

## Visual language
- Dark default tokens stay. Themes override CSS variables on `html[data-theme]`.
- Color-blind: blue / orange, never green / red as the sole pair.
- Feather 36–72px on splash, no animation beyond a quiet fade if cheap.
- Author pills: you / Payne / agent / human.

## Hick cuts
- Header primary choices: brand + badge. Actions live on the tool rail.
  Install is opportunistic and explained. Badge is status.
- Onboard: one primary button.
- Themes only in Settings.
- Agent + human add are drawers, not splash steps.

## Caps (named, one site each)
- HTTP recv `HUSH_BUF_SZ = 65536` (was 8192; HTTP JSON + small avatar).
- Session JSON `HUSH_LAUNCH_JSON_MAX = 32768`.
- Groups: 8. Humans per channel: 8. Robots per channel: 8.
- Channel policy: burst notes 8 (`HUSH_INTEL_BURST_MAX`); holds 8
  (`HUSH_INTEL_HOLD_MAX`); confirm tag `hush-confirm`.
- Channel / group id: 32 hex (`HUSH_LAUNCH_ID_HEX`).
- Context: 3 files × 4096 bytes.
- Provider models: 32 names × 64 bytes (`HUSH_PROVIDER_MODELS_MAX`).
- Provider overlay: `$HOME/.hush/config/providers.json` (0600).
- Vibe overlay: `$HOME/.hush/config/vibe.json` (0600). Override
  directory with `HUSH_CONFIG_DIR` / `HUSH_HOME` (tests). Never nsec.
- Skills: `$HOME/.hush/skills/{system,user,robots}/`. Product buckets are
  System (application-wide: shipped `system/` + hive-forged `user/`) and
  This robot (`robots/<slug>/`). Catalog JSON `"scopes":["system","robot"]`.
- Avatar on disk: sniffed JPEG/PNG only; client downscales ≤96px.
- Kind 0 `picture` is a URL, never a data URI (`HUSH_EVENT_MAX_CONTENT = 4096`).

## Non-negotiables
- pass checkbox default checked + retrieve CLI.
- Profile/Settings visible before ready.
- Payne always first in the robot card list when vibe present.
- A robot cannot be raised without a system prompt and an AI provider.
- Provider secrets live only in `pass` (`hush/providers/<id>/{api_key,username,password,token,passkey}`).
- Hush never writes `~/.config/goose`, `~/.grok`, or `~/.codex`.
- Close and Exit are distinct verbs. Rail buttons open `#hive-leave`
  (Exit the application / Close the window / Cancel). The OS `×` is
  the `--app` window's; last `--app` child gone raises the zenity follow-up.
- Close never kills the relay. Exit always does, with exit code 0.
- No catfu / Griffe / Scout / Brave / feline words.
- Embed after every HTML change:
  `./scripts/embed-ui.sh hush-c/demo` from the worktree root.
- write-legible-c §14 on every C commit.
