# Desk gaps

Close the gaps that stop a bound project job, and correct the desk
docs, the typewriter face, and the header mark. Land on `gb/desk-gaps`
from `worktrees/desk-gaps`. One pull request into `main`.

## Out of scope

Inventory, skill gems, and the character sheet stay. The hive is not
rebuilt as a thread sidebar. A guest Yes does not approve a robot turn.
Unbound jobs do not gain a shell, the web, or subagents. The 8-human,
8-robot, and 16-client caps stay. The Volume dial caption stays. Codex
stays read-only. Canvas grok stays on its own one-turn argv. Issue #209
is not closed from this tree.

## Milestones

### M1. Worker directory

Goose has no directory flag. The forked child calls `hush_agent_enter_cwd`
before `goose run` when `job->cwd` is non-empty. An empty path is refused
and the relay process does not change directory.

Copilot non-interactive mode needs `--allow-all-tools`. `--allow-all`
also opens every path and URL, so the argv drops it. `-C` is "change
working directory before doing anything else" and is added only when
`job->cwd` is non-empty.

Verify: `hush-c/tests/test_team` (`test_worker_cwd`).

### M2. Project budget

A job with `project_tools` set uses Grok `--max-turns 8` and a 300 second
kill clock (`HUSH_AGENT_PROJECT_TIMEOUT_S`). Other jobs stay at 2 turns
and 90 seconds. A fixup stays at 1 turn.

`hush_wake_in_t.lease_s` of 0 keeps the 90 second lease. A project claim
passes 300. The claim slot and the kind-1039 `lease` tag use that same
span.

While the child pid is live and the kill clock has not passed, the relay
does not mark the robot Stuck and does not post the Stuck nudge. The
timeout still kills the process group.

Verify: `hush-c/tests/test_team` (`test_project_clock`) and
`hush-c/tests/test_wake` (lease tag 300, still claimed at 91 seconds,
done at 301 seconds).

### M3. Docs

README names milestones, hop, the chief of staff, and the project-bound
budget. UI_SPEC §13 describes the private chat directory and the project
file-tool unlock. ROBOT_TO_ROBOT.md says whitespace around the
`approval_mode` colon is accepted, and a bad value is still refused.

### M4. Offline face and ink mark

Special Elite latin-400 is served from the relay as
`/fonts/special-elite-latin-400.woff2` (Apache-2.0, fontsource 5.2.8),
linked with `ld -r -b binary` the same way as the icon panels. The page
drops the Google stylesheet and keeps the Courier Prime, VT323, and
monospace fallback.

The header mark is a square plate and a letter H in `currentColor`.
It does not use `#064e3b` or `#34d399`.

Verify: the embedded page names the local face and does not name
`fonts.googleapis.com`, and the header mark does not contain `064e3b`.
