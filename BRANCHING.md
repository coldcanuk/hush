# Branching & Main Protection for Hush

## Policy
- `main` is the only long-lived branch.
- All development uses short-lived worktrees under **this repo's** `worktrees/` (e.g. `/opt/repo/hush/worktrees/<slug>`).
  Do **not** place Hush worktrees under `/opt/repo/worktrees` or other sibling paths.
- Never push directly to `main`.
- Feature branches are `gb/<slug>` created by `git worktree add -b ...`.
- When work is complete: merge --no-ff into main, push, remove worktree, delete the gb/ branch locally.
- Remote Buzz history is not fetched: origin is configured for `main` only (`remote.origin.fetch` + `tagopt = --no-tags`).
  Do not base work on old Buzz branches or tags.

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

