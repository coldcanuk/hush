# RESEARCH: Skill pack inventory, Bayesian consolidation, Hush remap

**Date:** 2026-08-26  
**Worktree:** `worktrees/skill-pack-map` / `gb/skill-pack-map`  
**Sources:** live `skills/system/` (34 dirs + forge-skill) and eight named GitHub repos.

## Shipped hive pack (before MAP)

| Cluster | Slug | Role | Category | Source / notes |
|---|---|---|---|---|
| Chaperon rails | topic-leash | chaperon | guardrail | hive original |
| Chaperon rails | on-topic | chaperon | guardrail | overlaps topic-leash |
| Chaperon rails | bring-back | chaperon | guardrail | hive original |
| Chaperon rails | token-budget | chaperon | guardrail | hive original |
| Chaperon rails | hop-cap | chaperon | guardrail | hive original |
| Chaperon rails | cool-down | chaperon | guardrail | hive original |
| Chaperon rails | rate-limit | chaperon | guardrail | hive original |
| Chaperon rails | job-cap | chaperon | guardrail | hive original |
| Chaperon rails | no-loop | chaperon | guardrail | hive original |
| Chaperon rails | no-self-mention | chaperon | guardrail | overlaps no-loop |
| Chaperon rails | human-cue | chaperon | guardrail | hive original |
| Chaperon rails | silence-nudge | chaperon | guardrail | hive original |
| Chaperon rails | chaperon-ack | chaperon | guardrail | hive original |
| Chaperon rails | summary-handoff | chaperon | guardrail | hive original |
| Chaperon rails | channel-kind | chaperon | guardrail | hive original |
| Chaperon rails | night-watch | chaperon | guardrail | hive original |
| Chaperon rails | civility | chaperon | guardrail | hive original |
| Chaperon rails | claim-check | chaperon | guardrail | hive original |
| Chaperon rails | conflict-break | chaperon | guardrail | hive original |
| Chaperon rails | secret-watch | chaperon | guardrail | hive original |
| Chaperon rails | pii-redact | chaperon | guardrail | hive original |
| Chaperon rails | guardrail-log | chaperon | guardrail | hive original |
| Reverse engineering | skillui-extract | worker | reverse-engineering | SkillUI C, not npm |
| Reverse engineering | repo-static-audit | worker | reverse-engineering | hive |
| Reverse engineering | protocol-trace | worker | reverse-engineering | hive HTTP/Nostr |
| Quality / process | ai-engineering-coach | worker | quality | Microsoft AI-Engineering-Coach |
| Quality / process | write-discoverable | worker | craft | VoltAgent slice |
| Quality / process | reflect-learn | worker | memory | claude-reflect-system |
| Quality / process | code-review-web | worker | review | rampstackco |
| Quality / process | agentic-patterns | any | knowledge | evoiz / Gulli |
| Security | security-audit | worker | security | Cloudflare |
| Design / UX | (none yet) | — | — | new sources below |
| Social / SEO | social-voice | worker | social | charlie947 |
| Social / SEO | seo-audit | worker | seo | AgriciDaniel |
| Hive craft | forge-skill | any | — | canonical forge |

Chaperon count before MAP: **22**. Worker/any besides forge: **12**.

## Named sources (this goal)

| Source | What it actually is | Dump cost | Hush-adapt |
|---|---|---|---|
| Trystan-SA/claude-design-system-prompt | Reverse-engineered Claude Design prompt + **14** procedural skills (wireframe, tokens, a11y, slop-check…) | 14 chips + 20-chapter prompt | One design-craft skill: anti-slop, hierarchy, a11y. Not 14 files. |
| SimoneAvogadro/android-reverse-engineering-skill | Claude plugin: jadx/Fernflower, Retrofit/OkHttp/Ktor extract | Bundled decompilers (Non-goal) | Standing-order Android RE: fingerprint, static API hunt. No jadx. |
| iosre/iOSAppReverseEngineering | Book: concepts, tools (class-dump, Theos, Cycript, IDA), ObjC/ARM | PDF + jailbreak toolchain | Standing-order iOS RE: Mach-O, entitlements, ATS. No Frida/class-dump binary. |
| yanliudesign/product-teardown-skill | 11-section PM teardown + bilingual HTML to Desktop | Desktop HTML + Python fill | One product-as-system RE skill; hive notes, not Desktop HTML. |
| obra/superpowers | Claude bootstrap: invoke Skill tool first, brainstorming, plans, worktrees | 10+ process chips + Claude-only ritual | Fold small-step plan/verify into canvas coach. No Skill-tool mandate. |
| Shubhamsaboo/awesome-llm-apps | Huge index of LLM apps (RAG, chat, agents) | 100+ apps | One curated slice: hive-sized tools, C11 first. Not the index. |
| nextlevelbuilder/ui-ux-pro-max-skill | 79 styles / 192 palettes, `npx` CLI | 84-style database (Non-goal) | Priority rules only (a11y, touch 44px). No CSV dump, no npm. |
| plugin87/ux-ui-agent-skills | 17 skills, 138 design systems, WCAG 2.2, npm init | 17 chips + Figma | Fold a11y/review into hive-look. No Figma, no 138 brands. |

## Overlap clusters (consolidate candidates)

1. **Topic rails:** `topic-leash` ≈ `on-topic` ≈ `bring-back` (bring-back is the action; leash/on-topic is the score). Fold on-topic into topic-leash. Keep bring-back.
2. **Loop rails:** `no-loop` ≈ `no-self-mention`. Fold self-mention into no-loop.
3. **UX/design:** SkillUI extract + Claude Design 14 + ui-ux-pro-max 84 + plugin87 17. Three sources, one craft skill + keep C token extract (wired to `/api/skillui`).
4. **Mobile RE:** Android plugin ≈ iOS book (fingerprint → structure → APIs → secrets). One `mobile-trace` that names both platforms.
5. **Process/quality:** Superpowers plans/TDD ≈ canvas coach ≈ write-discoverable. Fold Superpowers into canvas-coach; keep write-legible as C craft (write-legible-c brand).

## Bayesian update

Hypothesis set (mutually exclusive remap policies):

| Id | Theory | Prior P(T) |
|---|---|---|
| T1 | Union-dump every upstream file as its own armory chip | 0.08 |
| T2 | Zero new chips; mention sources only inside existing three RE skills | 0.15 |
| T3 | Cluster fold: 1 mobile-RE, 1 teardown, 1 hive-look, Superpowers→canvas-coach, 1 LLM-app slice; rename vendor ids; fold two chaperon pairs (keep 20) | 0.45 |
| T4 | Separate android-trace + ios-trace plus five UX chips | 0.22 |
| T5 | One mega reverse-engineering skill for tokens + mobile + protocol + teardown | 0.10 |

Evidence **E**: easy-to-scan armory; ≥20 distinct chaperon skills; each of the eight sources has ≥1 Hush-adapted skill; no jadx/npm/Claude leftover; RE covers Android, iOS, teardown, token extract; watermarks still bind.

Likelihood P(E|T) from overlap + watermark + role-wall + overwhelm:

| T | P(E\|T) | Why |
|---|---|---|
| T1 | 0.05 | Floods chips; blows complexity watermark; vendor names stay |
| T2 | 0.35 | Misses “at least one skill per named source” |
| T3 | 0.88 | Hits all gates; armory grows by ~4 worker chips, not 50 |
| T4 | 0.40 | Two mobile chips + five UX chips overwhelm; still no jadx |
| T5 | 0.28 | One body cannot name four RE workflows under char/complexity caps |

Unnormalized P(E|T)P(T): T1 0.004, T2 0.0525, T3 **0.396**, T4 0.088, T5 0.028.  
Sum = 0.5685. **MAP T3 posterior ≈ 0.70.**

Pick **T3**.

## MAP remap table

### Chaperon (20; two folds)

Keep hive names (already on-brand). Fold `on-topic` → `topic-leash`. Fold `no-self-mention` → `no-loop`.

topic-leash, token-budget, bring-back, hop-cap, cool-down, no-loop, civility, secret-watch, pii-redact, rate-limit, human-cue, silence-nudge, claim-check, channel-kind, job-cap, night-watch, conflict-break, summary-handoff, guardrail-log, chaperon-ack.

### Worker / any (rename vendor leftovers)

| Old slug (must be absent) | New slug | Role | Category | Sources folded |
|---|---|---|---|---|
| ai-engineering-coach | canvas-coach | worker | quality | Microsoft coach + obra/superpowers process |
| security-audit | hive-audit | worker | security | Cloudflare |
| social-voice | hive-voice | worker | social | charlie947 |
| seo-audit | hive-seo | worker | seo | AgriciDaniel |
| agentic-patterns | hive-patterns | any | knowledge | evoiz |
| reflect-learn | hive-reflect | worker | memory | haddock reflect |
| code-review-web | hive-review | worker | review | rampstackco |
| write-discoverable | write-legible | worker | craft | VoltAgent slice |
| skillui-extract | token-extract | worker | reverse-engineering | SkillUI C |
| repo-static-audit | repo-trace | worker | reverse-engineering | hive |
| (new) | mobile-trace | worker | reverse-engineering | Android + iOS (MAP fold) |
| (new) | hive-teardown | worker | reverse-engineering | yanliudesign |
| (new) | hive-look | worker | design | Trystan-SA + ui-ux-pro-max slice + plugin87 a11y |
| (new) | hive-apps | worker | craft | Shubhamsaboo curated slice |
| protocol-trace | protocol-trace | worker | reverse-engineering | keep (already Hush) |
| forge-skill | forge-skill | any | — | keep |

Locked templates: Coach equips `system:canvas-coach`. Auditor equips `system:hive-audit`. No new locked army.

## Shipped after MAP (grouped)

| Cluster | Slug | Role | Named source |
|---|---|---|---|
| Chaperon rails | topic-leash, token-budget, bring-back, hop-cap, cool-down, no-loop, civility, secret-watch, pii-redact, rate-limit, human-cue, silence-nudge, claim-check, channel-kind, job-cap, night-watch, conflict-break, summary-handoff, guardrail-log, chaperon-ack | chaperon | hive (20) |
| Reverse engineering | token-extract | worker | SkillUI C |
| Reverse engineering | repo-trace, protocol-trace | worker | hive |
| Reverse engineering | mobile-trace | worker | SimoneAvogadro + iosre (fold) |
| Reverse engineering | hive-teardown | worker | yanliudesign |
| Design / UX | hive-look | worker | Trystan-SA + nextlevelbuilder slice + plugin87 a11y |
| Quality / process | canvas-coach | worker | Microsoft + obra/superpowers |
| Quality / process | write-legible, hive-review, hive-reflect, hive-patterns | worker/any | VoltAgent, rampstackco, haddock, evoiz |
| Security | hive-audit | worker | Cloudflare |
| Social / SEO | hive-voice, hive-seo | worker | charlie947, AgriciDaniel |
| Hive craft | hive-apps | worker | Shubhamsaboo slice |
| Hive craft | forge-skill | any | canonical |

## Not shipped

jadx, Fernflower, class-dump, Frida, Theos, npm `ui-ux-pro-max-cli`, 84-style CSV, 14 Claude Design files as chips, Superpowers Skill-tool bootstrap, awesome-llm-apps full index.
