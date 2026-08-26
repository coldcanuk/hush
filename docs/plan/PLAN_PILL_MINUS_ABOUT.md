# PLAN: pill minus strips @Name; manage about absent is no-op

Branch: `gb/pill-minus-about-wipe`
Worktree: `worktrees/pill-minus-about-wipe`
Base: `main` `9508623d8`

Skeptic follow-up to PR #100.

## Scope

1. Composer/thread pill minus deletes `@Name` from the textarea (all
   occurrences). Submit must not still assemble that mention.
2. Drop prepend-era Backspace-at-0 pill pop on composer and thread.
3. `hush_http_channel_manage` writes `about` only when the JSON body
   contains the key. Empty string may clear. Absent is a no-op so
   `seedTeam` and roster-only manage POSTs do not wipe topics.

## Tests (must fail the old paths)

- HTML: minus handler calls `dropMentionFromInput`; no `composerPills.pop()`
  / `threadPills.pop()` on Backspace-at-0.
- HTTP: set channel about, manage without `about`, session still has it.
