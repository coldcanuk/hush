---
name: worktree
description: "MANDATORY Hush lifecycle: worktree → commit/push on gb/* → PR + review + auto-merge → delete worktree. Direct main is forbidden."
---
# Worktree (Prime Directive)

**Full law:** repo root `PRIME_DIRECTIVE.md`. Obey it.

## Hard rules

1. Always create a worktree (`worktrees/<slug>`, branch `gb/<slug>`).
2. Commit **and** push on that branch only.
3. Land via **Pull Request** into `main` (review + auto-merge). Never commit/push/merge onto local `main` to land.
4. After merge: delete the worktree and `gb/<slug>`.
5. Direct writes to `main` are **strictly prohibited**.

Worktree path **must** be inside this repo (`…/hush/worktrees/…`).  
Never `/opt/repo/worktrees`.

## Start new work (from main checkout only)

```bash
cd /opt/repo/hush
git checkout main
git pull --ff-only origin main
git status   # must be clean and on main

FEATURE_SLUG="short-slug"
BRANCH="gb/${FEATURE_SLUG}"
WT="worktrees/${FEATURE_SLUG}"

git worktree add -b "$BRANCH" "$WT"
cd "$WT"
pwd | grep -q '/hush/worktrees/' || { echo "FATAL: bad worktree path"; exit 1; }
test "$(git branch --show-current)" = "$BRANCH" || exit 1
```

## After every Milestone

```bash
git add .
git commit -m "Milestone X.Y: concise achievement"
git push -u origin HEAD
```

## Finish (PR — not local merge)

```bash
git push -u origin HEAD
BRANCH="$(git branch --show-current)"

gh pr create --base main --head "$BRANCH" --title "…" --body "…"
gh pr merge --auto --merge

# After GitHub shows MERGED:
cd /opt/repo/hush
git checkout main
git pull --ff-only origin main
SLUG="${BRANCH#gb/}"
git worktree remove "worktrees/${SLUG}"
git branch -d "$BRANCH" 2>/dev/null || true
git push origin --delete "$BRANCH" 2>/dev/null || true
git worktree list
```

## Forbidden

- `git commit` / `git push` on `main`
- `git merge` into local `main` to land a feature
- Worktrees outside `/opt/repo/hush/worktrees/`
- Leaving worktrees after merge

## Hooks

From main checkout: `./scripts/install-hooks.sh`  
Blocks commit and push while current branch is `main`.
