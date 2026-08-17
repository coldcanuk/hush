# Branching & Main Protection for Hush

## Policy
- `main` is the only long-lived branch.
- All development uses short-lived worktrees under `worktrees/`.
- Never push directly to `main`.
- Feature branches are `gb/<slug>` created by `git worktree add -b ...`.
- When work is complete: merge --no-ff into main, push, remove worktree, delete the gb/ branch locally.
- Remote `origin/*` branches from the Buzz fork are historical and ignored for development. Do not base work on them.

## Goose Workflow (mandatory)
See AGENTS.md "Prime Directive: Worktrees Only".

## Protection Mechanisms
- Documentation and team discipline (this file).
- Optional: install a pre-push hook that rejects direct pushes to main (see scripts/install-hooks.sh).
- CI (when present) can require PRs or signed commits for main.

## Cleanup
After merge:
git worktree remove worktrees/<slug>
git branch -d gb/<slug>   # or git push origin --delete gb/<slug>

