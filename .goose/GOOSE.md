# Goose for Hush

Hush is optimized exclusively for the Goose AI agent.

## Prime Directives
- All development happens in worktrees created inside the repository (worktrees/<slug>).
- From clean `main`: create worktree + branch `gb/<slug>`, implement, commit after every Milestone, push branch, return to main, merge --no-ff, delete worktree and (optionally) branch.
- No orphaned worktrees or branches are kept. Once a worktree serves its purpose, it is removed.
- `main` is protected: direct pushes discouraged; all changes via reviewed worktree merges.
- Goose is the only supported agent. Support for Claude, Codex, and other agents is deprecated.

## Skills
Skills live in `.goose/skills/<name>/SKILL.md` (or with runner scripts).

Core skills for Hush (C11 + Goose):
- worktree (lifecycle commands)
- c-build, c-test
- legible-c (apply write-legible-c + c-standard §14 checklist)
- relay (run hush-relay + raw Nostr line protocol tests)
- goose-init (bootstrap .goose layout)

## Build
```bash
./configure
make
make test
```

See AGENTS.md (short Hush version) and docs/research/RESEARCH.md.

## Vision & Images (CRITICAL)
Your primary model (DeepSeek) DOES NOT support images natively. 
**DO NOT** use the native `read_image` tool and **DO NOT** attempt to attach images directly to the chat context. 
If you need to view or analyze a screenshot or image, you **MUST** run the shell command:
`python3 .goose/skills/vision/vision_tool.py /path/to/image.png "Your query here"`
This will delegate the image analysis to the actVite provider and return text.
