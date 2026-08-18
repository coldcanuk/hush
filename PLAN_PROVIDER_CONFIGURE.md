# PLAN: Provider configure (pencil per AI runtime)

Frozen after Phase 1 synthesis (2026-08-18). Execute only this file.
Worktree: `/opt/repo/hush/worktrees/provider-configure`
Branch: `gb/provider-configure`

## Primary Goal

Selecting an AI provider on Raise a robot reveals a tailored Configure
pencil. The human reuses the existing home configuration for that
runtime, or enters the credentials that runtime actually needs. API
providers take a key + host, scan models, and store the chosen model.

## Non-Goals

- Spawning Goose / Codex / Grok / Cline as child agent processes.
- Live LLM chat from the hive.
- Writing `~/.config/goose/config.yaml`, `~/.grok/config.toml`, or
  `~/.codex/config.toml`.
- OAuth browser dance from C.
- Authenticated remote control API.
- Adding Deepseek as a ninth radio (follow-up).
- libcurl / a first-party TLS client.

## Success Criteria / DoD

1. Each of the eight radios shows a 44px pencil once selected.
2. Pencil opens `#provider-drawer` whose fields depend on the id.
3. Goose reads `~/.config/goose/config.yaml` (legacy `~/.goose` probed).
4. Grok Build detects `~/.grok/auth.json` / `grok` on PATH.
5. Codex detects `codex` on PATH + `~/.codex`.
6. Cline shows an honest empty state plus optional API fields.
7. API family: key + host + Scan models + model select/type-in.
8. Keys only in `pass` at `providers/<id>/api_key`. Host/model in
   `$XDG_CONFIG_HOME/hush/providers.json` (0600). Never in session JSON.
9. `GET /api/provider`, `POST /api/provider`, `POST /api/provider/scan`.
10. `./configure && make && make test` pass. Embed after HTML change.
11. PR merged, worktree removed, main clean.

## Constraints

C11 + write-legible-c. Worktree/PR law. JSON is string-field only.
Scan uses `curl` argv if present; typed model always works without it.
No writes to foreign home configs.

## Assumptions

Provider stays a roster label on the robot. Configure state is
hive-global (one overlay per provider id). Existing Goose/Grok/Codex
homes are borrowed, never overwritten.

## Environment

- gcc, make, `./configure`, `make test`
- `curl` for scan + check scripts
- `pass` / `hush-pass` for keys
- goose 1.46.0 docs: https://goose-docs.ai/

## Top Risks

1. HTTPS scan without a client → curl argv + typed-model fallback.
2. Accidentally writing Goose/Grok/Codex homes → read-only detect.
3. Key leakage → pass only; tests assert GET bodies have no key.
4. Cline missing on this host → honest empty state.
5. User said `~/.goose` → probe both; official path is
   `~/.config/goose`.

---

## Phase 0 — Isolation (done)

- [x] Task 1 of M0.1: clean main at f29a6d62c, worktree
      `worktrees/provider-configure` on `gb/provider-configure`.
- Verify: `pwd` contains `/hush/worktrees/` and branch is
  `gb/provider-configure`.

## Phase 1 — Research (this commit)

### M1.1 Inspect current raise form and roster

- [x] Task 1 of M1.1: read `index.html` radios, `hush_roster.h`,
      `hush_http.c` `/api/agent`, `UI_SPEC.md` §9.
- Verify: eight wire ids, no configure UI, no `/api/provider`.

### M1.2 Inspect real provider homes and official Goose docs

- [x] Task 1 of M1.2: Goose docs + `goose info` →
      `~/.config/goose/config.yaml`.
- [x] Task 2 of M1.2: `~/.grok`, `codex doctor`, Cline absence,
      API host defaults from Goose providers table.
- Verify: family table in RESEARCH.md.

### M1.3 Synthesis gate

- [x] Task 1 of M1.3: append RESEARCH.md addendum.
- [x] Task 2 of M1.3: write this PLAN and freeze it.
- Verify: `rg -n "Provider configure" RESEARCH.md PLAN_PROVIDER_CONFIGURE.md`
- Commit: `Milestone 1.3: freeze provider-configure research and plan`

## Phase 2 — Define / Architecture

### M2.1 Lock UI_SPEC + API table

- Task 1 of M2.1: add UI_SPEC §11 Provider configure (pencil,
  three families, Payne copy, drawer Close is not hive Close).
- Task 2 of M2.1: add GET/POST `/api/provider` and
  POST `/api/provider/scan` to the API table.
- Task 3 of M2.1: add `deepseek-api` as an explicit non-goal note.
- Verify: `rg -n "provider-drawer|/api/provider" UI_SPEC.md`
- Commit: `Milestone 2.1: lock provider-configure UI and API contract`

### M2.2 Risk register + module boundary

- Task 1 of M2.2: new module `hush_provider` owns detect / overlay
  file / pass key / curl scan. HTTP only dispatches. Roster still
  owns the eight-id allowlist.
- Task 2 of M2.2: document curl argv contract (no key on argv;
  `CURL_KEY` env or header file 0600 in `$TMPDIR`).
- Verify: this PLAN still names one owner per concern.
- Commit: `Milestone 2.2: lock provider module boundary`

## Phase 3 — Implementation

### M3.1 `hush_provider` detect + overlay (no HTTP yet)

- Task 1 of M3.1: add `hush-c/include/hush_provider.h` with
  families, default hosts, `hush_provider_status_t`,
  `hush_provider_detect`, `hush_provider_load`, `hush_provider_save`,
  `hush_provider_set_key`, `hush_provider_has_key`.
- Task 2 of M3.1: implement `hush-c/src/hush_provider.c`
  (write-legible-c: fn ≤40, depth ≤2, named literals).
- Task 3 of M3.1: unit test `hush-c/tests/test_provider.c`
  using a temp HOME / XDG_CONFIG_HOME.
- Verify: `./configure && make -C hush-c tests/test_provider && ./hush-c/tests/test_provider`
- Commit: `Milestone 3.1: provider detect and overlay file`

### M3.2 Scan via curl argv

- Task 1 of M3.2: `hush_provider_scan` spawns curl when present,
  parses OpenAI-style `"id":"` and Gemini `"name":"models/` into
  at most 32 model names.
- Task 2 of M3.2: missing curl or non-zero exit →
  `HUSH_ERR_IO` with last-error string; caller still 200s a
  `{ok:false,error:…}` so the UI can type a model.
- Verify: unit test with a fake curl script on PATH.
- Commit: `Milestone 3.2: provider model scan via curl`

### M3.3 HTTP routes

- Task 1 of M3.3: GET `/api/provider` and POST `/api/provider` in
  `hush_http.c`. POST `/api/provider/scan`.
- Task 2 of M3.3: never write `api_key` into any GET body.
- Verify: `check_provider.sh` on an ephemeral port.
- Commit: `Milestone 3.3: /api/provider routes`

### M3.4 Raise-form pencil + tailored drawer

- Task 1 of M3.4: radio change shows `#provider-cfg` pencil (44px)
  next to the selected label.
- Task 2 of M3.4: `#provider-drawer` fields swap by family.
- Task 3 of M3.4: Save posts `/api/provider`; Scan posts
  `/api/provider/scan`; leftover typed model always allowed.
- Task 4 of M3.4: `./scripts/embed-ui.sh hush-c/demo`
- Verify: `check_launch.sh` greps `id="provider-cfg"` and
  `/api/provider`.
- Commit: `Milestone 3.4: configure pencil and tailored drawer`

### M3.5 Deepseek non-goal + skill + README

- Task 1 of M3.5: agent-create skill mentions configure but does
  not require it to raise.
- Task 2 of M3.5: README / pass-integration: key path
  `pass show hush/providers/<id>/api_key`.
- Verify: `rg -n "providers/<id>/api_key" docs/pass-integration.md`
- Commit: `Milestone 3.5: docs and Payne skill for provider configure`

## Phase 4 — Verify, PR, cleanup

### M4.1 Full test + §14

- Task 1 of M4.1: `./configure && make clean && make && make test`
- Task 2 of M4.1: write-legible-c §14 on every touched `.c`/`.h`.
- Verify: `ALL TESTS PASSED`
- Commit: `Milestone 4.1: provider-configure tests and §14`

### M4.2 PR + land + delete worktree

- Task 1 of M4.2: `git push -u origin HEAD`
- Task 2 of M4.2: `gh pr create --base main --head gb/provider-configure`
- Task 3 of M4.2: `gh pr merge --auto --merge` and wait MERGED.
- Task 4 of M4.2: from main checkout, pull, `git worktree remove`,
  delete `gb/provider-configure` (use `gh api` if hooks block
  `git push --delete` from main).
- Verify: `git worktree list` is only main; `git status` clean.
- Do **not** state “Grok Build complete.” until this milestone is done.

---

## Audit (before execute)

- Every remaining task names its milestone, a command or snippet,
  and a verify step.
- Phase 1 ends with RESEARCH + this frozen plan.
- Worktree path is inside `/opt/repo/hush/worktrees/`.
- Land is PR-only. Direct main is forbidden.
