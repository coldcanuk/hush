# Goal: Evolve Hush towards Super-agent architecture (Router/Worker)

## Outcome
A robust, schema-evolvable, and event-driven Router/Worker system in Hush that implements the core principles of the DeepSeek Super-agent architecture.

## Deadline
TBD

## Never without my yes
- Merging any changes to the core router logic or JSON schema.
- Any architectural changes that affect the single-binary constraint.
- Deployment of new experimental routing modes.

## Done Test
- The Router successfully loads, updates, and saves router JSON files even after schema changes.
- Workers can react to environmental events (Triggers) via a subscription mechanism.
- The system maintains "Continuous, Productive, Proactive, and Persistent" characteristics as defined in the DeepSeek Harness research.

## Tools
- `terminal` (for all C development and testing)
- `gh` (for issue tracking and PRs)
- `ceci` (for job management)
- `cela` (for code verification)

## Approach: compare
Compare the "evolutionary" approach (incremental updates to existing files) vs the "clean-break" approach (new directory structure/format).
