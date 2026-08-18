# AGENTS.md — Goose + Hush (C11)

Hush is a legible C11 Nostr relay core, optimized exclusively for the Goose agent.

**Read [PRIME_DIRECTIVE.md](PRIME_DIRECTIVE.md) first. It overrides every other workflow note.**

## Prime Directive (absolute)

1. **Always** create a worktree under `worktrees/<slug>` on branch `gb/<slug>`.
2. **Commit and push** only on that worktree branch.
3. Land on `main` **only** with a **Pull Request** → review → **auto-merge** (or approved GitHub merge).
4. After the PR is **merged**, **delete** the worktree (and the `gb/*` branch).
5. **Writing directly to `main` is strictly prohibited** (no commits, no pushes, no local merge-as-land).

No exceptions for docs, hooks, “tiny fixes,” or agent convenience.

### Start

```bash
cd /opt/repo/hush
git checkout main && git pull --ff-only origin main
git status   # clean + on main
FEATURE_SLUG="short-slug"
git worktree add -b "gb/${FEATURE_SLUG}" "worktrees/${FEATURE_SLUG}"
cd "worktrees/${FEATURE_SLUG}"
```

Worktrees live **inside this repo only**. Never `/opt/repo/worktrees` or paths outside Hush.

### During

```bash
git add . && git commit -m "…"
git push -u origin HEAD
```

### Finish

```bash
git push -u origin HEAD
gh pr create --base main --head "gb/${FEATURE_SLUG}" --title "…" --body "…"
gh pr merge --auto --merge
# wait until merged, then:
cd /opt/repo/hush && git pull --ff-only origin main
git worktree remove "worktrees/${FEATURE_SLUG}"
git branch -d "gb/${FEATURE_SLUG}" 2>/dev/null || true
git push origin --delete "gb/${FEATURE_SLUG}" 2>/dev/null || true
```

See also: [BRANCHING.md](BRANCHING.md), [.goose/skills/worktree/SKILL.md](.goose/skills/worktree/SKILL.md).  
Install local guards: `./scripts/install-hooks.sh`.

Product version: see top-level `VERSION` (currently `0.0.1`).

## Goose is the Only Agent

- `.goose/` is the canonical location for skills and configuration.
- `.agents/`, `.claude/`, `.codex/` and similar are deprecated and removed.
- Skills: `.goose/skills/<name>/SKILL.md` (with optional runner scripts).
- Core skills: worktree, c-build, c-test, legible-c, relay, goose-init, publish.

## C11 + write-legible-c

- Every `.c`/`.h` follows the machine-legibility standard (write-legible-c skill).
- Strict build: `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`.
- No Rust, no Cargo, no crates, no desktop/mobile/web legacy.

## Build & Test

```bash
./configure
make
make test
```

## Research → Plan → Build

Follow RDAP (research first, synthesis gate at end of Phase 1, small atomic Milestones, commit per M **on the worktree branch**).

See:

- [PRIME_DIRECTIVE.md](PRIME_DIRECTIVE.md)
- [README.md](README.md)
- [RESEARCH.md](RESEARCH.md)
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) (SQLite Code of Ethics / Rule of St. Benedict)
- `hush-c/` for the implementation
