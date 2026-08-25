# RESEARCH — Canvas Fill-in-the-Middle (Current Base Audit)

Methodology: **RDAP** — verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/canvas-fim`
Branch: `gb/canvas-fim`
Base: `main` `818790f3c` (post #65 rail-prov merge)

## Base State

All DoD artifacts already present from prior implementation slices:

**Frontend (hush-c/demo/index.html):**
- `activePrediction`, `predictionPos`
- `CANVAS_FIM_MS = 300`
- `.tok-ghost`, `.fim-caret` CSS (var(--faint), var(--accent)/var(--accent-dim))
- `/api/complete` POST + polling GET
- `#code-canvas-edit` Tab handler calls `acceptCanvasFim()` when prediction set
- Debounce, prefix/suffix split, ghost paint, Esc cancel, accept on Tab

**Backend:**
- `hush-c/include/hush_canvas.h`: hush_canvas_init/shutdown/poll/start/take, HUSH_CANVAS_PRED_MAX=512
- `hush-c/src/hush_canvas.c`: POSIX spawn of grok for FIM prompt, token state machine, poll, take ≤512
- `hush_http.c`: POST /api/complete returns token immediately; GET /api/complete?t= returns pending/text/error
- Pump in hush_relay.c: hush_canvas_init at start, poll in loop, shutdown at exit
- No `#include <curl/curl.h>`, no `-lcurl`, no `pthread` added for canvas

**Tests:**
- `hush-c/tests/check_pwa.sh` greps: /api/complete, tok-ghost, fim-caret, CANVAS_FIM_MS
- `hush-c/tests/check_complete.sh`: POST returns token, GET eventually yields "int x;", no hive note inserted, no curl/pthread in sources
- `make -C hush-c test` → "complete FIM ok" + "ALL TESTS PASSED"

**Docs:**
- Original RESEARCH_CANVAS_FIM.md and PLAN_CANVAS_FIM.md exist
- UI_SPEC and README already reference Tab FIM, ghost, /api/complete

## Verification Evidence (executed in this worktree)

- `make -C hush-c clean && make -C hush-c` succeeds (includes hush_canvas.o)
- `make -C hush-c test` → ALL TESTS PASSED (complete FIM ok)
- `sh hush-c/tests/check_pwa.sh` → "PWA routes ok"
- `sh hush-c/tests/check_complete.sh` → "complete FIM ok"
- Explicit: no curl/pthread in canvas files
- Tab inserts prediction when active
- POST returns token without blocking; GET polls to text

## Differences from original PLAN base (f9e3b581e)

- Current base 818790f3c is later. Canvas FIM was implemented in an earlier slice and is already on main.
- This worktree performs research/audit gate + verification + hygiene to close PLAN_CANVAS_FIM.md (similar to rail-prov, pills-rail-voice-exit, thread-ux slices).

## Conclusion

Implementation on this base satisfies every Success/DoD item in PLAN_CANVAS_FIM.md.
No code changes required. H4 lock (POSIX spawn, non-blocking, no curl/pthread, separate slot) holds.
Proceed to verification commit + M5.1 PR lifecycle.

## Commands executed
- git worktree add -b gb/canvas-fim from clean main
- ./configure && make -C hush-c clean && make -C hush-c
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_pwa.sh
- sh hush-c/tests/check_complete.sh
- rg/grep for DoD strings in html + sources
- Verified no curl/pthread

## Next
- Write PLAN_CANVAS_FIM_VERIFIED.md
- Commit M1.1 + verification
- Push + PR + auto-merge + post-merge cleanup
