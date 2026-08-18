# PLAN: Provider secrets in unix `pass`

Frozen after Phase 1 synthesis (2026-08-18). Execute only this file.
Worktree: `/opt/repo/hush/worktrees/provider-pass-audit`
Branch: `gb/provider-pass-audit`

## Primary Goal

Every AI-provider secret Hush accepts — API key, username, password,
token, passkey — is stored only in the local unix password manager
`pass`. Audit is done. Close the gaps. Retrieve CLIs are documented.

## Non-Goals

- Copying Goose `secrets.yaml`, Grok `auth.json`, Codex login, or
  Cline editor storage into `pass`.
- Writing those foreign homes.
- OAuth / WebAuthn ceremonies from C.
- Deepseek radio.
- libcurl / first-party TLS.
- TURN / vibe / identity secrets.
- Requiring `pass` to save host/model overlay.

## Success Criteria / DoD

1. Each secret kind has one path: `hush/providers/<id>/<kind>`.
2. Overlay `providers.json` never contains secret values.
3. `GET /api/provider` and `GET /api/session` never echo secrets and
   never use secret field names (`api_key`, `username`, `password`,
   `token`, `passkey`) as JSON keys.
4. `POST /api/provider` writes each non-empty secret via
   `hush_pass_save`. Soft-fail if `pass` is missing.
5. `POST /api/provider/scan` uses a posted key if present, else loads
   `api_key` then `token` from `pass`. Key never on argv.
6. Drawer accepts the five kinds. Empty after paint. Help shows
   retrieve CLI for each `has_*`.
7. `make test` proves save-to-pass, scan-from-pass, and no leak.
8. Docs list every path. PR merged, worktree removed, main clean.

## Constraints

C11 + write-legible-c. Worktree/PR law. JSON is string-field only.
Never put a secret on argv. Missing `pass` never blocks overlay.
Widen `HUSH_PASS_SECRET_MAX` and `HUSH_PROVIDER_KEY_MAX` to 512.

## Assumptions

Configure stays hive-global. Foreign homes stay borrowed and unread
for secret material. Username / password / token / passkey are
optional; public API hosts still need only `api_key`.

## Environment

- gcc, make, `./configure`, `make test`
- `curl` for scan + check scripts
- `pass` / `hush-pass` / `tests/fake-pass.sh`

## Top Risks

1. Hick from five secret fields → API key primary; others behind
   one disclosure.
2. Copying foreign homes → still read-only detect.
3. Leak via GET field names → only `has_*`; tests grep kinds.
4. Scan without posted key → `hush_pass_get` fallback.
5. `pass` missing → soft-fail; overlay still writes.

---

## Phase 0 — Isolation (done)

- [x] Task 1 of M0.1: clean main at 8e0172125, worktree
      `worktrees/provider-pass-audit` on `gb/provider-pass-audit`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/provider-pass-audit`.

## Phase 1 — Research (this commit)

### M1.1 Inventory current secret paths

- [x] Task 1 of M1.1: read `hush_pass`, `hush_provider`, HTTP
      handlers, drawer, tests, `docs/pass-integration.md`.
- [x] Task 2 of M1.1: confirm overlay formatter writes no values.
- [x] Task 3 of M1.1: confirm scan does not call `hush_pass_get`.
- Verify: table in RESEARCH.md addendum.

### M1.2 `pass` conventions and credential kinds

- [x] Task 1 of M1.2: `pass` on PATH; store layout; hush-pass prefix.
- [x] Task 2 of M1.2: lock five kinds and retrieve CLIs.
- Verify: RESEARCH.md locked contract.

### M1.3 Synthesis gate

- [x] Task 1 of M1.3: append RESEARCH.md addendum.
- [x] Task 2 of M1.3: freeze this PLAN.
- Verify: `git add . && git commit` after this milestone.

```
git add .
git commit -m "Milestone 1.3: freeze provider-pass-audit research and plan"
```

## Phase 2 — Architecture

### M2.1 Lock module contract

- [ ] Task 1 of M2.1: name the five kinds in `hush_provider.h`
      (`HUSH_PROVIDER_SECRET_*`, `HUSH_PROVIDER_SECRET_COUNT`).
- [ ] Task 2 of M2.1: status grows `has_username`, `has_password`,
      `has_token`, `has_passkey`. Input grows matching optional
      `const char *` fields.
- [ ] Task 3 of M2.1: `hush_provider_secret_path(out, outsz, id, kind)`.
- [ ] Task 4 of M2.1: scan loads pass when posted key is empty.
- [ ] Task 5 of M2.1: update `UI_SPEC.md` §11 + version line.
- Verify: header compiles conceptually; UI_SPEC lists five retrieve
  CLIs. Then commit.

```
git add .
git commit -m "Milestone 2.1: lock provider secret kinds and UI_SPEC"
```

## Phase 3 — Implementation

### M3.1 Pass paths + save/load in `hush_provider`

- [ ] Task 1 of M3.1: widen `HUSH_PASS_SECRET_MAX` and
      `HUSH_PROVIDER_KEY_MAX` to 512.
- [ ] Task 2 of M3.1: kind table + `hush_provider_secret_path`.
- [ ] Task 3 of M3.1: `hush_provider_save` writes each non-empty
      kind via `hush_pass_save`; soft-fail; overlay still writes.
- [ ] Task 4 of M3.1: status sets each `has_*` from `hush_pass_has`.
- [ ] Task 5 of M3.1: scan: if posted `api_key` empty, `hush_pass_get`
      `api_key` then `token`.
- Verify: `make -C hush-c tests/test_provider` after M3.2 wires tests.
- Commit after M3.2 so tests land with the code.

### M3.2 Unit tests

- [ ] Task 1 of M3.2: save openai `api_key` + `username` + `password`
      + `token` + `passkey`; `hush_pass_has` each path.
- [ ] Task 2 of M3.2: read overlay file; assert it does not contain
      any of the five values.
- [ ] Task 3 of M3.2: scan with NULL key after save uses pass.
- Verify: `./hush-c/tests/test_provider` from `hush-c/` prints `ok`.

```
git add .
git commit -m "Milestone 3.2: store every provider secret kind in pass"
```

### M3.3 HTTP + UI + smoke

- [ ] Task 1 of M3.3: POST parse five optional fields. GET append
      `has_*` booleans only. Never write secret kind names as keys.
- [ ] Task 2 of M3.3: scan handler: posted key else leave empty so
      C loads from pass.
- [ ] Task 3 of M3.3: drawer fields + disclosure + retrieve help.
- [ ] Task 4 of M3.3: `./scripts/embed-ui.sh hush-c/demo`.
- [ ] Task 5 of M3.3: `check_provider.sh` posts all five kinds;
      GET must not contain kind names or sample values; overlay
      file must not contain sample values; scan with no key after
      save (fake-pass not in smoke HOME — assert overlay-only and
      leak checks). Smoke stays honest: without `pass`, `has_*`
      stay false; leak checks still fire.
- Verify: `sh hush-c/tests/check_provider.sh`.

```
git add .
git commit -m "Milestone 3.3: HTTP and drawer keep secrets only in pass"
```

### M3.4 Docs

- [ ] Task 1 of M3.4: `docs/pass-integration.md` five rows.
- [ ] Task 2 of M3.4: `SECURITY.md` provider table.
- [ ] Task 3 of M3.4: `README.md` provider section.
- [ ] Task 4 of M3.4: agent-create skill retrieve paths.
- Verify: `rg -n "providers/<id>/" docs/pass-integration.md SECURITY.md`.

```
git add .
git commit -m "Milestone 3.4: document every provider pass path"
```

## Phase 4 — Verify, land, cleanup

### M4.1 Full suite + §14

- [ ] Task 1 of M4.1: `./configure && make clean && make && make test`
      from worktree root.
- [ ] Task 2 of M4.1: write-legible-c §14 on every touched `.c`/`.h`.
- Verify: `ALL TESTS PASSED`.

```
git add .
git commit -m "Milestone 4.1: provider-pass-audit verified"
```

### M4.2 PR and merge

- [ ] Task 1 of M4.2: `git push -u origin HEAD`
- [ ] Task 2 of M4.2: `gh pr create --base main --head gb/provider-pass-audit`
- [ ] Task 3 of M4.2: `gh pr merge --merge` (auto-merge is disabled).
- Verify: GitHub shows MERGED.

### M4.3 Cleanup

- [ ] Task 1 of M4.3: `cd /opt/repo/hush && git pull --ff-only origin main`
- [ ] Task 2 of M4.3: remove worktree; delete local branch;
      `gh api -X DELETE repos/coldcanuk/hush/git/refs/heads/gb/provider-pass-audit`
- Verify: `git status` clean on main; `git worktree list` has no
  `provider-pass-audit`. Then state Grok Build complete.

---

## Audit of this plan (Phase 4 of RDAP, pre-execution)

- Every task names its milestone, has CLI/code, and a verify step.
- Research → plan-update gate is M1.3.
- Worktree lifecycle matches Prime Directive (PR, not local merge).
- Tasks are atomic. Hick risk has a mitigation.
- Frozen. Execute from here.
