# PLAN: conversation fidelity + selectable robot pictures (RDAP)

Branch: `gb/conv-fidelity-pics`
Worktree: `worktrees/conv-fidelity-pics`
Base: `main` `64ed68f5f`
Land via PR only.

## Scope

**Primary.** In-place mention order with summary pills; progressive
thinking → reacting → emoji; one intro per robot+thread; documented
inter-robot mention standard wired into jobs; channel topic pills +
prompt; robot picture picker from sliced atlas tiles (no Raylib).

**Non-goals.** Raylib as hush-c dep; 360-rail; shipping 3.5MB JPEGs;
unbounded HRI essays; local merge to main.

**Success.** Goal criteria 1–5 + `make test` + PR.

## Architecture (frozen 2026-08-26)

1. `assembleMentionContent(raw, roster)` is the only writer of stored
   mention text: replace `@Name` in place with `nostr:<npub>`;
   `mention_i` follows appearance order. Pills are a **roster of who is
   in the bubble**, not a prepend buffer.
2. Ack phase: `status.thinking` plus a short client clock per
   `(parent, robot)` → thinking / reacting / emoji.
3. Intro: bounded table of `(hex, root)`, not one static pair.
4. Peer standard: named C string in `hush_agent.c` + `docs/`.
5. Channel `about` set from manage UI (topics + prompt).
6. Pictures: sliced equipment atlas (skip cat) at `/agent-atlas.png`;
   `picture` id on agent; session JSON emits it.

## Phases

### Phase 0 COMPLETE
Worktree `worktrees/conv-fidelity-pics`.

### Phase 1 GATE
Research file + this plan.
Commit: `Milestone 1.1: research JPEGs, Raylib lane, remaining plan`

### Phase 2 Architecture
Commit: `Milestone 2.1: lock in-place mentions, ack gradient, atlas picker`

### Phase 3 Mentions, acks, intro
- Task 1: `assembleMentionContent` in demo JS; composer + thread submit
  use it; pills remain.
- Task 2: progressive ack chips; re-paint on tick.
- Task 3: C intro table in `hush_agent_on_deck`.
- Task 4: embed + build.
Commit: `Milestone 3.1: in-place mentions, ack gradient, one-intro table`

### Phase 4 Standard, topics, pictures
- Task 1: `docs/ROBOT_TO_ROBOT.md` + `HUSH_AGENT_PEER_STANDARD`.
- Task 2: channel topic pills + prompt in manage; HTTP sets `about`.
- Task 3: slice atlas (skip feline); serve `/agent-atlas.png`; picker;
  emit `picture` in agent JSON.
Commit: `Milestone 4.1: peer standard, channel topics, robot picture picker`

### Phase 5 Tests
check_launch / check_agent; make test; mention-order curl; live probe.
Commit: `Milestone 5.1: tests for mention order, acks, topics, pictures`

### Final
PR to main; remove worktree.
