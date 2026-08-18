# PLAN: OAuth UX + @mentions + Channel Groups

Frozen after Phase 1 synthesis (2026-08-18). Execute only this file.
Worktree: `/opt/repo/hush/worktrees/oauth-mention-groups`
Branch: `gb/oauth-mention-groups`

## Primary Goal

After provider OAuth the human can see they are authenticated and is
told to close the extra windows. `@` mentions humans and robots.
Channels have UUIDs, can live in a Group, and can be deleted or
managed from a right-click menu.

## Non-Goals

- Live LLM replies from mentioned robots.
- Emitting NIP-29 39000/39001/39002 on the wire.
- NIP-28 kinds 40–44.
- Nested groups, drag reorder, remote auth.

## Success Criteria / DoD

See RESEARCH_OAUTH_MENTION_GROUPS.md. Tests + embed + PR + cleanup.

## Constraints

C11 + write-legible-c. Worktree/PR law. JSON is string-field only.
`#h` stays the channel slug. Authenticated OAuth = `has_home`.

## Assumptions

One vibe, one hive. Groups are local parent folders of channels.
Manage Channel membership is local vibe state.

## Environment

gcc, make, `./configure`, `make test`, curl, `./scripts/embed-ui.sh`.

## Top Risks

See research. Caps: 8 groups, 8 humans/channel, 8 robots/channel,
JSON 32768.

---

## Phase 0 — Isolation (done)

- [x] Task 1 of M0.1: clean main at e1766398a, worktree
      `worktrees/oauth-mention-groups` on `gb/oauth-mention-groups`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/oauth-mention-groups`.

## Phase 1 — Research (this commit)

### M1.1 Inspect current OAuth, composer, channels

- [x] Task 1 of M1.1: read UI_SPEC, index.html OAuth + paintChannels
      + composer + render.
- [x] Task 2 of M1.1: read hush_launch / hush_http / hush_roster /
      hush_provider / hush_event.
- Verify: no contextmenu, no channel delete, no `@`, OAuth copy
  lacks close-window after success.

### M1.2 NIPs

- [x] Task 1 of M1.2: fetch NIP-29, NIP-27, NIP-51, NIP-C7, README.
- [x] Task 2 of M1.2: map groups = NIP-29 parent/child; mentions =
      NIP-27; 10009 is a client bookmark not a folder.
- Verify: findings recorded.

### M1.3 Synthesize (mandatory gate)

- [x] Task 1 of M1.3: write RESEARCH_OAUTH_MENTION_GROUPS.md.
- [x] Task 2 of M1.3: write this plan and commit.
- Verify: both files exist on `gb/oauth-mention-groups`.

## Phase 2 — Architecture (docs + UI_SPEC)

### M2.1 Lock contracts

- [x] Task 1 of M2.1: add UI_SPEC §12 OAuth signed-in, §13 mentions,
      §14 channel groups / manage / delete.
- [x] Task 2 of M2.1: add API rows to UI_SPEC data table.
- Verify: `rg -n "Manage Channel|mention_0|group_id" UI_SPEC.md`.
- Commit: `Milestone 2.1: lock OAuth mention group contracts`

## Phase 3 — Channel + group core

### M3.1 Data model

- [ ] Task 1 of M3.1: extend `hush_launch_channel_t` with `id`,
      `group_id`, human npubs, robot slugs + counts. Add
      `hush_launch_group_t` and `groups[]` / `ngroups` on launch.
      Bump JSON max to 32768. `HUSH_LAUNCH_ID_HEX = 32`.
- [ ] Task 2 of M3.1: `hush_launch_make_uuid` (16 random bytes → 32
      hex). Assign on push_channel and create_group. Restore fills
      missing ids then save.
- Verify: `test_launch` still builds after header change (next tasks
  implement the functions).

### M3.2 Mutators

- [ ] Task 1 of M3.2: `hush_launch_remove_channel` (refuse last).
- [ ] Task 2 of M3.2: `hush_launch_add_group`,
      `hush_launch_set_channel_group` (empty group_id = ungroup).
- [ ] Task 3 of M3.2: `hush_launch_set_channel_roster` (humans +
      robots by slug/npub; validate against roster).
- Verify: unit tests in `test_launch.c`.
- Commit: `Milestone 3.2: channel UUID group roster mutators`

### M3.3 Persist + session JSON

- [ ] Task 1 of M3.3: put/take channel id, group_id, humans, robots;
      put/take groups.
- [ ] Task 2 of M3.3: format session channels with id + group_id +
      members; add `"groups":[...]`.
- Verify: restore test asserts UUID + group survive vibe.json.
- Commit: `Milestone 3.3: persist channel groups`

## Phase 4 — HTTP

### M4.1 Channel routes

- [ ] Task 1 of M4.1: `POST /api/channel` accepts `action` =
      `create` (default) | `delete` | `group` | `ungroup` | `manage`.
      Fields: `name`, `slug`, `group`, `group_id`, `human_0`…,
      `robot_0`….
- [ ] Task 2 of M4.1: `POST /api/group {name}` creates a group.
- [ ] Task 3 of M4.1: `POST /api/event` accepts `mention_0`…`mention_7`
      as extra `p` tags. Events JSON includes content unchanged.
- Verify: extend `check_launch.sh`.
- Commit: `Milestone 4.1: channel group mention HTTP`

## Phase 5 — UI

### M5.1 OAuth signed-in

- [ ] Task 1 of M5.1: CSS `.providers label.ready` + checkmark.
- [ ] Task 2 of M5.1: after OAuth click, poll `/api/provider` until
      `has_home` or timeout; copy: close the browser and the
      terminal. `paintProviderPencil` marks ready radios.
- Verify: `rg` in demo HTML for close-window copy + `.ready`.

### M5.2 Mentions

- [ ] Task 1 of M5.2: `#mention-box` under composer. `@` after
      start/whitespace opens roster. Enter/click inserts
      `nostr:<npub>` and records mention pubkeys.
- [ ] Task 2 of M5.2: `render` replaces `nostr:npub1…` with `@Name`.
      `who()` maps raised-robot pubkeys.
- Verify: HTML contains `mention-box` and `nostr:`.

### M5.3 Channels, groups, manage

- [ ] Task 1 of M5.3: `paintChannels` groups under labeled folders;
      red `−` per row; context menu with four items.
- [ ] Task 2 of M5.3: `#manage-chan` modal: humans + robots
      checklists, Save, Close. Prompt for new group name on Add To
      Group when none exist / “New group…”.
- [ ] Task 3 of M5.3: embed UI.
- Verify: `check_launch.sh` greps for manage-chan, mention-box,
  chan-del, contextmenu.
- Commit: `Milestone 5.3: OAuth mention group UI`

## Phase 6 — Verify + land

### M6.1 Tests and docs

- [ ] Task 1 of M6.1: `./configure && make && make test`.
- [ ] Task 2 of M6.1: README / NOSTR one-liners if needed.
- [ ] Task 3 of M6.1: PR → auto-merge → delete worktree.
- Verify: main clean, “Grok Build complete.”
