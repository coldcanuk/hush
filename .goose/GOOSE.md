# Goose for Hush

Hush is optimized exclusively for the Goose AI agent.

## Prime Directives
- All development happens in worktrees created inside the repository (worktrees/<slug>).
- From clean `main`: create worktree + branch `gb/<slug>`, implement, commit after every Milestone, push branch, return to main, merge --no-ff, delete worktree and (optionally) branch.
- No orphaned worktrees or branches are kept. Once a worktree serves its purpose, it is removed.
- `main` is protected: direct pushes discouraged; all changes via reviewed worktree merges.
- Goose is the only supported agent. Support for Claude, Codex, and other agents is deprecated.

## Skills
Skills live in `.goose/skills/<name>/SKILL.md` (or with runner scripts).

Core skills for Hush (C11 + Goose):
- worktree (lifecycle commands)
- c-build, c-test
- legible-c (apply write-legible-c + c-standard §14 checklist)
- relay (run hush-relay + raw Nostr line protocol tests)
- goose-init (bootstrap .goose layout)

## Build
```bash
./configure
make
make test
```

See AGENTS.md (short Hush version) and docs/research/RESEARCH.md.
