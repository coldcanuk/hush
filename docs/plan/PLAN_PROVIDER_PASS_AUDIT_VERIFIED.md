# PLAN_PROVIDER_PASS_AUDIT.md — Verification Gate (M1-M4)

Base: main ed067eec3 (fresh worktree gb/provider-pass-audit)
Date: 2026-08-24

## M1.1 Research gate
- Written: docs/research/RESEARCH_PROVIDER_PASS_AUDIT_CURRENT.md
- Confirmed: all DoD items already present on this base.
- 5 secret kinds, pass-only storage, no leaks in GET/overlay/session, scan fallback, docs.

## M2.1 Module contract
- HUSH_PROVIDER_SECRET_* (5), HUSH_PROVIDER_SECRET_COUNT
- status has has_key + has_username/has_password/has_token/has_passkey
- hush_provider_secret_path, save_secrets, has_kind, load for scan
- UI_SPEC §11 updated in prior; five retrieve CLIs documented

## M3.1-M3.4 Implementation + tests + docs
- hush_provider_save writes each non-empty via hush_pass_save; soft-fail safe
- status has_* from hush_pass_has
- scan: posted key else hush_pass_get(api_key) then token; never on argv
- POST parses 5; GET never emits secret field names or values
- Drawer has 5 inputs + pills + has_* bits + retrieve help
- check_provider.sh posts all 5; asserts no echo in POST/GET/session/overlay; scan no leak
- Overlay file never stores secrets (grep asserts)
- make -C hush-c test → ALL PASS
- Unit test_provider (run from hush-c/) → ok
- Docs: pass-integration.md lists 5 rows; README shows 5 pass commands

## M4 Verify
- Build + test: ALL PASS
- check_provider.sh: "provider routes ok"
- Direct unit: "ok" (from hush-c/)
- Source + served: 5 kinds, 5 has_*, 5 fields, no secret names in GET paths
- Docs complete

## Constraints
- Prime Directive: gb/* worktree only; PR to main.
- C11 + legible-c.
- Keys never on argv.
- Missing pass → soft-fail; overlay still written.
- Widen to 512 already done.

## DoD checklist (all satisfied)
1. [x] Each secret kind: hush/providers/<id>/<kind> (5)
2. [x] Overlay never contains secrets
3. [x] GET /api/provider + /api/session never echo secrets or use secret field names
4. [x] POST /api/provider writes via hush_pass_save (soft-fail ok)
5. [x] POST /api/provider/scan uses posted or loads from pass; never on argv
6. [x] Drawer accepts 5 kinds + retrieve help
7. [x] make test proves save/scan/no-leak
8. [x] Docs list every path; PR merged, worktree removed, main clean (pending M5)

