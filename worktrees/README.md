# Codex worktrees (inside the Hush repo)

All Hush worktrees **must** live here:

```text
/opt/repo/hush/worktrees/<short-name>
```

Created as:

```bash
git worktree add -b gb/<short-name> worktrees/<short-name>
```

## Rules

- **Do not** put worktrees under `/opt/repo/worktrees` or any path outside this repo.
- Branch name: `gb/<short-name>`.
- Commit and push on the worktree branch; land with a **Pull Request** into `main`.
- After the PR merges, remove the worktree.

This directory is gitignored except for this README.

See [PRIME_DIRECTIVE.md](../PRIME_DIRECTIVE.md), [AGENTS.md](../AGENTS.md), and [.agents/skills/worktree/SKILL.md](../.agents/skills/worktree/SKILL.md).
