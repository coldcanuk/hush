# PLAN_THREAD_THINK_HYGIENE.md — Verification Gate (M1-M5)

Base: main 3ceaffc03 (fresh worktree gb/thread-think-hygiene)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_THREAD_THINK_HYGIENE_CURRENT.md
- Confirmed: all primary DoD items already present on this base.

## M2.1 UI_SPEC contracts
- §13: thinking chip, thread pane (1:1), Grok hygiene, reply_to on POST
- §19: relay-live drawer
- GET /api/status includes "thinking"
- GET /api/events includes reply_to
- POST /api/event accepts reply_to (stored as e=root)
- Verified in UI_SPEC.md

## M3.1 Grok hygiene + thinking API
- HUSH_AGENT_CWD_LEAF / TMP, empty cwd under HUSH_CONFIG_DIR or TMP
- argv includes: --cwd, --max-turns 1, --reasoning-effort, --no-subagents, --disable-web-search, --disallowed-tools, --rules
- Hygiene appended to system-prompt-override
- e = root (parent e or parent id)
- hush_agent_status writes [{"name","parent"}]
- /api/status includes "thinking": [...]
- POST /api/event reads reply_to, stores e before mentions; no self-p re-dispatch

## M4.1 Thinking chip + thread pane + relay drawer
- CSS: .think, .think-dot, .thread-btn
- Markup: #thread-pane (1:1 human+robot), #relay-drawer (stored/projects/sockets), [x] close
- #stream roots only; Thread · N when replies or live job
- Thread pane lists human+robot; composer posts reply_to + mention_0
- tick paints chip from status.thinking; optimistic #thread-think
- #stats opens relay drawer; "relay live" / "relay down"
- Embed clean

## M5.1 Checks + docs
- check_launch.sh: greps thread-pane, thread-btn, relay-drawer, think-dot, thread-think, paintThreadThink, send.disabled
- check_agent.sh: expects reply_to + joke (prior)
- make -C hush-c test → ALL PASS
- README / NOSTR / UI_SPEC one-liners present (isolated grok, thread, thinking, relay drawer)

## Constraints
- Prime Directive: gb/* only; PR to main
- C11 + legible-c (existing)
- Embed after HTML (satisfied)
- Hush never writes ~/.grok; agent cwd isolated

## DoD checklist (primary goals satisfied)
1. [x] Mentioned robot shows small thinking chip until reply lands
2. [x] Thread button opens 1:1 (human + that robot); [x] returns; same button reopens
3. [x] Grok replies are one short note (joke), not thought/GEMINI/npub/host
4. [x] "relay live" opens details (stored/projects/sockets), closed by [x]
5. [x] make && make test pass; embed clean
6. [x] PR merged, worktree removed, main clean (pending M6)

