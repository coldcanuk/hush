#!/bin/sh
# Writes Hush-adapted system SKILL.md files. Run from repo root.
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

# --- chaperon-only (20) ---
write_skill topic-leash chaperon guardrail \
  "Keep robot notes on the channel about; bring wanderers back once." \
  "Read the channel about. If a robot note is off that topic, post one canned bring-back: quote the last human ask, then stop. A second offense in the same thread gets the canned halt. Do not start a grok job to debate. Absorbs bring-back and on-topic."

write_skill token-budget chaperon guardrail \
  "Stop robot talk at max_robot_turns." \
  "Count robot work notes on this thread (skip protocol acks such as Mention received). At max_robot_turns, emit the canned chaperon line and start no further grok jobs."

write_skill hop-cap chaperon guardrail \
  "Refuse unbounded robot-to-robot hops." \
  "If robot_hops is off, do not chain a follow-kick to another robot. If hops are on, still honor max_robot_turns. Never hop to a robot that is disabled."

write_skill cool-down chaperon guardrail \
  "Enforce channel cooldown_s between robot jobs." \
  "If cooldown_s has not elapsed since the last robot work note, hold. Do not spawn grok. Do not post a second Holding."

write_skill no-loop chaperon guardrail \
  "Break self-mention and ping-pong loops." \
  "If a robot names its own npub or repeats the last work line, stop the chain. Strip the self p-tag. Do not follow-kick that hex."

write_skill civility chaperon guardrail \
  "Keep the hive civil." \
  "Insults, slurs, and bait get one halt. Restate the human ask in one line. Do not grok on the bait."

write_skill secret-watch chaperon guardrail \
  "Watch for secrets in robot notes." \
  "If a note looks like an nsec, API key, bearer token, or pass path, redact to [redacted] and stop jobs. Do not repeat the secret."

write_skill pii-redact chaperon guardrail \
  "Stop robots from leaking emails, phones, or home addresses." \
  "Replace emails, phone numbers, and street addresses with [redacted]. Do not grok on the leaked text."

write_skill rate-limit chaperon guardrail \
  "Cap how fast one robot may speak." \
  "One work note per robot per cooldown window. Extra attempts are held. Distinct from channel cooldown_s (cool-down) and from turn caps (token-budget)."

write_skill human-cue chaperon guardrail \
  "Yield immediately when a human speaks." \
  "Cancel queued follow-kicks. The new human note is the standing order. Do not talk over it."

write_skill silence-nudge chaperon guardrail \
  "Nudge a silent robots-only room once, then wait." \
  "If no human appears after the cap, post at most one nudge. Then stay quiet."

write_skill claim-check chaperon guardrail \
  "Flag unverifiable claims in robot talk." \
  "Ask for a hive-local source (file, event id, test log). If none, mark unchecked. Do not invent facts."

write_skill channel-kind chaperon guardrail \
  "Honor channel kind: humans, robots, mixed, open." \
  "Robots-only rooms never address an absent human. Humans-only rooms stay silent. Mixed rooms wait for a mention unless policy says otherwise."

write_skill job-cap chaperon guardrail \
  "Honor max_jobs. Queue, do not pile Holding notes." \
  "When the job table is full, wait. Do not post a second Holding from Major or Marshal."

write_skill night-watch chaperon guardrail \
  "Overnight robot-only rooms stay at rest unless the user opted in." \
  "Default rails are off. Opt-in robot_talk plus hops is required to run unattended."

write_skill conflict-break chaperon guardrail \
  "Two robots arguing get one referee line." \
  "Restate the human ask. Name who speaks next. Then stop. Do not grok a debate."

write_skill summary-handoff chaperon guardrail \
  "Hand a short summary back to the human." \
  "Three bullets max: what ran, what is blocked, what to do next. Then stand down until mentioned."

write_skill guardrail-log chaperon guardrail \
  "Record chaperon events on the in-hive chan-events ring." \
  "Emit type chaperon with seq and due. No outbound HTTP. Do not duplicate the same halt."

write_skill chaperon-ack chaperon guardrail \
  "Acknowledge a human without taking work grok." \
  "One short standing-by note. Never start a worker grok job. Protocol acks are not work notes."

write_skill intro-once chaperon guardrail \
  "One intro per robot per thread." \
  "If this robot already posted an on-deck intro on this thread, do not intro again. Protocol acks such as Mention received do not count as intros. Distinct from topic-leash (topic), not a second bring-back."

# --- worker / any ---
write_skill canvas-coach worker quality \
  "Coach hive canvas and grok jobs toward small tested C changes." \
  "Hush-adapted from Microsoft AI-Engineering-Coach plus obra/superpowers process (not the VS Code extension, not a Claude Skill-tool bootstrap). Session standing orders: write a short plan, one C11 slice, run the shipped tests, commit. Watch vague prompts, missing tests, huge diffs. Local notes only. Process chip — not a line-by-line review (that is hive-review)."

write_skill hive-audit worker security \
  "Run a six-phase security audit of local C/hive code." \
  "Hush-adapted from Cloudflare security-audit-skill. Phases: recon, hunt, validate, report, structured findings, independent check. Only report exploitable issues you can prove in this tree. No Node validator."

write_skill hive-voice worker social \
  "Draft one hive-voice post from stored notes, not a SaaS farm." \
  "Hush-adapted from charlie947 social-media-skills. Read the robot voice and last hive notes. Write one post for one platform. No Apify, no Gemini keys, no spray to five networks."

write_skill hive-seo worker seo \
  "Audit hive-facing HTML for titles, headings, and citability." \
  "Hush-adapted from AgriciDaniel claude-seo. Check the page the hive actually serves (demo index or a project canvas). Titles, headings, schema, one falsifiable fix. No Playwright crawl farm."

write_skill hive-patterns any knowledge \
  "Apply Gulli agentic patterns as hive standing orders." \
  "Hush-adapted from evoiz Agentic-Design-Patterns for Major knowledge. Prefer prompt chaining, routing, reflection, HITL, guardrails on grok jobs. Not a Python notebook dump."

write_skill hive-reflect worker memory \
  "Learn from a human correction once and keep a local note." \
  "Hush-adapted from claude-reflect-system. Store HIGH-confidence use X instead of Y in a hive note. Replay it on the next similar job. No Claude Code hooks."

write_skill hive-review worker review \
  "Review a hive/C diff with file:line evidence only." \
  "Hush-adapted from rampstackco code-review-web. Given a diff, cite file and line, demand a test on the shipped path, verdict pass or fail. Do not rewrite the session plan (that is canvas-coach)."

write_skill write-legible worker craft \
  "Write C the hive can grep and change locally." \
  "Hush-adapted from the VoltAgent write-discoverable slice, on-brand with write-legible-c. Named constants, small functions, one producer per error. C11 -Wall -Werror. Not a 1000-skill dump."

write_skill token-extract worker reverse-engineering \
  "Extract design tokens from HTML/CSS into a Hush skill package." \
  "Hush-adapted from SkillUI static analysis (not npm). POST /api/skillui with HTML. Read colors, fonts, spacing. Write a skill the hive can equip. No Playwright ultra mode."

write_skill repo-trace worker reverse-engineering \
  "Reverse-engineer a local tree: languages, entry points, secrets patterns." \
  "List binaries, /api routes, and config in this checkout. Do not exfiltrate. Report only. Distinct from protocol-trace (wire tags) and mobile-trace (APK/IPA)."

write_skill protocol-trace worker reverse-engineering \
  "Trace hive HTTP/Nostr protocol from C sources." \
  "Map /api routes and event tags. Cite hush_http.c and hush_event.h. Distinct from repo-trace (tree inventory)."

write_skill mobile-trace worker reverse-engineering \
  "Static reverse-engineering standing orders for Android and iOS artifacts the user owns." \
  "Hush-adapted from SimoneAvogadro android-reverse-engineering-skill and iosre iOSAppReverseEngineering. Android and iOS in one chip. Fingerprint first (Flutter/RN/native, HTTP stack, obfuscation). Then structure, API hunt, secrets/entitlements. Android: APK, Retrofit/OkHttp/Ktor, Kotlin metadata. iOS: Mach-O, Info.plist, ATS, pinning, ObjC/Swift names. Do not ship jadx, Fernflower, class-dump, Frida, or Theos. Only inspect code the user is authorized to read. Report in hive notes."

write_skill hive-teardown worker reverse-engineering \
  "Reverse-engineer a product as a system: loop, moat, friction." \
  "Hush-adapted from yanliudesign product-teardown-skill. Snapshot, users, core loop, architecture, UX, business, friction, next move. Write a hive note. No bilingual Desktop HTML, no Python fill scripts."

write_skill hive-look worker design \
  "Design hive UI with one committed look: anti-slop, a11y, tokens." \
  "Hush-adapted from Trystan-SA claude-design-system-prompt, a slice of nextlevelbuilder ui-ux-pro-max (a11y and 44px touch, not the 84-style database), and plugin87 ux-ui-agent-skills (WCAG). Reject generic SaaS tropes. Commit to one palette and type. Contrast 4.5:1. Use POST /api/skillui to extract tokens. No npm CLI, no 138 brand kits, no Figma runtime."

write_skill hive-apps worker craft \
  "Build one small hive tool as a C11 hush-relay slice." \
  "Hush-adapted curated slice of Shubhamsaboo awesome-llm-apps (not the full index). Pick one job (channel note, local canvas, one grok). Implement in hush-c, secrets in pass, test on the shipped path. Do not copy a Python RAG demo zoo."

echo "wrote pack under $ROOT"
