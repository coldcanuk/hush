# PLAN: scrub robot replies (echo / npub / last handoff)

Research: `docs/research/RESEARCH_SCRUB_ROBOT_REPLY.md`
Branch: `gb/scrub-robot-reply`

C contract after rewrite: drop self-mentions, drop ask-echo sentences,
drop handoff phrases, last robot drops remaining npub tokens, tidy.
Unknown npubs are deleted, not displayed. Last robot emits no extra p.
UI skips unresolved `@npub1` pills.

`check_agent.sh` feeds the live transcript strings through fake grok
and asserts they are stripped. `make test` green.
