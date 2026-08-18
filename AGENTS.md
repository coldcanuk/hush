# AGENTS.md — Goose + Hush (C11)

Hush is a legible C11 Nostr relay core, optimized exclusively for the Goose agent.

## Prime Directive: Worktrees Only
- All work starts from clean `main`.
- Create worktree **inside this repository only** (`/opt/repo/hush/worktrees/...`).
  Never use `/opt/repo/worktrees` or any path outside the Hush repo.
  ```
  git checkout main && git pull --ff-only || true
  git status   # must be clean
  FEATURE_SLUG="hush-feature"
  git worktree add -b "gb/${FEATURE_SLUG}" "worktrees/${FEATURE_SLUG}"
  cd "worktrees/${FEATURE_SLUG}"
  ```
- Commit after **every Milestone**: `git add . && git commit -m "Milestone X.Y: ..."`
- Finish: push branch, return to main, `git merge --no-ff`, push, `git worktree remove ...`, delete branch if desired.
- No orphaned worktrees or branches. Delete when done.
- `main` is protected: no direct pushes for features. Use worktree merges only.
- Product version is `0.0.1` (see top-level `VERSION`).

## Goose is the Only Agent
- `.goose/` is the canonical location for skills and configuration.
- `.agents/`, `.claude/`, `.codex/` and similar are deprecated and removed.
- Skills: `.goose/skills/<name>/SKILL.md` (with optional runner scripts).
- Core skills: worktree, c-build, c-test, legible-c, relay, goose-init.

## C11 + write-legible-c
- Every .c/.h follows the machine-legibility standard (see write-legible-c skill + c-standard.md).
- Strict build: `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`.
- No Rust, no Cargo, no crates, no desktop/mobile/web legacy.

## Build & Test
```
./configure
make
make test
```

## Research → Plan → Build
Follow RDAP (research first, synthesis gate at end of Phase 1, small atomic Milestones, commit per M).

See:
- README.md
- RESEARCH.md (history + current plan)
- CODE_OF_CONDUCT.md (SQLite Code of Ethics / Rule of St. Benedict)
- hush-c/ for the implementation

