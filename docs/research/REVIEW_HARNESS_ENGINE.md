# Technical Review — Harness Engine, Token/Context, Signaling (PR #130)

Branch `gb/harness-engine`, 15 commits on top of `main`. All work is behind the
C11 strict build (`-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`) and
the full `make test` suite is green.

This review is a self-assessment of the change set, written against the actual
code rather than the plan. It separates *verified-correct* from *known risk*.

---

## 1. What landed

| Area | Milestone | Files |
|------|-----------|-------|
| Capability matrix | `hush_provider_caps_t` (TOOLS/IMAGE/FILE_ATTACH), `capabilities()`/`can()` | `hush_provider.{h,c}` |
| Providers + policy flags | 9→13 runtimes (agy/copilot/ollama/custom); `SPAWN_ONLY`/`ALLOWLIST`/`OAUTH` | `hush_provider.{h,c}`, `hush_roster.{h,c}` |
| Capability gating | file context denied for providers without `FILE_ATTACH` | `hush_roster.c` |
| Auto-update scanner | `hush_provider_update_all()` (opt-in grok/codex) | `hush_provider.{h,c}` |
| Token chunker | `hush_seg` structural splitter (UTF-8 safe, markdown fences) | `hush_seg.{h,c}` |
| Context flow | persist body in memory; inject chunked body into `-p` note | `hush_roster.{h,c}`, `hush_agent.c` |
| Chaperon rule | standalone robots auto-assign Payne | `hush_launch.c` |
| Signaling reliability | cevent drop counter | `hush_cevent.{h,c}` |
| UI | exit/leave modal z-index fix | `demo/index.html` |
| **Second runtime** | `agy -p` spawn-only execution; provider-aware dispatcher | `hush_agent.c` |

---

## 2. Architecture assessment

**The provider registry is the backbone and it is clean.** A single static
meta table carries `id/label/family/host/binary/caps/flags`, with small query
functions layered on top. Adding a provider is a table entry plus (if it
executes) a readiness check and an exec branch — not a scattered rewrite.

**The dispatcher is now a real abstraction.** `hush_agent_exec_child()` does
the stdio setup once and dispatches to `hush_agent_exec_agy()` or
`hush_agent_exec_grok()`. `hush_agent_runtime_ready()` gates readiness per
provider. This is the right seam for the remaining runtimes.

**The chunker is self-contained and well-tested.** It has no external
dependencies, preserves UTF-8 boundaries, and treats markdown fences as atomic.
Good candidate for reuse beyond context injection.

---

## 3. Correctness review (verified)

The following were traced by hand against the code and are correct:

- **`hush_seg` fence atomicity.** The line scanner toggles `in_fence` only on
  a `>=3` backtick/tilde run, closes only on the matching char, and refuses to
  record a newline/sentence/space boundary while inside a fence. Traced the
  `A.\n\n```\nline\n```\n\nB.` case → first span ends exactly at the blank line
  before the fence.
- **UTF-8 hard-cap backoff.** `hush_seg_utf8_back()` walks an exclusive end
  offset back off continuation bytes; the `cap <= off` fallback advances a full
  codepoint, so a multi-byte run can never split a codepoint or stall.
- **Context injection is bounded.** `hush_agent_append_context()` uses
  `snprintf` for the header and `hush_seg_split` with the remaining-room as
  both target and cap; every `memcpy` is length-checked. No overflow path.
- **agy combined-prompt sizing.** `prompt` (≤1023) + `rules` (≤1023) +
  `note` (≤4096) + two newlines + NUL = 6145 < `HUSH_AGENT_AGY_PROMPT_MAX`
  (6160). No truncation of the human note.
- **Provider JSON arg alignment.** `hush_http_append_provider` has 18 format
  specifiers and 18 arguments (verified after adding `caps` and `flags`).
- **Provider-list sync.** `hush_roster_providers[]` (13) and
  `hush_provider_meta[]` (13) name the same set, so `hush_roster_is_provider()`
  and `hush_provider_can()` can never disagree on a valid id.

---

## 4. Issues found and fixed during this review

- **agy was being dispatched for grok-tuned jobs.** The original dispatch
  keyed only on `job->provider`, so an agy robot elected leader (or a
  fixup/plan job) would have received a grok-specific plan/elect prompt through
  `agy -p`. Fixed: agy routes only for `HUSH_AGENT_KIND_NOTE_JOB`; the
  fixup/plan/elect prompts stay on grok-build regardless of the robot's
  provider. (Commit `a848dbc51`.)

- **Dead code block in `hush_agent_finish_job`.** A trailing `if (store)`
  block built a `bot` and `parent` struct (eight memset/copy ops) and then
  discarded them via `(void)` casts — a disabled "error on_deck" path. Removed
  during this audit; the function now just closes the job.

---

## 4.5 Phantom code and placeholders found (audit)

- **~~`hush_sha256_hex()` is a stub.~~** **Fixed in this branch.** Now streams
  the NIP-01 canonical preimage straight into OpenSSL `EVP_sha256()`; the
  dead `#if HUSH_USE_OPENSSL` guard (never defined by `configure`) is gone.
  OpenSSL is already a hard dependency via `hush_identity.c`, so the guard
  was pure dead weight.
- **~~`hush_event_compute_id()` is phantom.~~** **Fixed in this branch.** It is
  now the single authoritative id path and is called from every event fill:
  `hush_launch`, `hush_roster`, `hush_agent`, `hush_intel`, `hush_presence`,
  and `hush_http` (post + signal). Serialization is NIP-01 canonical
  `[0,pubkey,created_at,kind,tags,content]` with string escaping and tags.
- **~~Four near-duplicate `make_id()` helpers.~~** **Fixed in this branch.**
  `hush_agent_make_id`, `hush_roster_make_id`, `hush_presence_make_id`,
  `hush_intel_make_id`, and `hush_launch_make_id` are all deleted. A sixth
  duplicate, `hush_make_event_id()` in `hush_http.c` (the human-message path,
  `0x9e3779b9` variant), was found during migration and removed too. Only
  `hush_skill_make_id()` remains — that one is a deterministic catalog key
  (`robot:slug`), not a Nostr event id, so it correctly stays separate.
  Verified against three hard-coded NIP-01 SHA-256 preimages in
  `tests/test_event.c`.
- **`hush_provider_update_all()` is built but unwired** — production never
  calls it (only `test_provider.c`). Awaiting launch policy sign-off.
- **`hush_provider_capabilities()` and `hush_provider_flags()` are test-only
  accessors.** Production reads caps/flags from the `/api/provider` JSON, not
  these functions. Legitimate API surface, but currently redundant.

---

## 5. Known risks and limitations

These are honest and should gate the remaining work.

1. ~~**Makefile has no header-dependency tracking.**~~ **Fixed in this branch.**
   `src/%.o` now compiles with `-MMD -MP` and the Makefile `-include`s the
   generated `.d` files, so a header change rebuilds every consumer. Verified
   by touching `include/hush_provider.h` and observing `hush_agent.c` rebuild.

2. **Two provider lists.** `hush_roster.c` and `hush_provider.c` each keep the
   13 ids. They currently agree, but nothing enforces that at compile time.
   Worth collapsing to one source of truth.

3. **agy gets no cwd isolation.** grok runs under `--cwd <temp>`; agy does not.
   Acceptable for spawn-only text generation, but a divergence to note.

4. **Context body is in-memory only.** Deliberate: it is not serialized to the
   session JSON, so file content stays off the wire and out of a 200 KB-per-
   agent BSS footprint is avoided. The trade-off is that context is lost on
   restart.

5. **Auto-update scanner is not wired into launch.** Deliberate: silently
   running `grok update`/`codex update` at startup is a policy decision. The
   primitive exists and is tested; wiring is one env-gated line when approved.

6. **cevent JSON is served over HTTP** via `hush_http_serve_chan_events()`
   (`/api/chan-events`), so the drop counter does reach the UI. (An earlier
   draft of this review wrongly claimed it was not wired — corrected.)

7. **Remaining runtimes need verified CLIs.** `goose`/`codex`/`copilot`/
   `ollama` still fall back to grok. I refused to fabricate their headless
   invocations; a harness that runs the wrong command is worse than a clean
   fallback. `agy -p` was the one I could verify from the brief.

---

## 6. Verification

- `./configure && make clean && make && make test` → **ALL TESTS PASSED**.
- Unit tests: `test_provider`, `test_roster`, `test_seg`, `test_cevent`,
  `test_launch` all green.
- End-to-end: `check_agy.sh` proves an agy robot runs `agy -p` (not grok) and
  the combined prompt reaches the argument; `check_agent.sh` proves context
  reaches the grok `-p` note.

---

## 7. Recommendations (ordered)

1. ~~Fix the Makefile header-dependency gap~~ — done in this branch.
2. Collapse the two provider id lists into one shared definition.
3. Get sign-off on launch-time auto-update, then env-gate the wiring.
4. Before wiring the next runtime, confirm its exact headless invocation from
   official docs; then mirror the agy pattern (exec branch + readiness gate).
5. Phase 5 UI layout work (responsive edit-menu, Skills Forge screen, avatar
   window, rail redesign) needs visual verification — do not attempt in a
   CI-only loop without a browser check.
