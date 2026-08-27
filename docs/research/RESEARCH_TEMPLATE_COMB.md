# RESEARCH: Fine-comb of templates, hardwired loadouts, and the system skill catalog

**Date:** 2026-08-27  
**Worktree:** `worktrees/template-comb` / `gb/template-comb`  
**Scored artifacts:** `hush_launch_seed_templates`, Payne defaults, every `skills/system/*/SKILL.md`.

## Category definitions (one sentence each)

| Id | Category | What 10 means |
|---|---|---|
| C1 | Distinctness | Each template and skill owns one job; near-duplicates are folded. |
| C2 | Hush-fit | Hive/armory/equip language; no leftover Claude-plugin, npm CLI, or jadx runtime. |
| C3 | Loadout legality | Hardwired ids exist in the catalog, pass the role wall, and fit slot/char/complexity watermarks. |
| C4 | Identity | Major, Coach, Auditor, and Marshal each have a unique name and a `panel:` icon. |
| C5 | Coverage | Important hive jobs are represented; Marshal’s hardwired rails cover yield, topic, loops, turns, secrets, and ack. |
| C6 | Actionability | A robot can follow the SKILL.md as standing orders without inventing the procedure. |

Scale: 0 = worst, 10 = perfect. Iterate any category **below 8** (7.8–7.9 acceptable).

## Templates (shipped, pre-iteration)

| Name | Role | Icon | Hardwired skills | Notes |
|---|---|---|---|---|
| Major | worker (dual-role chaperon on a channel) | panel:robots:1 | none | Name/prompt locked; not cloneable |
| Coach | worker | panel:robots:0 | canvas-coach | Locked; clone to edit |
| Auditor | worker | panel:robots:2 | hive-audit | Locked; clone to edit |
| Marshal | chaperon | panel:angevin:3 | topic-leash, no-loop, civility, hop-cap, secret-watch, chaperon-ack | Locked; 6 of 8 slots |

## Catalog comb (pre-iteration)

**Keep (distinct hive knobs):** hop-cap, token-budget, job-cap, cool-down, rate-limit, channel-kind, night-watch, civility, claim-check, conflict-break, secret-watch vs pii-redact, human-cue, silence-nudge, summary-handoff, chaperon-ack, guardrail-log, no-loop.

**Redundant:** `bring-back` is the action `topic-leash` already orders (“post one short bring-back line”). Fold into topic-leash.

**Forgotten on Marshal (2 free slots):** `human-cue` (yield when a human speaks) and `token-budget` (honor max_robot_turns). Channel rails already implement these; the hardwired loadout does not.

**Forgotten on Major:** `hive-patterns` is documented as Major knowledge but Payne seeds with an empty loadout.

**Thin / low-action worker chips:** `hive-apps` is a slogan; `hive-seo` and `hive-voice` are generic SaaS leftovers with one sentence of hive mapping; `hive-review` overlaps canvas-coach’s “demand tests”.

**forge-skill** C seed has no `role:` frontmatter (defaults to any). File on disk also omits role.

**Keep 20 chaperon:** folding `bring-back` drops to 19. Replace with `intro-once` (one intro per robot+thread) — a real gap from channel-rail history, not a duplicate.

No extra locked army. Do not delete Marshal.

## Pre-iteration panel

| Category | Score | Why below 8 (if so) |
|---|---|---|
| C1 Distinctness | 7.2 | bring-back ⊆ topic-leash; hive-review ≈ canvas-coach tests; hive-apps is not a job |
| C2 Hush-fit | 8.4 | Bodies grep Hush-adapted; named sources cited; no jadx/npm dump |
| C3 Loadout legality | 8.5 | Six Marshal skills are chaperon and under watermarks; Coach/Auditor worker ids exist |
| C4 Identity | 8.6 | Names + panel icons already seeded |
| C5 Coverage | 7.3 | Marshal missing yield + turn cap; Major missing hive-patterns; no intro-once chip |
| C6 Actionability | 6.8 | Most chaperon bodies are one line; hive-apps/seo/voice/review too thin to follow |

**Must iterate: C1, C5, C6.**

## Planned remaps

1. Fold `bring-back` → `topic-leash`. Add `intro-once` (chaperon) so count stays ≥20.
2. Marshal hardwired (8/8): topic-leash, no-loop, civility, hop-cap, secret-watch, chaperon-ack, **human-cue**, **token-budget**.
3. Default Major loadout: `system:hive-patterns` when empty (still user-prunable; name/prompt stay locked).
4. Rewrite thin worker bodies (hive-apps, hive-seo, hive-voice, hive-review, canvas-coach vs review split). Expand chaperon standing orders one notch without blowing watermarks.
5. `forge-skill` frontmatter `role: any` in C seed and pack file.

## Post-iteration panel

| Category | Pre | Post | What changed |
|---|---|---|---|
| C1 Distinctness | 7.2 | 8.3 | Folded bring-back into topic-leash; intro-once is a different job (one intro/thread); hive-review is diff evidence, canvas-coach is session process |
| C2 Hush-fit | 8.4 | 8.5 | Bodies still Hush-adapted; forge-skill `role: any` |
| C3 Loadout legality | 8.5 | 8.6 | Marshal 8 chaperon ids `try_equip` as chaperon under watermarks |
| C4 Identity | 8.6 | 8.6 | Names and panel icons unchanged |
| C5 Coverage | 7.3 | 8.5 | Marshal adds human-cue + token-budget; Major defaults hive-patterns; intro-once fills the forgotten intro gap |
| C6 Actionability | 6.8 | 8.2 | Chaperon and thin worker bodies now have followable standing orders |

All post scores ≥8.
