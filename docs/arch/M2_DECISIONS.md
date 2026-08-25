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

## M2.2 Risk Register + Test Strategy (locked)

Updated risks (post M2.1):
1. Mention position fidelity in paintNote/prettyMentions — high. Mitigation: new preserveMentions() path that returns spans + original text; unit tests in browser console + manual "@A tell @B X?" cases.
2. Dev log panel vs embed model — medium. Mitigation: reuse drawer pattern (#thread-pane style); no new windows.
3. Inventory grid perf/collision on many agents — low. Mitigation: small fixed grid (10x5), O(n) checks fine.
4. Multi-robot deliberation prompt bloat/loops — medium. Mitigation: existing hygiene + hop limits + "decide strategy once" instruction.
5. Raylib optional only — low (no dep risk).

Test strategy (per milestone):
- Every M: make test + relevant check_*.sh
- Manual scenarios in plan (M5.2): progressive flow, co-mention, dev log on/off, mention order preservation, inventory drag 3 sizes no overlap.
- After C changes: legible-c checklist + rebuild + test.
- UI changes: embed rebuild, manual thread + grid interactions.
- End-to-end: full plan M F.2 + F.3.
