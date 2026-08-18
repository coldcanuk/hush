# 2026-08-18 RDAP: OAuth UX + @mentions + Channel Groups

Worktree: `/opt/repo/hush/worktrees/oauth-mention-groups`
Branch: `gb/oauth-mention-groups`
Base: `e1766398a` (PR #30, Grok OAuth hold)

## Scope locked

Primary goal: after a successful provider login the human can see that
Grok Build (or Codex) is authenticated and is told to close the extra
windows; `@` mentions humans and raised robots in the composer; channels
can be deleted, grouped, and managed (humans + robots) from a
right-click menu.

Non-goals:

- Spawning Goose / Grok / Codex as a live reply process.
- Emitting full NIP-29 relay-signed `kind:39000` / `39001` / `39002`
  events on the wire (local vibe.json is source of truth this slice).
- NIP-28 public chat (kinds 40–44). Deprecated; Hush already points at
  NIP-29.
- Nested groups deeper than one parent.
- Drag-and-drop channel reorder.
- Authenticated remote control. Localhost only, as today.

Success / DoD (measurable):

1. After OAuth starts, the provider drawer tells the human to finish
   sign-in in the terminal/browser, then close those windows.
2. When `GET /api/provider` reports `has_home` for an OAuth provider,
   that radio shows a green check (blue/orange on color-blind) and a
   short “authenticated” line. The provider box color changes.
3. Typing `@` in the composer lists Payne, raised robots, the human,
   and invited humans. Choosing one inserts a NIP-27 `nostr:npub1…`
   mention. Posted notes render as `@Name`.
4. Each channel row has a red `−` that deletes after confirm.
5. Every channel and every group has a UUID. Groups persist in
   `vibe.json` and survive restart.
6. Right-click a channel: Add To Group, Remove From Group, Delete
   Channel, Manage Channel.
7. Manage Channel modal: add/invite/remove humans and robots;
   Save and Close.
8. `./configure && make && make test` pass. Embed after HTML change.
9. PR merged, worktree removed, main clean.

## Current behavior (code)

### OAuth

- `POST /api/provider/login` starts `grok login --oauth` or
  `codex login` in a held xterm (`hush_provider_start_login`).
- UI copy after click: “A terminal is running … Finish sign-in there
  (a browser should open). Leave that window open until it says you
  are signed in.”
- Missing: close-window instruction after success; no poll for
  `has_home`; provider radios stay the same color; no checkmark.
- `paintProviderPencil` only marks the selected radio `.picked`.
- Status line lists “home config found” / “configured” as prose, not
  a signed-in badge.
- Click immediately `POST`s `{use_home:true}`. `configured` can become
  true before the CLI writes `~/.grok/auth.json`. UI must treat
  **`has_home`** as the authenticated signal, not `use_home`.

### Mentions

- Composer is a bare `<input id="msg">`. No `@` handler.
- `POST /api/event` stores `kind` (default 1) + one `h` tag = channel
  slug. No `p` tags.
- `GET /api/events` returns `id,pubkey,kind,created_at,content,channel`.
  No tags beyond channel.
- Agents already have `npub` in session JSON
  (`hush_roster_format_agents`). Members have `npub`. Payne has `npub`.
- `who()` maps the human pubkey and a Payne npub prefix. Raised robots
  render as `pk.slice(0, 8)`.
- There is no agent runtime. Mentioning a robot addresses it; it does
  not yet reply.

### Channels

- `hush_launch_channel_t` is `{name, slug}` only. Cap 16.
- Create: `POST /api/channel {name}`. No delete. No update.
- Sidebar `paintChannels` rebuilds buttons. No `−`. No contextmenu.
- `#h` on stored notes is the **slug** (`welcome`, `general`, …).
- vibe.json: `channel_name_i`, `channel_slug_i`. No UUID. No group.

## NIP findings (https://github.com/nostr-protocol/nips)

| NIP | Role |
|---|---|
| **NIP-29** | Relay-based groups. Group id = random string. User events MUST carry `h` = group id. Create 9007, delete 9008, put-user 9000, remove-user 9001, edit-metadata 9002. Relay-signed 39000 metadata, 39001 admins, 39002 members. **Subgroups:** a group is a parent when its 39000 has `child` tags; a child has `parent`. Membership does not inherit. |
| **NIP-51** kind 10009 | Client-side list of NIP-29 groups the user wants to remember (`group` tag = id + relay + optional name). |
| **NIP-27** | `@` autocomplete → `nostr:nprofile1…` or `nostr:npub1…` in `.content`, optional `p` tag to notify. Deprecated NIP-08. |
| **NIP-C7** | Chat messages are kind 9. Hush UI still posts kind 1; keep kind 1 this slice (store + UI already keyed that way). |
| **NIP-28** | Public channels 40–44. Unrecommended. Do not implement. |

Hush `NOSTR.md` already claims NIP-29 as the native model. Today’s C
core stores slugs, not UUIDs, and does not emit 39000.

## Architecture decisions (locked)

1. **OAuth authenticated = `has_home`.** Poll `GET /api/provider` every
   2s after login until `has_home` or 90s. Drawer then says the
   provider is authenticated and to close the login browser and the
   held terminal. Radio gets `.ready` + checkmark. Color-blind theme
   uses `--accent` (blue) not green/red as the sole pair.

2. **Mentions are NIP-27 in content.** Autocomplete over Payne, raised
   robots, the signed-in human, and invited members. Insert
   `nostr:<npub>` in the posted content. Render back to `@Name`.
   Optional `mention_0`…`mention_7` string fields on `POST /api/event`
   become `p` tags (JSON is string-field only). No live LLM reply.

3. **A Hush channel is a NIP-29 group.** Each channel gets a UUID
   (`id`). Existing channels receive a UUID on restore if missing, then
   vibe.json is saved. **`#h` stays the slug** so already-stored notes
   keep matching. The UUID is the stable NIP-29-shaped identifier for
   grouping and manage. Full 39000 emission is a follow-up.

4. **A Hush “Group” is a NIP-29 parent.** `hush_launch_group_t {name,
   id}`. Cap 8. Channel stores `group_id` (empty = ungrouped). Add To
   Group sets `group_id`; Remove From Group clears it. No membership
   inheritance (NIP-29 subgroup rule).

5. **Manage Channel** stores per-channel human npubs (max 8) and robot
   slugs (max 8). Empty lists mean “whole hive” (current open
   behavior). Non-empty lists are the roster for that channel.

6. **Delete** removes the channel from the table and vibe.json. Refuse
   if it is the last channel. Confirm in the UI. Notes already stored
   under that slug stay in the store (no mass delete).

7. **JSON caps.** `HUSH_LAUNCH_JSON_MAX` and `HUSH_LAUNCH_FILE_MAX`
   rise from 16384 to 32768 so UUID + group + membership fit.

## Risks

1. vibe.json overflow → bump file/session caps; keep membership caps
   at 8+8; tests write a full hive and restore.
2. `#h` slug vs UUID mismatch with third-party NIP-29 clients → keep
   slug on the wire this slice; document the follow-up.
3. OAuth `use_home` pretends configured too early → UI keys off
   `has_home` only.
4. `@` in the middle of a word → trigger only after whitespace or
   start, standard mention caret.
5. write-legible-c on a 1487-line `hush_launch.c` → small named
   helpers, no drive-by rewrite.

## Verification performed (this research)

- Read `UI_SPEC.md`, `NOSTR.md`, `hush_launch.h/.c`, `hush_http.c`,
  `hush_roster.h/.c`, `hush_provider.h`, `hush_event.h`,
  `demo/index.html` (OAuth drawer, `paintChannels`, composer, `render`).
- Fetched NIP-29, NIP-27, NIP-51, NIP-C7, NIP README from
  github.com/nostr-protocol/nips (2026-08-18).
- Confirmed no `contextmenu`, no channel delete, no group id, no `@`
  handler, no provider `.ready` class.
