# Codex migration

Research gate: [accepted synthesis](../research/RESEARCH_CODEX_MIGRATION.md).

1. [x] Remove the retired provider from registry, dispatch, UI, and update scan;
   normalize saved robot and Payne selections to Codex. Replace the runtime
   smoke test with Codex coverage, including OAuth and skill availability.
2. [x] Port and install write-legible-c for Codex locally and in the repository;
   update agent guides and affected documentation. Preserve upstream licensing.
3. Run strict build, full tests, skill validation, and review the diff. Commit
   milestones on gb/replace-agy-with-codex, push, open a PR, review, merge through
   GitHub, fast-forward main, and delete the merged worktree and branch.
