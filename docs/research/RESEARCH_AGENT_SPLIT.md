# RESEARCH: splitting hush_agent.c into legible modules

**Scope.** P1 slice 5: decompose `hush_agent.c` (4,524 lines) following the
reviewed decomposition. Unlike `hush_http.c` (family-ordered), the agent's
definition order interleaves clusters, so this split is tranche-based: move
only genuinely contiguous, self-contained blocks; document the rest.
**Baseline.** `main` @ `b074c1f58` (HTTP split merged, PR #165); suite green.
Worktree `worktrees/agent-split`, branch `gb/agent-split`.

## (a) Verified structure

Types (lines 171-339): `hush_agent_job_t`, `hush_agent_follow_t`,
`hush_agent_mode_t`, `hush_agent_assign_t`, `hush_agent_robot_t`,
`hush_agent_note_in_t`, `hush_agent_thread_walk_t`,
`hush_agent_job_in_t`, `hush_agent_alias_t`, `hush_agent_alias_set_t`,
`hush_agent_mentions_t`, `hush_agent_context_line_t`.
Statics: `g_jobs[4]`, `g_follow[]`, `g_id_seq`, plus job/note/emission
globals.
Public API (hush_agent.h, unchanged): init, shutdown, reset_follow, mention,
on_posted, poll, status, channel_busy, jobs_active, cancel, partial,
start_fixup, take_fixup.

The prototype block (326-664) is grouped by concept, but definitions scatter:
text munging spans 1393-3037+, thread/context sits at 1517-1749, prompts at
1041/1754/1898, process execution at 1919-2323, follow/flow at 3904-4505.

## (b) Contiguous clusters (verified line ranges)

| Block | Range | Functions | Target module |
|---|---|---|---|
| provider process execution | 1919-2323 | prepare/wait worker, capture family, run_worker, exec_child + exec_api/cline/grok/copilot/codex/goose/ollama, build_combined, spawn_grok | `agent_process.c` |
| thread walking + context | 1517-1749 | thread_skip, walk_thread, collect_thread, push_thread, append_line, append_durable, fill_thread, is_markdown, append_context | `agent_thread.c` |

Both blocks touch only the shared types plus a handful of core helpers
(`hush_agent_copy/trim`, `hush_agent_prepare_cwd`, `hush_agent_event_channel`,
`hush_agent_event_root`, `hush_agent_human_name`), which the internal
header will export.

## (c) Plan

1. **M5.2** — `hush_agent_internal.h`: all types verbatim + the shared
   helper declarations; core de-statics the helpers. No moves. Suite green.
2. **M5.3** — `agent_process.c` (1919-2323); export the entries the core
   calls (compile loop reveals them: spawn_grok, capture_reply, ...).
3. **M5.4** — `agent_thread.c` (1517-1749); export `fill_thread` etc.
4. **M5.5** — docs + land. Remaining clusters (text munging, prompts,
   dispatch/follow, fixup) documented as the follow-on tranche.

## (d) Final state (tranche 2 landed)

| Module | Lines | Owns |
|---|---|---|
| `hush_agent.c` | 864 | public API, job table, notes, runtime readiness, cancel/partial, grok start |
| `agent_dispatch.c` | 1,591 | job lifecycle, dispatch/follow flow, election/planning waves, follow table |
| `agent_process.c` | 454 | worker exec, capture, provider CLIs, spawn |
| `agent_thread.c` | 254 | thread walking, transcript + context assembly |
| `agent_text.c` | 722 | text utilities, mention rewrite, alias mapping, reply scrub |
| `agent_prompt.c` | 338 | prompt/directive builders, instruction/guidance appends, peer rules |
| `agent_fixup.c` | 122 | fixup token mint, fixup prompt fill, start/take fixup |
| `hush_agent_internal.h` | — | all types/constants + shared helper surface |

Tranche 2 moved the remaining clusters: dispatch/follow flow
(M6.1, 2185→EOF plus the follow table), text munging (M6.2, two blocks),
prompt builders (M6.3, three blocks), and the fixup pass (M6.4). The core
shrank from 4,524 lines to 864. Cross-module entry points and shared prompt
strings live in `hush_agent_internal.h`; module-local constants follow
their only users. Behavior preserved: every milestone kept the full suite
green, and the source-grep tests were retargeted to the new module files.

## (e) Risks

1. **Interleaved definitions** — mitigated by moving only the two verified
   contiguous blocks; the compile loop catches every cross-reference.
2. **Job-table internals** — `g_jobs`/struct access stays in the core;
   process/thread blocks receive `hush_agent_job_t *` pointers only.
3. **Provider forks** — check_agent.sh/check_provider.sh exercise the real
   exec paths; the suite is the gate.
