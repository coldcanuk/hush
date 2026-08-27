# RESEARCH: Diablo-style skill equip, watermarks, SkillUI-in-C, Hush-adapted packs

**Date:** 2026-08-26  
**Worktree:** `worktrees/skill-equip` / `gb/skill-equip`  
**Sources:** existing `hush_skill` / armory UI; Anthropic Agent Skills docs; SkillUI (`amaancoderx/npxskillui`); named GitHub packs.

## Existing hive

Three scopes already exist: `system` / `user` / `robot` under `~/.hush/skills`. Equip is 8 slots (`HUSH_SKILL_EQUIP_MAX`) via `skill_N` on `POST /api/agent`. Armory paints chips by scope; click equips, click loadout prunes. No role wall, no scores, no clone, catalog cap 64.

## Watermarks (evidence)

Anthropic progressive disclosure (engineering post + skills best practices):

| Layer | When loaded | Cost |
|---|---|---|
| Metadata (name+description) | always | ~50–100 tokens/skill |
| SKILL.md body | on trigger | keep **under 500 lines** / **~5k tokens** |
| references/scripts | on demand | unbounded until read |

Hush grok jobs currently inject standing orders, not progressive skill files. Equipped bodies therefore **must** fit a hard loadout budget, not Anthropic's 500-skill session cap.

Chosen scores (computed from the real SKILL.md body):

- **char_score:** byte length of the body.
- **complex_score:** `4 * heading_count + 8 * fence_count + list_count + chars/200`.
- **slot_score:** number of equipped ids.

Watermarks:

| Score | Low (sweet-spot floor) | High (hard refuse) |
|---|---|---|
| slots | 1 | 8 |
| chars (sum of equipped bodies) | 200 | 8000 (~2k tokens) |
| complexity (sum) | 2 | 64 |

Over high watermark → `HUSH_ERR_FULL`. Role mismatch → `HUSH_ERR_DENIED`.

## Role wall

Frontmatter `role: chaperon | worker | any`.  
Chaperon skill on worker → refuse. Worker skill on chaperon → refuse. `any` equips on both. Major is a worker who may chaperon a channel; he still uses **worker** skills for work jobs.

## SkillUI → C (no npm)

SkillUI default mode: fetch HTML/CSS, extract color tokens, typography, spacing; write `SKILL.md` + JSON. Ultra/Playwright is a Non-goal.

C `hush_skillui_extract` walks a buffer for `#hex`, `rgb()`, `--var:`, `font-family`, `font-size`, `padding`/`margin`. Writes a Hush skill package (tokens in the SKILL.md body). Fixture tests, not live crawl.

## Named sources → one Hush skill each (adapted, not vendor leftovers)

| Source | Hush skill | Notes |
|---|---|---|
| amaancoderx/npxskillui | `system:skillui-extract` (category reverse-engineering) | C extractor, not npm CLI |
| microsoft/AI-Engineering-Coach | `system:ai-engineering-coach` | session anti-patterns / canvas quality; not the VS Code extension |
| cloudflare/security-audit-skill | `system:security-audit` | recon→hunt→validate; no Node validator |
| charlie947/social-media-skills | `system:social-voice` | voice-first posts; drop Apify/Gemini SaaS |
| AgriciDaniel/claude-seo | `system:seo-audit` | technical+E-E-A-T checklist; no Playwright crawl |
| evoiz/Agentic-Design-Patterns | `system:agentic-patterns` | Gulli core patterns as Major knowledge |
| haddock-development/claude-reflect-system | `system:reflect-learn` | correct-once memory; local notes, not Claude hooks |
| rampstackco/claude-skills | `system:code-review-web` | web review workflow |
| VoltAgent/awesome-agent-skills | `system:write-discoverable` | curated slice (write-discoverable-code), not 1000+ dump |

All SKILL.md files must grep as **Hush-adapted** (hive/equip language), never leftover “Claude Code only” install.

## Chaperon pack (≥20)

Topic leash, token budget, bring-back, hop cap, cooldown, no-loop, on-topic, civility, secret-watch, pii-redact, rate-limit, human-cue, silence-nudge, claim-check, no-self-mention, channel-kind, job-cap, night-watch, conflict-break, summary-handoff, guardrail-log, chaperon-ack.

## Clone / lock

Locked template robots (Coach, Auditor, Voice) seed on vibe. `POST /api/agent {action:clone,slug}` copies to `<name> copy` unlocked. Payne slug → `HUSH_ERR_DENIED`. Enable slider stays. Locked originals refuse name/prompt/skill edits; enable still works.

## Seed path

Repo `skills/system/<slug>/SKILL.md` copied into `~/.hush/skills/system` via `hush_skill_seed_pack`. Tests pass pack dir. Catalog max 256. JSON max 64k.
