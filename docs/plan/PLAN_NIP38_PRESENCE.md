# PLAN: NIP-38 presence line + trail kind + STUCK→Major (frozen)

**Branch:** `gb/nip38-presence`  
**Worktree:** `worktrees/nip38-presence`  
**Gate:** `docs/research/RESEARCH_NIP38_PRESENCE.md`  
**Land:** PR to main only. No local merge. Worktree path inside this repo.

## Scope

Primary: robots and humans advertise a NIP-38 (`30315`) presence **line** per job; append a Hush trail kind `1038`; cevent + developer log; Stuck keep-alive pings Major; private vibe does not leak on `REQ`.

Non-goals: HTTP webhooks, 44102, chaperon status, schnorr, replacing `thinking[]`.

Success: `make test` green; unit tests for slugs, replace, skip chaperon, private omit, stuck keep-alive; UI hides 30315/1038 from chat; `/api/presence` lists lines; two launches.

## Remaining

M2.1 `hush_presence` unit + store addressable replace + tests.  
M3.1 Agent job hooks (Working / beat / stall Stuck / finish Idle) + Major nudge + disable clear.  
M3.2 HTTP `GET/POST /api/presence`, skip trail kinds in chat JSON, REQ privacy.  
M3.3 SPA: complement chips, filter kinds, tick `/api/presence`.  
M4.1 `check_launch` greps, `make test`, two launches, PR, delete worktree after merge.

## Do not

Write `main`. Infer offline from missing 30315. Publish as chaperon. Merge #52 leftovers.
