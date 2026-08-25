# RESEARCH — One joke, full-note snip (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/one-joke-snip`
Branch: `gb/one-joke-snip`
Base: `main` `e9abfd7b4` (post #74 onboard-profile-agents)

## Base State

PLAN_ONE_JOKE_SNIP.md is a small focused slice. The implementation is already present on this base from prior agent/hygiene work:

**Flattened snip:**
- `hush_agent_snip_line` collapses whitespace (space/tab/CR/LF) to single space, keeps ~160 visible chars.
- Multi-line prior notes (e.g. "Byte me.\n\tgo: fmt") become "Byte me. go: fmt" in the `-p` transcript.

**Hygiene:**
- System prompt override includes: "If the last human ask is a joke, reply with exactly one joke."
- Ensures follow-up "tell me another" does not repeat prior joke in the thread.

**--no-memory:**
- `HUSH_AGENT_GROK_NOMEM "--no-memory"` passed in `hush_agent_exec_grok`.
- Prevents memory bleed across jokes.

**Tests:**
- `check_agent.sh`:
  - Fake grok logs `-p` to `$HUSH_CONFIG_DIR/grok-p.log`.
  - After follow-up, asserts `grep -q 'Byte me. go: fmt' .../grok-p.log` (flattened second line).
  - Greps for `--no-memory` and the one-joke sentence.
- `make -C hush-c test` → ALL PASS ("agent mention reply ok").

**Spec:**
- UI_SPEC §13 documents snip flattening, exactly one joke, `--no-memory`.

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- sh hush-c/tests/check_agent.sh → "agent mention reply ok" (includes flattened follow-up + hygiene greps)
- Explicit source greps:
  - hush_agent_snip_line present and called on content
  - "exactly one joke" sentence in rules/hygiene
  - `--no-memory` define and argv
- UI_SPEC has the required phrases
- No new C required; this is verification + hygiene close-out

## Differences from original PLAN base

- Current base is later. The snip flattening, one-joke hygiene, --no-memory, and check_agent assertions for flattened transcript were implemented in earlier agent/mention/hygiene slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of rail-prov, canvas-fim, provider-*, oauth-*, thread-*, onboard-*, splash-* slices) to close PLAN_ONE_JOKE_SNIP.md per user directive.

## Conclusion

Implementation satisfies every item in the plan.
No code changes needed.
H4 lock (flatten to single space, one joke when asked, --no-memory, test asserts flattened line) holds.
Proceed to VERIFIED.md + commit(s) + full PR lifecycle.

## Commands executed
- git worktree add -b gb/one-joke-snip from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_agent.sh (passes, flattened "Byte me. go: fmt")
- rg/grep for snip_line, "exactly one joke", --no-memory, grok-p.log, UI_SPEC phrases
- Source + test inspection
