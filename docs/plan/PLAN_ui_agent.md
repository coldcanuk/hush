# PLAN: UI Refactor & Agent Ownership Protocol

Branch: `gb/ui-agent-ownership`
Worktree: `worktrees/ui-agent-ownership`

## Scope

### Primary Goal
1. Refactor the entire Hush UI using `oatmeal-olive-instrument` as the primary source, augmented by `application-ui-v4` and `catalyst-ui-kit`. Ensure all rectangles have rounded edges.
2. Implement a highly efficient, precise, and quick NOSTR-based means to identify conversation ownership inside a channel when multiple AI agents are present. If ownership is unknown, all agents must coordinate to establish ownership before proceeding. If any agent is unsure at any point, the process restarts.

### Non-Goals
- Changing the underlying C11 web server (`hush_http.c`) beyond serving the new UI.
- Rewriting the C11 networking layer.

### Success Criteria
- The UI in `demo/index.html` (which gets compiled into `hush_ui_html.h`) is completely replaced with the Tailwind-based UI components.
- The C11 core (`hush_agent.c` / `hush_relay.c`) implements the agent ownership protocol via NOSTR events.

### Required Environment
- `make`, `gcc`, Tailwind CLI or raw static HTML/CSS files from the provided UI kits.

## Phase 0 - Environment & Isolation Setup
- [x] Create worktree `worktrees/ui-agent-ownership` on branch `gb/ui-agent-ownership`.

## Phase 1 - Research & Discovery
- **Milestone 1.1: UI Research**
  - Task 1: Investigate the existing UI in `hush-c/demo/index.html` and how it interacts with the backend.
  - Task 2: Investigate the provided Tailwind kits (`oatmeal-olive-instrument`, `application-ui-v4`, `catalyst-ui-kit`) to select the correct static HTML/CSS components.
- **Milestone 1.2: Agent Ownership Protocol Research**
  - Task 1: Analyze `hush_agent.c` to understand how agents are currently triggered.
  - Task 2: Analyze NOSTR NIPs to decide which kind to use for ownership election.
  - Task 3: Synthesize findings into `docs/research/RESEARCH_ui_agent.md` and update this plan.

## Phase 2 - Define / Architecture
- **Milestone 2.1: Design Ownership Protocol**
  - Task 1: Define the state machine and NOSTR events for agent conversation ownership.

## Phase 3 - Implementation (UI)
- **Milestone 3.1: Replace Index HTML**
  - Task 1: Generate a new `demo/index.html` and `demo/tailwind.css` using the Oatmeal design system.
  - Task 2: Ensure rounded rectangles (e.g., `rounded-xl` / `rounded-2xl`).

## Phase 4 - Implementation (Agent Protocol)
- **Milestone 4.1: Agent Election C Code**
  - Task 1: Implement the ownership election logic in `hush_agent.c`.

## Final Phase - Verification, Polish, Integration & Cleanup
- **Milestone 5.1: Testing & Submission**
  - Task 1: Run `make test` and ensure C11 strict compilation passes.
  - Task 2: Push branch `gb/ui-agent-ownership` and create PR.
