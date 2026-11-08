# Design: Schema Evolution for Hush Router JSON Files (Issue #374)

Status: DRAFT — for review before any code changes (per CoS decision: design doc first,
"Never without my yes" on core router logic / JSON schema).

## 1. Problem

`hush_router_load` is strict. Any structural change to the router JSON files
(new field, removed field, changed type) causes a load failure, breaking
continuity for existing users. This blocks the Super-agent evolution, which
needs to grow the router state (workitems, workers, etc.) over time.

## 2. Current state (observed in `hush-c/src/hush_router.c`)

Router state lives under `$HUSH_HOME/router/` as six JSON files plus two markers:

| File | Shape | Fields (as loaded) |
|------|-------|--------------------|
| `workitems.json` | array of objects | `id` (req), `title`, `description`, `status`→phase, `channel_id`, `created` (i64), `updated` (i64), `assigned_worker_id`, `parent_id` |
| `workers.json` | array of objects | `id` (req), `workitem_id`, `specialist_id`, `status`→phase, `started` (i64), `last_heartbeat` (i64), `job_dir`, `provider_id` |
| `decisions.json` | array of objects | `id` (req), `workitem_id`, `text`, `created` (i64) |
| `artifacts.json` | array of objects | `id` (req), `worker_id`, `text` |
| `edges.json` | array of objects | `from` (req), `type`→kind, `to` |
| `router_state.json` | object | `next_id` (uint), `last_line` (string) |
| `commit` | marker file | presence = a save is in flight |
| `lock` | flock file | writer lock |

Loader mechanics:
- Each array loader iterates index `i`, reads `/<i>/id` (or `from` for edges).
  A missing `id`/`from` is treated as end-of-array (returns OK).
- Every other field is read via `hush_router_take_field` / `hush_router_take_i64`
  wrapped in `HUSH_ROUTER_TRY`. A missing field returns `HUSH_ERR_NOT_FOUND`
  and **fails the whole load**.
- `status`/`type` are parsed by `hush_router_phase_parse` / `hush_router_edge_parse`
  immediately after the read.
- Unknown/extra fields are already ignored (path-based lookup only reads known paths).
- Type mismatch (e.g. string where an int is expected) returns `HUSH_ERR_PARSE` and fails.

So today: **missing field = hard failure; extra field = ignored; wrong type = hard failure.**

## 3. Chosen approach: tolerant reader (primary) + optional `schema_version` (future)

We choose the **tolerant reader** as the primary mechanism, over per-file
`schema_version` + versioned loaders. Rationale:

- Minimal change: no migration step, no version dispatch, no per-version code paths.
- Naturally handles the two evolution directions we actually need:
  - **Add a field** — old readers ignore it (don't look it up); new readers load it if present.
  - **Make a field optional** — readers use a default when it is absent.
- Per-file `schema_version` would require a migration tool and N versioned loaders,
  which is more code and more failure surface for the same outcome.

We will **not** change the on-disk schema in this step. The tolerant reader is a
read-side change only. A `schema_version` field is proposed as an optional,
non-breaking future enhancement (see §7) so a clean-break is possible later if ever needed.

## 4. What "tolerant" means (precise contract)

For every non-identity field in every loader:
- **Field present, well-typed** → load it (unchanged behavior).
- **Field present, wrong type** → **still fail** (`HUSH_ERR_PARSE`). We do not silently
  accept corrupt data; a type change is a breaking change and must go through review.
- **Field absent** → **do not fail**. Leave the struct field at its zero-initialized
  default and continue.

Identity fields (`id` for items/workers/decisions/artifacts, `from` for edges) keep
their current semantics: absent = end-of-array, present-but-malformed = fail.

Zero-initialized defaults (already what `hush_router_init` gives us) are sensible:
- strings → `""`
- i64 timestamps → `0`
- `phase` → `HUSH_ROUTER_PENDING` (0)
- `edge.kind` → 0 (first enum value)

## 5. Implementation plan (in `hush-c`)

### 5.1 Two new static helpers in `hush_router.c`

```c
/* Like hush_router_take_field, but HUSH_ERR_NOT_FOUND is not an error:
 * the output is left untouched and HUSH_OK is returned. Any other status
 * (PARSE, FULL, ARG) is propagated. */
static hush_status_t hush_router_take_field_optional(char *out, size_t outsz,
                                                    const char *json, const char *path);

/* Same contract for hush_router_take_i64. */
static hush_status_t hush_router_take_i64_optional(int64_t *out,
                                                  const char *json, const char *path);
```

Both are thin wrappers: call the existing function, map `HUSH_ERR_NOT_FOUND` →
`HUSH_OK`, pass everything else through. No changes to `hush_json.c` /
`hush_json_read.c` are required — the tolerance lives entirely in the router layer.

### 5.2 Loader changes (per field)

For each non-identity field, replace:

```c
HUSH_ROUTER_TRY(hush_router_take_field(dst, sizeof dst, body, key));
```

with:

```c
status = hush_router_take_field_optional(dst, sizeof dst, body, key);
if (status != HUSH_OK)
    return status;   /* NOT_FOUND already mapped to OK; only real errors reach here */
```

For fields with a post-parse step (`status`→`hush_router_phase_parse`,
`type`→`hush_router_edge_parse`), only run the parser when the field was present.
We detect presence by checking whether the optional read actually wrote a value
(the buffer is non-empty), so an absent `status` leaves `phase` at PENDING instead
of feeding `""` into the parser (which would error).

Files/functions touched:
- `hush_router_load_items` — title, description, status, channel_id, created, updated, assigned_worker_id, parent_id
- `hush_router_load_workers` — workitem_id, specialist_id, status, started, last_heartbeat, job_dir, provider_id
- `hush_router_load_decisions` — workitem_id, text, created
- `hush_router_load_artifacts` — worker_id, text
- `hush_router_load_edges` — type, to
- `hush_router_load_state` — next_id, last_line (both optional; absent → keep current `next_id`/empty `last_line`)

Identity fields (`id`/`from`) are left exactly as they are.

### 5.3 Saver

No change. The saver already writes every field, so once a newer build runs, files
are normalized to the full current field set on the next save. Old files keep
working in the meantime because the reader tolerates the missing fields.

### 5.4 Migration utility (optional, follow-up)

Not required for correctness — the tolerant reader loads both old and new files.
If we want to normalize existing files eagerly, a small `hush-relay router migrate`
(or standalone tool) can load the graph and re-save it, which fills in all current
fields with their defaults. This is a convenience, not a prerequisite, and can be
a separate PR.

## 6. Backward / forward compatibility

- **Old file → new reader:** missing fields → defaults; loads OK. ✔
- **New file → old reader:** extra fields ignored (path lookup); loads OK. ✔
- **New file → new reader:** all fields present; loads OK. ✔
- **Type change:** still fails (by design) — a type change is a breaking change and
  must be reviewed, not silently absorbed. To evolve a type, add a new field with a
  new name, populate it, and deprecate the old one (two-step, both reviewed).

## 7. Optional future: `schema_version`

If a clean-break ever becomes necessary, add a top-level `schema_version` integer to
`router_state.json` (default 1 when absent). Readers would then be able to detect
the version and, if it is newer than understood, refuse with a clear "run the
migrator" message instead of guessing. This is additive and non-breaking; it is
**not** part of this step and is listed only so the path exists.

## 8. Concurrency

Unchanged. The router already serializes writers via `router/lock` (flock) and
publishes atomically (stage to `*.staged`, rename, then `commit` marker). The
tolerant reader only changes how a *published* file is parsed; it does not touch
locking or the atomic-publish path. A reader that hits a half-written file is
already protected by the existing `commit`-marker check (`hush_router_unmarked`).

## 9. "Never without my yes" compliance

- This document is the review gate. **No code changes until it is approved.**
- The change is read-side only; it does not alter the on-disk schema or the
  single-binary constraint.
- It deliberately does **not** make type changes tolerant, so no silent data
  corruption path is introduced.

## 10. Testing (evidence of completion)

Add cases to `hush-c/tests/test_router.c`:
1. **Missing fields load:** write a `workitems.json` with an item missing
   `description`, `updated`, `parent_id`; load; assert OK and that those fields
   hold their defaults (`""`, `0`, `""`).
2. **New/extra fields ignored:** write an item with an unknown field
   (`"extra":"x"`); load; assert OK and the known fields are correct.
3. **Type mismatch still fails:** write `created` as a string; load; assert
   `HUSH_ERR_PARSE` (we do not silently accept it).
4. **Round-trip:** load an old-shape file, save, reload; assert the graph is
   stable and the saved file now contains the full field set.
5. **State file optional:** `router_state.json` missing `last_line` loads OK.

Then run the full existing suite (`check_router_san.sh` + `test_router`) to confirm
no regressions.

## 11. Risks & mitigations

| Risk | Mitigation |
|------|------------|
| A field that *should* be required is silently defaulted | Only non-identity fields are optional; identity fields keep end-of-array semantics. Defaults are sensible (empty/0/PENDING). |
| Corrupt (wrong-type) data silently accepted | Type mismatch still returns `HUSH_ERR_PARSE`; tolerance is missing-only, not type-coercing. |
| Defaulted values mislead downstream logic | Defaults match `hush_router_init`; downstream already handles empty/0/PENDING. |
| Scope creep into schema changes | This step is read-side only; any schema change is a separate reviewed PR. |

## 12. Next steps (after approval)

1. Implement §5.1–5.2 in `hush_router.c` (worktree `gb/schema-evolution`).
2. Add the §10 tests.
3. Run the full suite; capture output as evidence.
4. Open a PR (no merge — CoS merges).
5. Optional: §5.4 migrator as a follow-up PR.
