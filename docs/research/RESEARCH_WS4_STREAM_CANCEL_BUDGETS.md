# RESEARCH — WS4 stream / cancel / budgets product journey

Date: 2026-09-23. Branch: `gb/ws4-stream-cancel-budgets`. Base: `b1f0747a`.
Labels: OBSERVED = read in tree. INFERRED = reasoned. UNKNOWN = not verified.

## OBSERVED — server already owns the primitives

- `POST /api/reply {root, robot}` → `{ok:true,running:true,text}` live
  partial via `hush_agent_partial()` (`hush-c/src/api_canvas.c:312`);
  idle returns `{ok:true,running:false,text:""}`; missing fields → 400.
- `POST /api/cancel {root, robot}` → `hush_agent_cancel()` sets
  `cancelled`, `kill_deadline = now + HUSH_AGENT_CANCEL_GRACE_S`,
  SIGTERM to the job process group immediately
  (`hush-c/src/hush_agent.c:823`); the poll pump SIGKILLs past the grace
  period and posts "<name> stopped on request before finishing. …"
  (`hush-c/src/agent_dispatch.c:231`). Selector matches parent id plus
  robot hex pubkey OR robot name (`hush_agent_job_matches`).
- `GET /api/status.thinking[]` entries are
  `{name,parent,slug,provider,stage}` (`hush_agent.c:443`).
- Both APIs are integration-tested: `check_cancel` and `check_streaming`
  in `hush-c/tests/check_collaboration.py`. They pass on main.
- Budgets enforced server-side with honest in-thread notes (all kind 1,
  `e` = root, so they already render inside the thread):
  - per-robot provider budget 60/min burst 20 → "Rate-limited. This robot
    needs a short break." (`hush_intel.c:574`).
  - per-channel `max_jobs` → "Holding. This channel is at its job cap.";
    holds full → "Holding. Too many live conversations…";
    cooldown (robot chains) → "Cooling down. …".
  - global job slots full → "<name> could not start a turn: every job
    slot is busy. …" (`hush_agent.c:637`).
  - no ready provider → "No selected provider is ready for <name>. …".
  - provider failure → "<name> did not return a usable reply through
    <provider> (<reason>). …".
- Manage Channel UI already exposes the enforceable leash: kind, reply,
  talk, burst, max_jobs, cooldown, turns (`index.html` manage-save).
- `GET /api/chan-events` (`hush_cevent` JSON feed) exists; the demo UI
  never polls it.

## OBSERVED — product-journey gaps (all in `hush-c/demo/index.html`)

1. The UI never calls `/api/reply`: no live partial painting anywhere.
   `tick()` polls `/api/status` + `/api/events` + `/api/session` +
   `/api/presence` only (one call site).
2. The UI never calls `/api/cancel`: no Stop control on either thinking
   chip (`paintThink` renders a pulsing dot + label only; shared by the
   channel root and `#thread-think`).
3. Refusal notes render as plain robot notes: nothing marks a leash
   refusal, a stop, or a failure as distinct from a real answer.
4. `UI_SPEC.md` §13 describes thinking chips with no Stop and no partial;
   §20 describes the leash notes but no visual distinction.

## INFERRED — smallest journey-complete diff (no C needed)

- Server primitives are complete and tested; WS4 is a UI + spec + test
  slice: Stop button on the shared thinking chip, `/api/reply` partial
  polling painted live in channel roots and the open thread, leash-note
  styling via a documented server-string marker list, UI_SPEC deltas.
- `hush_cevent` surfacing stays OUT: optional per the task and not needed
  to close act → observe → steer.

## UNKNOWN

- Whether Playwright/Chromium is available here for a live browser proof
  of the new UI (the repo's `check_collaboration_ui.cjs` is NOT in
  `make test`; verify by running or fall back to static wiring checks).
