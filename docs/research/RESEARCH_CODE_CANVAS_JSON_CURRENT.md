# RESEARCH — JSON C0 escape, code canvas, Buzz origin note (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/code-canvas-json`
Branch: `gb/code-canvas-json`
Base: `main` `089dfa012` (post #80 exit-close-design merge; clean)

## Base State

PLAN_CODE_CANVAS_JSON.md is a verification/hygiene slice. The implementation is already present on this base from prior JSON/canvas/escape work:

**JSON escape (hush_json_escape):**
- hush_json.c fully maps C0: " \ → \" \\ ; \n \r \t → \n \r \t ; other <=0x1F → \u00HH (HUSH_JSON_U_LEN=6 guard).
- hush_launch_json_escape delegates to it (hush_launch.c:1597).
- test_json.c exercises ctrl case: "a\tb\r\u0001" → "a\\tb\\r\\u0001".
- Used on /api/events content, tags, and many other writers.

**Canvas + fenced code:**
- hush_http.c: /api/canvas route + hush_http_serve_canvas (project-scoped write only; refuses .. and missing project).
- hush-c/demo/index.html: #code-canvas pane, .code-block CSS (lang borders), splitFences/paintNote, canvas-file selector, Download/Save, POST /api/canvas.
- check_launch.sh exercises POST /api/canvas with project "alpha", asserts file written and content; also greps HTML for id="code-canvas", code-block, /api/canvas, splitFences, canvas-file.

**README Buzz origin:**
- "Hush began as a fork of [Buzz]... We do not track, fetch, or sync Buzz." (near top).
- IMPORT.md remains for one-way import notes.

**Docs:**
- UI_SPEC.md documents POST /api/canvas contract, .code-block painting, #code-canvas pane + selector + save.
- No new C modules; escape + canvas were landed earlier.

**Tests:**
- ./configure && make && make -C hush-c test → ALL PASS (includes test_json ctrl case + launch canvas POST test).
- check_launch.sh + check_exit.sh pass (canvas greps + routes).

## Verification Evidence (executed this worktree)

Commands:
- git checkout main && git pull --ff-only && git worktree add -b gb/code-canvas-json worktrees/code-canvas-json
- cd worktrees/code-canvas-json && ./configure && make clean && make && make test
- sh hush-c/tests/check_launch.sh (grep assertions for canvas ids/strings + live /api/canvas POST)
- rg -n "code-canvas|code-block|/api/canvas" hush-c/demo/index.html hush-c/src/hush_http.c
- rg -n "put_u|is_ctrl|\\\\t|\\\\r|\\\\u00" hush-c/src/hush_json.c
- rg -n "began as a fork|do not track|do not.*sync" README.md
- rg -n "canvas|escape.*C0|code-block" UI_SPEC.md
- python3 -c 'import json,urllib.request; ...' live TAB test (see below)
- git status clean on entry; base 089dfa012

Live TAB-in-content test (to prove /api/events is strict JSON and contains \t):
- Start relay on temp port with HUSH_CONFIG_DIR.
- POST /api/event with content containing literal TAB (day-of-week announcer style).
- GET /api/events; python3 -c 'import json,sys; json.loads(sys.stdin.read())' succeeds.
- The escaped body contains "\\t" for the TAB.

All DoD greps pass. No changes required.

## Differences from original PLAN base

- Current base is later. JSON C0 escape (RFC 8259), fenced .code-block rendering, #code-canvas right pane + project-scoped /api/canvas save, and README Buzz origin acknowledgement were implemented in prior canvas/JSON/escape slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of close-x-dialog, conv-intel-policy, pills-rail-voice-exit, provider-*, oauth-*, thread-*, onboard-*, etc.) to close PLAN_CODE_CANVAS_JSON.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria / DoD (M2.1–M5.1).
No code changes needed.
H1 (illegal C0 in events) fixed by hush_json_escape + 6-byte guard.
Canvas scoped to recorded project; fences at line start.
README states origin and no-sync.
Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed (this gate)
- make -C hush-c clean && make && make test (ALL PASS)
- sh hush-c/tests/check_launch.sh (canvas greps + live POST)
- rg/grep for code-canvas, code-block, /api/canvas, hush_json_escape ctrl paths, README Buzz sentence, UI_SPEC canvas contract
- Live TAB POST + strict json.loads verification
- Source + test + HTML + README inspection
