#!/bin/sh
# Writes Hush-adapted system SKILL.md files (MAP T3). Run from repo root.
set -eu
ROOT="${1:-skills/system}"

write_skill() {
    slug="$1"
    role="$2"
    category="$3"
    desc="$4"
    body="$5"
    dir="$ROOT/$slug"
    mkdir -p "$dir"
    cat > "$dir/SKILL.md" <<EOF
---
name: $slug
description: $desc
role: $role
category: $category
---

# $slug

Hush-adapted skill. Equip from the hive Armory. Not a Claude Code install.

$body
EOF
}

# --- chaperon-only (20 after MAP folds) ---
write_skill topic-leash chaperon guardrail \
  "Keep robot notes on the channel about." \
  "Score each robot note against the channel about. If a robot drifts, post one short bring-back line. A second offense triggers the canned stop. Do not start a new grok job. Do not debate. Absorbs the old on-topic chip."

write_skill token-budget chaperon guardrail \
  "Stop robot talk when the channel has chewed enough tokens." \
  "Count robot work notes. At max_robot_turns, emit the canned chaperon line. No further jobs."

write_skill bring-back chaperon guardrail \
  "Return wandering robots to the standing orders." \
  "Quote the last human ask. Tell the room to answer that, not the weather."

write_skill hop-cap chaperon guardrail \
  "Refuse unbounded robot-to-robot hops." \
  "If hops are off, do not chain. If hops are on, still honor max_robot_turns."

write_skill cool-down chaperon guardrail \
  "Enforce channel cooldown between robot jobs." \
  "If cooldown_s has not elapsed, hold. Do not spawn grok."

write_skill no-loop chaperon guardrail \
  "Break self-mention and ping-pong loops." \
  "If a robot names itself or repeats the last line, stop the chain. Self npub in a robot note is a loop: strip it and do not follow-kick that hex. Absorbs the old no-self-mention chip."

write_skill civility chaperon guardrail \
  "Keep the hive civil." \
  "Insults, slurs, and bait get a halt. Restate the human ask."

write_skill secret-watch chaperon guardrail \
  "Watch for secrets in robot notes." \
  "If a note looks like a key, token, or nsec, redact and stop jobs."

write_skill pii-redact chaperon guardrail \
  "Stop robots from leaking emails, phones, or home addresses." \
  "Replace PII with [redacted]. Do not grok on the leaked text."

write_skill rate-limit chaperon guardrail \
  "Cap how fast robots may speak." \
  "One work note per robot per cooldown. Extra attempts are held."

write_skill human-cue chaperon guardrail \
  "Yield immediately when a human speaks." \
  "Cancel queued follow-kicks. The human note is the new standing order."

write_skill silence-nudge chaperon guardrail \
  "Nudge a silent robots-only room once, then wait." \
  "If no human appears after the cap, stay quiet."

write_skill claim-check chaperon guardrail \
  "Flag unverifiable claims in robot talk." \
  "Ask for a source or mark as unchecked. Do not invent facts."

write_skill channel-kind chaperon guardrail \
  "Honor channel kind: humans, robots, mixed, open." \
  "Robots-only rooms never address an absent human. Humans-only rooms stay silent."

write_skill job-cap chaperon guardrail \
  "Honor max_jobs. Queue, do not pile Holding notes." \
  "When the job table is full, wait. Do not post a second Holding from Major."

write_skill night-watch chaperon guardrail \
  "Overnight robot-only rooms stay at rest unless the user opted in." \
  "Default rails are off. Opt-in robot_talk plus hops is required to run."

write_skill conflict-break chaperon guardrail \
  "Two robots arguing get one referee line." \
  "Restate the human ask. Name who speaks next. Then stop."

write_skill summary-handoff chaperon guardrail \
  "Hand a short summary back to the human." \
  "Three bullets max. Then stand down until mentioned."

write_skill guardrail-log chaperon guardrail \
  "Record chaperon events on the in-hive chan-events ring." \
  "Emit type chaperon with seq and due. No outbound HTTP."

write_skill chaperon-ack chaperon guardrail \
  "Acknowledge a human without taking work grok." \
  "One short standing-by note. Never start a worker job."

# --- worker / any (Hush-renamed, MAP folds) ---
write_skill canvas-coach worker quality \
  "Coach hive canvas and grok jobs toward small tested C changes." \
  "Hush-adapted from Microsoft AI-Engineering-Coach (not the VS Code extension) plus obra/superpowers process (plans, small steps, verify). Watch anti-patterns: vague prompts, missing tests, huge diffs. Prefer small C11 changes. Write a short plan, commit per slice, run the shipped tests. Local session notes only. Not a Claude Skill-tool bootstrap."

write_skill hive-audit worker security \
  "Run a six-phase security audit of local C/hive code." \
  "Hush-adapted from Cloudflare security-audit-skill. Phases: recon, hunt, validate, report, structured findings, independent check. Only report exploitable issues. No Node validator."

write_skill hive-voice worker social \
  "Draft social posts in a stored hive voice." \
  "Hush-adapted from charlie947 social-media-skills. Build voice notes first. Write one platform post. No Apify, no Gemini SaaS keys."

write_skill hive-seo worker seo \
  "Audit a page for technical SEO and E-E-A-T gaps." \
  "Hush-adapted from AgriciDaniel claude-seo. Check titles, headings, schema, citability. Falsifiable recommendations. No Playwright crawl farm."

write_skill hive-patterns any knowledge \
  "Apply Gulli agentic design patterns in the hive." \
  "Hush-adapted from evoiz Agentic-Design-Patterns for Major knowledge. Prefer prompt chaining, routing, reflection, HITL, guardrails. Not a Python notebook dump."

write_skill hive-reflect worker memory \
  "Learn from human corrections once and keep the note." \
  "Hush-adapted from claude-reflect-system. Store HIGH-confidence use X instead of Y in a local hive note. No Claude Code hooks."

write_skill hive-review worker review \
  "Review hive and C changes with evidence." \
  "Hush-adapted from rampstackco claude-skills code-review-web. Cite files. Demand tests on the shipped path. No leftover Claude install steps."

write_skill write-legible worker craft \
  "Write C the hive can grep and change locally." \
  "Hush-adapted from the VoltAgent awesome-agent-skills slice (write-discoverable-code), on-brand with write-legible-c. Named constants, small functions, one producer per error. Not a 1000-skill dump."

write_skill token-extract worker reverse-engineering \
  "Extract design tokens from HTML/CSS into a Hush skill package." \
  "Hush-adapted from SkillUI static analysis (not npm). POST /api/skillui with HTML. Read colors, fonts, spacing. Write a skill the hive can equip. No Playwright ultra mode."

write_skill repo-trace worker reverse-engineering \
  "Reverse-engineer a local tree: languages, entry points, secrets patterns." \
  "Hush reverse-engineering category. List binaries, APIs, and config. Do not exfiltrate. Report only."

write_skill protocol-trace worker reverse-engineering \
  "Trace a hive HTTP/Nostr protocol from C sources." \
  "Hush reverse-engineering category. Map /api routes and event tags. Cite hush_http.c and hush_event.h."

write_skill mobile-trace worker reverse-engineering \
  "Static reverse-engineering standing orders for Android and iOS artifacts the user owns." \
  "Hush-adapted from SimoneAvogadro android-reverse-engineering-skill and iosre iOSAppReverseEngineering. Android and iOS in one chip (MAP fold). Fingerprint first (Flutter/RN/native, HTTP stack, obfuscation). Then structure, API hunt, secrets/entitlements. Android: APK layout, Retrofit/OkHttp/Ktor patterns, Kotlin metadata. iOS: Mach-O, Info.plist, ATS, pinning, ObjC/Swift names. Do not ship jadx, Fernflower, class-dump, Frida, or Theos. Only inspect code the user is authorized to read. Report in hive notes."

write_skill hive-teardown worker reverse-engineering \
  "Reverse-engineer a product as a system: loop, moat, friction." \
  "Hush-adapted from yanliudesign product-teardown-skill. Snapshot, users, core loop, architecture, UX, business, friction, next move. Write a hive note, not a bilingual Desktop HTML report. No Python fill scripts."

write_skill hive-look worker design \
  "Design hive UI with one committed look: anti-slop, a11y, tokens." \
  "Hush-adapted from Trystan-SA claude-design-system-prompt (anti-slop, hierarchy), a curated slice of nextlevelbuilder ui-ux-pro-max (a11y and 44px touch first, not the 84-style database), and plugin87 ux-ui-agent-skills (WCAG review). Reject generic SaaS tropes. Commit to one palette and type. Contrast 4.5:1. Keyboard and labels. Use POST /api/skillui to extract tokens. No npm CLI, no 138 brand kits, no Figma runtime."

write_skill hive-apps worker craft \
  "Build small useful hive tools, not a zoo of LLM demo apps." \
  "Hush-adapted curated slice of Shubhamsaboo awesome-llm-apps. Prefer one C11 hush-relay vertical (chat, local notes, one grok job) over copying the full index of Python RAG demos. Keep secrets in pass. Not a 100-app dump."

echo "wrote pack under $ROOT"
