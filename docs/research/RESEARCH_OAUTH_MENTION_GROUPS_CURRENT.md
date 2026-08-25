# RESEARCH — OAuth UX + @mentions + Channel Groups (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/oauth-mention-groups`
Branch: `gb/oauth-mention-groups`
Base: `main` `5a61b88fb` (post #68 provider-pass-audit)

## Base State (post prior slices)

All major DoD items from PLAN_OAUTH_MENTION_GROUPS.md are already present on this base:

**OAuth authenticated UX:**
- Provider drawer shows: "PROVIDER is authenticated. Close the login browser and the terminal. Carry on." when `has_home`
- `has_home` drives `.ready` / checkmark / authenticated state
- `GET /api/provider` reports `has_home` for OAuth providers after CLI login
- Poll + status handling present

**Mentions (@):**
- `#mention-box` and `#thread-mention` exist
- Composer handles `@` autocomplete over Payne, raised robots, human, members
- Inserts `nostr:npub1…` (NIP-27 style) into content
- Notes render mentions as `@Name`
- `POST /api/event` supports `mention_0`…`mention_7` → `p` tags
- `hush_intel` has mention key matching for npub/hex

**Channels + Groups:**
- `hush_launch_channel_t` has `id` (UUID hex) + `group_id`
- `hush_launch_group_t` + `groups[]` / `ngroups` on launch
- UUID assigned on create/restore; missing ids filled on load then saved
- `hush_launch_remove_channel` exists (refuses last channel)
- `paintChannels` renders groups + loose channels
- Right-click contextmenu on channel rows: Add/Remove from Group, Delete, Manage
- `#manage-chan` drawer: add/remove humans (npubs) + robots (slugs), max 8 each
- `POST /api/channel` + actions for group/delete/manage
- `#h` remains slug for wire compatibility; UUID is stable id for grouping

**JSON / caps:**
- HUSH_LAUNCH_JSON_MAX / FILE_MAX bumped to 32768 in prior
- Channel/group membership caps enforced (8+8)

**Tests:**
- make -C hush-c test → ALL TESTS PASSED (launch, pwa, provider, agent mention reply, etc.)
- check_launch.sh shows channels with `id` and `group_id`, groups array present
- check_pwa.sh passes

**Docs:**
- UI_SPEC.md references Manage Channel, mentions, OAuth authenticated copy, group_id
- RESEARCH_OAUTH_MENTION_GROUPS.md + PLAN exist from prior

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → channels have id + group_id; groups present
- sh hush-c/tests/check_pwa.sh → PWA routes ok
- Explicit greps in compiled header + source:
  - "authenticated. Close the login browser..."
  - mention-box, nostr:npub handling
  - manage-chan, contextmenu, group_id, groups[]
  - channel id + group_id
- hush_launch.c: remove_channel, has_group_id, UUID generation/restore
- hush_http.c: channel actions (group, delete, manage), mention fields
- No new C required for verification gate

## Differences from original PLAN base

- Current base 5a61b88fb is significantly later. OAuth mention groups (channel UUIDs, groups, manage, delete, @ mentions, authenticated OAuth copy) were implemented across earlier slices (robot-cards, pills-rail-voice-exit, thread-ux, rail-prov, provider work, etc.) and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching prior pattern: rail-prov #65, canvas-fim #66, provider-configure #67, provider-pass-audit #68) to close PLAN_OAUTH_MENTION_GROUPS.md per user directive.

## Conclusion

Implementation on this base satisfies the primary DoD items (OAuth close-window/authenticated, @ mentions with nostr:npub + p-tags, channel UUIDs + groups, right-click manage/delete, Manage Channel modal, tests + embed).
No code changes required for this verification slice.
H4 lock (local vibe state, slug on #h, UUID for grouping, has_home for auth, NIP-27 style mentions, no full NIP-29 wire events this slice) holds.

## Commands executed
- git worktree add -b gb/oauth-mention-groups from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh + check_pwa.sh
- rg/grep for authenticated copy, mention-box, nostr:npub, manage-chan, contextmenu, group_id, channel id, groups
- Source inspection of hush_launch (id/group/remove/UUID), hush_http (actions), html (drawers/JS)

## Next
- Write PLAN_OAUTH_MENTION_GROUPS_VERIFIED.md
- Commit M1.1 research gate + verification on gb/*
- Push + gh pr create + auto-merge
- Post-merge: pull main, remove worktree, delete branch local+remote
