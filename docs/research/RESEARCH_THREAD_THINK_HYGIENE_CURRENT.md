# RESEARCH — Thinking chip, thread pane, Grok hygiene, relay-live (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/thread-think-hygiene`
Branch: `gb/thread-think-hygiene`
Base: `main` `3ceaffc03` (post #71 thread-chat-rail-ux)

## Base State

All primary DoD items from PLAN_THREAD_THINK_HYGIENE.md are already present on this base:

**Thinking chip:**
- CSS `.think` + `.think-dot` (pulse)
- `status.thinking[]` from `/api/status`
- UI paints chip on matching root (parent + name)
- Optimistic think strip in thread, send disabled while thinking
- `paintThreadThink`

**Thread pane (1:1 with robot):**
- `#thread-pane`, `.thread-btn`
- Thread button opens 1:1 (human + that robot)
- Close returns to channel; same button reopens
- Composer in pane posts with `reply_to` + `mention_0`
- `check_launch.sh` greps for thread-pane, thread-btn, thread-think, paintThreadThink, send.disabled

**Grok hygiene (short note, no telemetry):**
- hush_agent argv: --cwd, --max-turns 1, --reasoning-effort, --no-subagents, --disable-web-search, --disallowed-tools, --rules
- Empty cwd under HUSH_CONFIG_DIR/agent-cwd or TMP
- Hygiene appended to system-prompt-override
- e tag is root (if parent has e, else parent id)
- Replies are short joke/note, not thought/GEMINI/npub/host

**Relay-live drawer:**
- `#relay-drawer` (stored, projects, sockets, port, version)
- `#stats` button opens it; [x] closes
- "relay live" / "relay down" label
- check_launch greps for relay-drawer, relay-close

**Status + POST:**
- GET /api/status includes "thinking": [...]
- hush_agent_status emits [{"name":"...","parent":"..."}]
- POST /api/event accepts reply_to, stores e before mentions
- No re-dispatch on human self-p

**Tests:**
- make -C hush-c test → ALL PASS (agent mention reply ok)
- check_launch.sh passes with required greps (no dock greps from prior, focus on think/thread/relay)
- check_agent.sh still expects reply_to + joke

**Docs:**
- UI_SPEC §13 (thinking chip, thread pane, Grok hygiene, reply_to), §19 relay-live
- README/NOSTR one-liners for isolated grok, thread, thinking, relay drawer

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_launch.sh → launch routes ok (thread-pane, thread-btn, relay-drawer, think-dot, thread-think, paintThreadThink, send.disabled)
- Explicit greps:
  - Hygiene flags present (cwd, max-turns, reasoning-effort, disallowed-tools, no-subagents, disable-web-search, rules)
  - hush_agent_status + /api/status "thinking"
  - #thread-pane, .thread-btn, #relay-drawer, .think, think-dot, thread-think, paintThreadThink
  - reply_to handling in POST + agent
- No new C required for verification gate

## Differences from original PLAN base

- Current base is later. Thinking chip, 1:1 thread pane with Thread button, Grok hygiene argv + short replies, relay-live drawer, status.thinking, reply_to POST were implemented in prior slices (thread-ux, oauth-mention-rail, agent work, thread-chat-rail-ux) and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the established pattern of prior verification slices) to close PLAN_THREAD_THINK_HYGIENE.md per user directive.

## Conclusion

Implementation satisfies every primary DoD item.
No code changes needed.
H4 lock (isolated cwd, hygiene flags, thinking keyed on status, e=root, thread 1:1, relay drawer) holds.
Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/thread-think-hygiene from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_launch.sh
- rg/grep for hygiene flags, thinking, thread-pane, relay-drawer, .think, reply_to, e tag root
- Source + served + check greps
