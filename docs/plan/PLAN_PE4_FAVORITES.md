# PLAN: PE-4 favorite loadouts (Journey D)

**Branch:** `gb/pe4-favorites` (worktree `worktrees/pe4-favorites`; cloud VM only)
**Base:** `44c66f88` (PE-3 min-1 tip)
**Land:** DRAFT PR only. No merge. Never push `main`.

## Contracts (from Journey D + UI_SPEC PE-4 delta)

- `#agent-drawer` Favorites strip: Save snapshots the doll (≥1 skill)
  under a typed name; empty names and 0-skill dolls are refused inline.
- Load replaces the doll in one assignment (never through empty) and
  refuses a favorite with zero skills still on this relay; missing ids
  are skipped with inline copy when ≥1 valid skill remains.
- Unload clears the active-favorite highlight only — never the doll.
- Delete removes the list entry only — never the doll.
- Persistence is relay-side: per-robot named JSON sets
  (`{"name":..,"skills":[..]}`, 1–8 skill ids each) under
  `$HOME/.hush/robots/<slug>/loadouts/`, served by `POST /api/loadout`
  (`save` / `list` / `load` / `delete`). Unknown, `robot:<other>:` or
  repeated ids are refused at save, as are slug clashes with a different
  stored name (exact-name overwrite allowed), a 33rd favorite, a
  `skill_8` overflow, overlong values, and non-UTF-8 names. Robot slugs
  and names are allowlist-validated at every entry; load/delete/list
  create no directories. Favorites survive leave→return; the highlight
  does not. New robots (no slug yet) cannot save favorites.
- PE-3 min-1 holds everywhere: last-gem lift, empty-draft save, empty
  loadout write, and empty favorite all refuse with
  “Keep at least one skill equipped.” (or the favorite-specific copy).
- Honesty preserved: lifetime labels stay browser-only (relay has no
  lifetime store); favorites are labeled saved-on-this-relay, the only
  new relay-saved skill state.

## Milestones (atomic, commit per M on the worktree branch)

- M1 — Relay persistence: `hush_home_loadouts_dir` path builder,
  `hush_favorite` file module (save/list/load/delete, atomic tmp+rename),
  `POST /api/loadout` route, `tests/test_favorite.c` unit proof.
- M2 — Drawer UI: Favorites strip (name input, Save/Unload, Load/Delete
  per entry, active highlight), atomic load, unload/delete semantics,
  min-1 refusals, manual doll edits clear the highlight.
- M3 — Spec + proof: UI_SPEC PE-4 delta; `scripts/checks/pe4-favorites.sh`
  static proof; `pe3-min1.sh` section 6 updated to permit PE-4 plumbing
  while still proving min-1; `armory-honesty.sh` untouched and green.
- M4 — Build + run: `make`, unit bins, `check_launch.sh`,
  `armory-honesty.sh`, `pe3-min1.sh`, `pe4-favorites.sh`,
  `check_collaboration.py`, live `/api/loadout` round-trip. DRAFT PR
  with OBSERVED-only body.

## Out of scope

- Prompt-tier injection changes (`agent_prompt.c` untouched).
- Character (`c`) favorite picker (drawer strip only in v1).
- Lifetime persistence (stays `localStorage.hush-skill-lifetime`).
- Roster-layer favorite enforcement (HTTP + file module only).
