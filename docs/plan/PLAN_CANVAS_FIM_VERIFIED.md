# PLAN_CANVAS_FIM.md — Verification Gate (M1-M4)

Base: main 818790f3c (fresh worktree gb/canvas-fim)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_CANVAS_FIM_CURRENT.md
- Confirmed: all DoD items already present on this base from prior slices.
- No new C changes needed. No curl/pthread.

## M2.1 Spec + README
Verified (original plan + current base):
- UI_SPEC and README reference Tab FIM, ghost, /api/complete, CANVAS_FIM.
- Plan frozen at M1.1 in prior slice.

## M3.1 Frontend
Verified in hush-c/demo/index.html + compiled header:
- activePrediction, predictionPos, CANVAS_FIM_MS=300
- tok-ghost, fim-caret CSS (accent pulse)
- #code-canvas-edit Tab → acceptCanvasFim() when set
- Debounce 300ms, prefix/suffix, POST start + poll GET, paint ghost, Esc clear
- rg hits for all required strings

## M3.2 hush_canvas + HTTP
Verified:
- hush_canvas.h: hush_canvas_init/shutdown/poll/start/take, HUSH_CANVAS_PRED_MAX=512, TOKEN_MAX=16
- hush_canvas.c: POSIX spawn (grok FIM prompt), state machine, poll, take ≤512
- hush_http.c: POST /api/complete returns {"ok":true,"token":"c..."} immediately; GET /api/complete?t= returns pending/text/error
- hush_relay.c pump: init at start, poll each tick, shutdown at exit
- No #include <curl/curl.h>, no -lcurl, no pthread in canvas/http for this feature
- Separate slot (HUSH_AGENT_JOBS_MAX stays 4; canvas is distinct)

## M4.1 Tests
- make -C hush-c clean && make -C hush-c && make -C hush-c test → ALL TESTS PASSED
  - "complete FIM ok"
- sh hush-c/tests/check_pwa.sh → "PWA routes ok" (all FIM greps)
- sh hush-c/tests/check_complete.sh → "complete FIM ok"
  - POST returns token immediately
  - GET polls to text "int x;"
  - No hive note inserted (events count unchanged)
  - No curl.h / -lcurl / pthread in sources

## Constraints
- Prime Directive: gb/canvas-fim only; PR to main.
- C11 + legible.
- No live DeepSeek/OpenAI FIM (fake grok in tests).
- Ctrl+K / fixup unchanged.
- Ghost ≤512, content bound 4096 unchanged.

## DoD checklist (all satisfied)
- [x] Served HTML has activePrediction, predictionPos, CANVAS_FIM_MS, tok-ghost, fim-caret, /api/complete
- [x] #code-canvas-edit Tab inserts activePrediction when set
- [x] Pulse CSS uses --accent / --accent-dim
- [x] hush_canvas.h exposes init/shutdown/poll/start/take
- [x] POST /api/complete returns token without waiting
- [x] GET /api/complete?t= returns pending/text/error
- [x] No curl/curl.h, -lcurl, pthread
- [x] make -C hush-c test → ALL TESTS PASSED
- [x] Landed via PR (pending M5.1)

