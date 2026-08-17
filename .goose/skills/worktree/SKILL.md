---
name: worktree
description: "Mandatory worktree lifecycle for all Hush development. Create inside repo, commit per milestone, merge to main, delete wt."
---
# Worktree (Prime Directive)

## Start new work (from main worktree)
git checkout main
git pull --ff-only || true
git status   # must be clean
FEATURE_SLUG="short-slug"
BRANCH="gb/${FEATURE_SLUG}"
WT="worktrees/${FEATURE_SLUG}"
git worktree add -b "$BRANCH" "$WT"
cd "$WT"
pwd && git branch --show-current

## After every Milestone
git add .
git commit -m "Milestone X.Y: concise achievement"

## Finish
git add .
git commit -m "Complete: summary"
git push -u origin "$BRANCH"
cd /path/to/main
git checkout main
git pull
git merge --no-ff "$BRANCH" -m "Merge '$BRANCH' — summary"
git push origin main
git worktree remove "$WT"
# git branch -d "$BRANCH"  (optional)
