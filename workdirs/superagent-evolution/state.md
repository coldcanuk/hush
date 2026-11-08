title: Evolve Hush towards Super-agent architecture
graph: on
budget: medium

jobs:
  - id: j1
    goal: Design the schema evolution strategy for $HUSH_HOME/router/ JSON files (tolerant readers vs versioning).
    owner: @floor
    status: ready
    done_test: design document approved and verified against current hush_router_load implementation
    cela_gate: COMPLETE

  - id: j2
    goal: Design the event-driven subscription/pass-through mechanism for Workers.
    owner: @floor
    status: ready
    depends_on: [j1]
    done_test: design document describes how workers subscribe to threads/events
    cela_gate: COMPLETE

  - id: j3
    goal: Implement schema evolution logic and a migration tool in hush-c.
    owner: @web-developer
    status: ready
    depends_on: [j1]
    done_test: hush_router_load succeeds on files with new/missing fields; migrator works
    cela_gate: COMPLETE

  - id: j4
    goal: Implement the worker subscription/pass-through mechanism in hush-c.
    owner: @web-developer
    status: ready
    depends_on: [j2]
    done_test: workers receive events from subscribed threads without Router intervention
    cela_gate: COMPLETE

  - id: j5
    goal: Research and design the "Speculative Execution" (acting ahead) component.
    owner: @ai-product
    status: ready
    depends_on: [j2]
    done_test: design document proposes background execution triggers
    cela_gate: COMPLETE

  - id: j6
    goal: Implement the Watchdog/Control Loop for system health and progress monitoring.
    owner: @ops
    status: ready
    depends_on: [j4]
    done_test: watchdog identifies a stalled/drifting worker and reports it
    cela_gate: COMPLETE

events:
