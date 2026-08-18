# Goose worktrees (inside the Hush repo)

All Hush worktrees **must** live here:

```text
/opt/repo/hush/worktrees/<slug>
```

Created as:

```bash
git worktree add -b gb/<slug> worktrees/<slug>
```

## Rules

- **Do not** put worktrees under `/opt/repo/worktrees` or any path outside this repo.
- Branch name: `gb/<slug>`.
- Commit and push on the worktree branch; land with a **Pull Request** into `main`.
- After the PR merges, remove the worktree.

This directory is gitignored except for this README.

See [PRIME_DIRECTIVE.md](../PRIME_DIRECTIVE.md), [AGENTS.md](../AGENTS.md), and [.goose/skills/worktree/SKILL.md](../.goose/skills/worktree/SKILL.md).
