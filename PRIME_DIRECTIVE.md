# Hush Prime Directive

**Status: MANDATORY for every human and every agent (including Goose).**  
**Violation is a process failure. Stop and correct before continuing.**

Hush is a **C11** project. It is not Buzz. Rust/desktop/mobile history is irrelevant.

---

## The rule (no exceptions)

1. **Always create a worktree** for any change (code, docs, config, hooks, version bumps).
2. **Commit and push only on the worktree branch** (`gb/<slug>`).
3. **Land on `main` only via Pull Request** (merge request): open PR → review → **auto-merge** (or explicit approve + merge on GitHub).
4. **After the PR is successfully merged into `main`**, delete the worktree and the local branch.
5. **Writing directly to `main` is strictly prohibited**  
   — no commits on `main`, no direct pushes to `main`, no local merge-into-main as a substitute for a PR.

There is no “small change” exception. There is no “docs only” exception. There is no “I’m the agent” exception.

---

## Canonical layout

| Item | Path / name |
|------|-------------|
| Main checkout | `/opt/repo/hush` (branch `main` only; keep clean) |
| Worktree path | `/opt/repo/hush/worktrees/<slug>` **only** |
| Branch name | `gb/<slug>` |
| Forbidden worktree roots | `/opt/repo/worktrees`, any path outside this repo |
| Remote | `https://github.com/coldcanuk/hush.git` |
| Long-lived branch | `main` only |

---

## Required workflow

### A. Start work (from clean main checkout)

```bash
cd /opt/repo/hush
git checkout main
git pull --ff-only origin main
git status   # MUST be clean; MUST be on main

FEATURE_SLUG="short-kebab-slug"    # e.g. prime-directive
BRANCH="gb/${FEATURE_SLUG}"
WT="worktrees/${FEATURE_SLUG}"

git worktree add -b "$BRANCH" "$WT"
cd "$WT"
pwd | grep -q '/hush/worktrees/' || { echo "FATAL: worktree outside repo"; exit 1; }
git branch --show-current          # must print gb/<slug>
```

### B. During work

```bash
# still inside worktrees/<slug>
git add …
git commit -m "…"
git push -u origin HEAD            # push the gb/* branch, never main
```

Commit after every milestone. Push the branch often.

### C. Finish — Pull Request only

```bash
# inside worktrees/<slug>
git push -u origin HEAD

gh pr create \
  --base main \
  --head "$BRANCH" \
  --title "…" \
  --body "…"

# Prefer auto-merge after checks/review:
gh pr merge --auto --merge
# Or: ensure review, then merge via GitHub UI / gh pr merge --merge
```

**Do not** run `git checkout main && git merge …` to land the branch.  
**Do not** `git push origin main` from a feature path.

### D. After PR is merged

```bash
cd /opt/repo/hush
git checkout main
git pull --ff-only origin main

git worktree remove "worktrees/${FEATURE_SLUG}"
git branch -d "gb/${FEATURE_SLUG}" 2>/dev/null || true
# remote gb/* branch: delete after merge if GitHub did not
git push origin --delete "gb/${FEATURE_SLUG}" 2>/dev/null || true

git worktree list   # only main checkout should remain
git status          # clean, on main
```

---

## Strict prohibitions

| Forbidden | Why |
|-----------|-----|
| `git commit` while on `main` | Direct write to main |
| `git push origin main` from feature work | Bypasses PR |
| `git merge` / `git merge --no-ff` into local `main` to land work | Bypasses PR review |
| Worktree under `/opt/repo/worktrees` or outside repo | Orphans; wrong project root |
| Reusing Buzz branches/tags or fetching all origin refs | Contaminates Hush history |
| Leaving worktrees/branches after merge | Repo rot |

---

## Enforcement

1. **Docs:** this file, `AGENTS.md`, `BRANCHING.md`, `.goose/skills/worktree/SKILL.md`.
2. **Hooks:** `scripts/install-hooks.sh` installs `pre-commit` + `pre-push` that **reject commits and pushes on `main`**.
3. **GitHub:** protect `main` (require PR, disallow direct push) when admin access allows.
4. **Origin fetch:** main-only (`remote.origin.fetch` + `tagopt=--no-tags`).

Install hooks once per clone:

```bash
cd /opt/repo/hush
./scripts/install-hooks.sh
```

---

## Goose checklist (every task)

- [ ] Am I inside `…/hush/worktrees/<slug>` on `gb/<slug>`?
- [ ] Did I commit **and** push the branch (not main)?
- [ ] Did I open a PR into `main` (not merge locally)?
- [ ] After merge: did I remove the worktree and delete `gb/<slug>`?
- [ ] Is main checkout clean and not holding my edits?

If any answer is no → stop and fix process before more code.
