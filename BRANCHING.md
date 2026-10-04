# Branching & Main Protection for Hush

**Authority:** [PRIME_DIRECTIVE.md](PRIME_DIRECTIVE.md). If anything here conflicts, the Prime Directive wins.

## Policy

- `main` is the **only** long-lived branch.
- **All** development uses short-lived worktrees at  
  **`/opt/repo/hush/worktrees/<short-name>`** on branch **`gb/<short-name>`**.
- Never place worktrees under `/opt/repo/worktrees` or outside this repository.
- **Never commit on `main`. Never push to `main`.**  
  Landing is **Pull Request only** (review + auto-merge / approved merge on GitHub).
- After the PR merges: remove the worktree, delete `gb/<short-name>` locally and on origin if still present.
- Origin is main-only fetch (`remote.origin.fetch` + `tagopt = --no-tags`). Do not revive Buzz branches/tags.

## Codex workflow (mandatory)

```
clean main → worktree add gb/<short-name> → commit/push on branch
  → gh pr create → review → auto-merge
  → pull main → worktree remove → delete gb/<short-name>
```

Local `git merge` into `main` is **not** an acceptable land path.

## Protection mechanisms

1. **PRIME_DIRECTIVE.md** + this file + AGENTS.md + worktree skill.
2. **`./scripts/install-hooks.sh`** — `pre-commit` and `pre-push` reject work on `main`.
3. **GitHub branch protection** on `main` (require PR; block direct push) when enabled.
4. Agent discipline: Codex must refuse direct-main edits.

## Cleanup (after successful PR merge)

```bash
cd /opt/repo/hush
git checkout main
git pull --ff-only origin main
git worktree remove worktrees/<short-name>
git branch -d gb/<short-name>
git push origin --delete gb/<short-name>   # if remote branch remains
git worktree list
```
