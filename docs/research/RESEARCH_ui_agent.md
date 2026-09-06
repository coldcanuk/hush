# Research Findings: UI Refactor & Agent Ownership

## UI Refactor
- The existing UI is contained in a massive `hush-c/demo/index.html` file (266KB, ~6000 lines), which has embedded CSS, HTML structure, and complex vanilla JS.
- Refactoring the entire DOM manually into Tailwind HTML is impractical without breaking the tightly coupled JS `querySelector` and `getElementById` logic.
- The safest and most effective way to apply the `oatmeal-olive-instrument` theme and draw from `application-ui-v4` is to rewrite the `<style>` block of `demo/index.html`.
- We will replace the root CSS variables with Oatmeal's `--color-olive-*` palette, update typography to Instrument Serif and Inter, and adjust all `border-radius` values to ensure rounded rectangles (e.g., `0.75rem` / `12px`) instead of sharp edges or overly pill-shaped elements.

## Agent Ownership Protocol
- Existing logic in `hush_agent.c` uses NOSTR NIP-10 threads. A thread is owned by the human who starts it, but the queue of which robot speaks next is transient and memory-bound.
- For channel-wide "conversation ownership", we will introduce a new NIP-77 (kind 29007) transient event.
- When an agent believes it should speak or take ownership, it broadcasts a 29007 event asserting ownership.
- If multiple agents try to assert ownership concurrently, they will resolve it by electing the agent with the lowest pubkey (or just first-seen).
- If any agent is unsure, it will clear its local state and wait for or trigger a new election.

## Plan Updates
- We will proceed to Milestone 2.1 to define the C protocol, and then execute the UI refactor via an automated Python patch script to ensure no JS is broken.
