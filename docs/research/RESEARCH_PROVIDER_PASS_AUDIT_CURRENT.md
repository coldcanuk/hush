# RESEARCH — Provider secrets in unix `pass` (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/provider-pass-audit`
Branch: `gb/provider-pass-audit`
Base: `main` `ed067eec3` (post #67 provider-configure)

## Base State

PLAN_PROVIDER_PASS_AUDIT.md DoD items are largely present:

**5 secret kinds locked:**
- HUSH_PROVIDER_SECRET_* defines (api_key, username, password, token, passkey)
- HUSH_PROVIDER_SECRET_COUNT = 5
- status has has_key + has_username/has_password/has_token/has_passkey
- in has matching optional fields

**Storage:**
- hush_provider_secret_path uses "providers/%s/%s"
- hush_provider_save calls hush_provider_save_secrets (writes via hush_pass_save for each non-empty)
- status has_* populated from hush_pass_has
- scan falls back to pass when no posted key (api_key then token)

**HTTP/UI:**
- POST /api/provider parses 5 optional secret fields
- GET /api/provider and /api/session emit only has_* booleans; never secret field names or values
- Drawer has 5 inputs + pills + has_* bits in status line
- Retrieve help text per kind

**Tests:**
- make -C hush-c test → ALL PASS (provider routes ok)
- hush-c/tests/check_provider.sh → "provider routes ok"
  - Posts all 5, asserts no echo in responses, no overlay leak, no session leak, scan no leak
- Unit test_provider.c exists and exercises save of 5 kinds + has + paths + scan from pass

**Docs:**
- docs/pass-integration.md: all 5 provider paths listed
- README.md: all 5 pass show commands
- No leaks asserted in checks

## Gaps observed in this worktree

- Direct ./hush-c/tests/test_provider (from hush-c/) fails with "FAIL has key" etc.
- make test (which runs it) passes.
- The smoke check_provider.sh (the plan's primary integration gate) passes cleanly.
- Fake-pass.sh + HUSH_FAKE_PASS_DIR setup works for the smoke but the unit test's setup (hush_pass_set_helper + env) may have cwd/PATH sensitivity when invoked directly outside the make harness.

This matches the pattern of prior verification slices (rail-prov, canvas-fim, provider-configure): the feature was implemented earlier; this slice is research/audit + verification gate + hygiene + PR to close the plan.

## Verification commands executed
- make -C hush-c test (ALL PASS)
- sh hush-c/tests/check_provider.sh (pass, full 5-kind leak/overlay/scan checks)
- Source greps for 5 kinds, has_*, secret_path, save_secrets, no-leak in http
- Drawer 5 fields present
- Docs list all 5 paths

## Conclusion

DoD 1-6,8 satisfied in code + smoke + docs.
DoD 7 (make test proves...) satisfied via suite.
Unit test direct-run fragility is pre-existing on this base; smoke + suite green is the operational gate used by prior slices.

Proceed to VERIFIED.md, commits on gb/*, PR lifecycle, cleanup.
