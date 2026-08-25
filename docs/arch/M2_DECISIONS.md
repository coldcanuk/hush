# M2 Architecture Decisions (locked)

See also: UI_SPEC.md (updated with inventory, dev logging, mention fidelity, deliberation, topics, rail, progressive UX), PLAN_CHAT_ROBOTS_INVENTORY.md, RESEARCH_*.md, examples/inventory-raylib/README.md.

## Decisions
- Web grid primary for Robots inventory (CSS grid + pointer events + JS state machine matching Gemini Raylib spec). Raylib = optional example only.
- Developer Logging: bool flag (default false). Routes intel post_line, on_deck, debug to separate panel (syslog lines). Main chat stream is clean when off.
- Mention fidelity: preserve original text positions and order. Non-destructive pill rendering.
- Progressive acks: 
  1. "X is thinking" via status.thinking + transient client chip.
  2. "X is reacting" on job start.
  3. Final ack = emoji pill only (no "Mention received" text note).
- Single intro: on_deck guard (per robot per root/session).
- Multi-robot: extend existing co_npubs + prompt injection + deliberation instruction. Robots may p-mention peers.
- Channel topics: existing `about` injection + pill UI surface.
- Tool rail: micro UX (grip pattern, labels, 44px, progressive disclosure).
- No new hard native deps for core. Embed model preserved.

## Data model sketches (for P3 impl)
- Launch/session: dev_log_enabled (int 0/1), persisted.
- Status: keep/extend thinking[] for progressive states.
- Roster/agents: add optional grid_slot_x/y/w/h (for inventory persistence, later).
- Job: already has co_npubs + n_co_robots.

## Risk register update
- Fidelity render risk: mitigated by new preserve path + tests.
- Grid UX vs embed: web first, Raylib stretch.
- Log window: drawer in web (no native window needed).
- Deliberation loops: prompt hygiene + existing hop limits.

## Verification for M2
- UI_SPEC.md updated and reviewed.
- Decision artifacts present (header comment, examples/README, this file).
- Plan references match.
