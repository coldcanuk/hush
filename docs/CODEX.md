# Codex for Hush

Codex is Hush's development agent. Read [AGENTS.md](../AGENTS.md) and
[PRIME_DIRECTIVE.md](../PRIME_DIRECTIVE.md) before changing the repository.
Create a `worktrees/<slug>` checkout on `gb/<slug>`, commit and push there,
land through PR review and GitHub merge, then remove the merged worktree and
branch. Never commit, push, or merge directly into local main.

## C skill

The repository carries the Codex port of
[write-legible-c](../.agents/skills/write-legible-c/SKILL.md), including the
complete normative reference and upstream MIT license. Codex discovers
`.agents/skills` from the working directory up to the repository root.
Invoke `$write-legible-c` explicitly for C tasks; spawned development agents
must read the same skill and follow its verification checklist.

For an account-wide installation, copy the complete `write-legible-c`
directory into `~/.agents/skills/`. Restart Codex if it does not discover an
updated skill. No Grok plugin manager is needed.

## Runtime

Select Codex in a robot's ranked providers and authenticate with `codex login`.
Hush invokes `codex exec` for ordinary mention replies, using its isolated
working directory and a read-only sandbox. Before spawning, it links the full
C skill under that directory's `.agents/skills/write-legible-c`. Source builds
use the repository copy; installed builds fall back to
`share/hush/codex/skills/write-legible-c`. `HUSH_CODEX_SKILL_DIR` explicitly
overrides the source. Missing files or a conflicting link fail dispatch. Planning, election, and fixup jobs
continue to use the Grok runner. These runtime choices do not change the
repository's Codex development policy.

Saved `agy` selections are converted to `codex` on restore. New API requests
must use `codex`; the retired id has no registry entry, executable, or UI option.
Log into Codex separately; credentials are not migrated. The existing opt-in
update scanner includes `codex update` when `HUSH_AUTO_UPDATE` is enabled.

## Build and validation

```sh
./configure
make
make test
```

Integration tests require `curl` and `python3`.

Skills for builds, tests, worktrees, relay operation, and publishing are in
`.agents/skills/`. Use available Codex image tools for image inspection;
there is no repository-wide restriction tied to an older agent's model.
