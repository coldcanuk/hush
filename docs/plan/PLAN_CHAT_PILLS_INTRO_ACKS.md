# PLAN: in-sentence mention pills, one intro, dest-log isolation, 2s ack skip

Branch: `gb/chat-pills-intro-acks`
Worktree: `worktrees/chat-pills-intro-acks`
Base: `main` `fcb44b705`

Follow-up to PR #99. Land via PR only.

## Scope

**Primary.** G1–G5 from the post-#99 audit:

1. `paintNote` must not run `prettyMentions` before `renderPreservingMentions`.
   Stored `nostr:` tokens become in-sentence pills.
2. Developer Logging never un-hides log notes in the stream. `visibleNotes`
   always drops `isDevLogNote`.
3. `isDevLogNote` is `"Mention received."` (and similar receipts) only.
   `"At ease…"` intros are chat.
4. C posts **one** intro per `(robot hex, thread root)` even when grok starts
   and even when dest log is off.
5. Ack gradient skips to emoji when the parent note is older than 2s
   (unless a live thinking job exists). Stream/thread keys include an ack
   stamp so the 1s tick can advance thinking → reacting → emoji.

**Non-goals.** Raylib; 360-rail; deleting the stored `"Mention received."`
protocol note; structured co-mention vote protocol.

## Architecture (frozen)

- Storage path stays `assembleMentionContent` (in-place `nostr:`).
- Render path: `splitFences(raw content)` → `renderPreservingMentions`.
- Logs: stream always filtered; drawer is the only log view.
- Intro: `hush_agent_on_deck` before grok start; table already present.
- Ack age: `created_at` is unix seconds (same as `ago()`).

## Phases

### Phase 0 COMPLETE
Worktree `worktrees/chat-pills-intro-acks`.

### Phase 1 / 2
This file. No JPEG re-research.

### Phase 3 Implementation
JS paint + C intro gate + tests + embed.

### Phase 4 Verify
`make test`; mention-order capture; HTML greps that fail the old paint path.
