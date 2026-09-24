# PLAN: PE-3 min-1 block (minimum one equipped skill)

**Branch:** `cursor/pe3-min1-equipped-1ebf` (cloud VM; single checkout, no kiff/athena)
**Base:** `c51bf9ec` (main tip after Honesty Armory #203)
**Land:** DRAFT PR only. No merge. PE-4 (favorites) is a separate assign.

## Contracts (from UI_SPEC PE-1 delta + #203 honesty polish)

- Every enabled robot keeps ≥1 equipped skill; any unequip / Clear / unload
  that would leave 0 is refused with “Keep at least one skill equipped.”
- Relay has no persistence for browser-only claims: lifetime labels stay
  browser-only, doll/sheet stay draft-until-Save, Character strip reports
  relay-saved counts. PE-3 adds no saved-state claims.

## Milestones (atomic, commit per M on the branch)

- M1 — Relay gate: `hush_http_check_loadout` refuses `has_skills && nskills==0`
  with `HUSH_ERR_DENIED` (→ 400, worn loadout kept). Untouched loadouts pass
  through; pre-existing skill-less robots keep state until edited.
- M2 — Client guards: `pruneSkillAt` + `pickUpSkill` (doll lift) + `agent-save`
  pre-check refuse empty with the contract copy; watermark line carries the law.
- M3 — Tests: `check_launch.sh` refusal expectations replace empty-OK ones;
  `check_collaboration.py` removal step becomes refusal + swap to a second
  forged skill; static client-copy gate added.
- M4 — Spec + proof: UI_SPEC PE-3 delta; `scripts/checks/pe3-min1.sh` static
  proof (gate: refusal sites, copy count, no PE-4 leakage, no phantom claims).
- M5 — Build + run: `make`, unit bins, `check_launch.sh`, `armory-honesty.sh`,
  `pe3-min1.sh`, `check_collaboration.py`. DRAFT PR with OBSERVED-only body.

## Out of scope

- PE-4 favorites save/load (`loadouts/`, favorite picker): no plumbing.
- Prompt-tier injection changes (`agent_prompt.c` untouched).
- Roster-layer enforcement (stays permissive; HTTP is the single gate).
