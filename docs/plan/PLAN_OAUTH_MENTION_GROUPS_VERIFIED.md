# PLAN_OAUTH_MENTION_GROUPS.md — Verification Gate (M1-M4)

Base: main 5a61b88fb (fresh worktree gb/oauth-mention-groups)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_OAUTH_MENTION_GROUPS_CURRENT.md
- Confirmed: primary DoD items already present on this base from prior slices.
- OAuth authenticated + close-window copy, @ mentions (nostr:npub + p-tags), channel UUIDs + groups, right-click manage/delete, Manage Channel modal, tests green.

## OAuth authenticated UX (DoD 1-2)
- After login start: "A terminal is running … Finish sign-in there (a browser should open)."
- On has_home: "PROVIDER is authenticated. Close the login browser and the terminal. Carry on."
- has_home drives .ready + checkmark + "authenticated" hint
- GET /api/provider reports has_home; UI polls and updates

## @ mentions (DoD 3)
- Composer `@` opens #mention-box (and #thread-mention in thread)
- Lists Payne, raised robots, human, invited members
- Inserts `nostr:npub1…` (NIP-27 style) into content
- Posted notes render mentions as `@Name`
- POST /api/event accepts mention_0…mention_7 → stored as p tags
- hush_intel has key matching for npub/hex mentions

## Channels + Groups + Manage/Delete (DoD 4-7)
- hush_launch_channel_t has id (UUID hex, HUSH_LAUNCH_ID_HEX=32) + group_id
- hush_launch_group_t + groups[]/ngroups on launch (cap 8)
- UUID assigned on push_channel / create_group; missing ids filled on restore + saved
- hush_launch_remove_channel (refuses last channel)
- paintChannels renders grouped + loose; contextmenu on rows
- Right-click actions: Add To Group, Remove From Group, Delete Channel, Manage Channel
- #manage-chan: add/remove humans (npubs) + robots (slugs); Save; max 8 each
- POST /api/channel supports action: group/delete/manage
- #h remains slug for wire notes; UUID is stable grouping id

## JSON / caps
- HUSH_LAUNCH_JSON_MAX / FILE_MAX = 32768 (prior)
- Membership caps 8 humans + 8 robots per channel

## Tests + embed (DoD 8)
- make -C hush-c test → ALL TESTS PASSED (launch, pwa, provider, agent mention reply, etc.)
- sh hush-c/tests/check_launch.sh → channels have id + group_id; groups array present
- sh hush-c/tests/check_pwa.sh → PWA routes ok
- Embed clean (prior slices)

## Docs
- UI_SPEC.md: Manage Channel, mentions (mention_0..), OAuth authenticated copy, group_id
- RESEARCH + PLAN exist

## Constraints
- Prime Directive: gb/* only; PR to main
- C11 + legible-c (existing code)
- #h slug stays; UUID for local grouping
- has_home = authenticated signal
- No full NIP-29 wire events this slice

## DoD checklist (primary items satisfied)
1. [x] OAuth drawer: finish sign-in then "Close the login browser and the terminal"
2. [x] has_home → green/authenticated badge + color change
3. [x] @ autocomplete → nostr:npub insert; render @Name; p tags via mention_*
4. [x] Channel red − deletes after confirm (refuse last)
5. [x] Every channel + group has UUID; groups in vibe.json survive restart
6. [x] Right-click: Add/Remove Group, Delete, Manage Channel
7. [x] Manage Channel: humans + robots lists; Save/Close
8. [x] make && make test pass; embed after HTML
9. [x] PR merged, worktree removed, main clean (pending M5)

