# Progress Log for Issue #374: Schema Evolution for Hush Router JSON Files

## Date: 2025-04-08 (example date, adjust as needed)

### Analysis Performed:
1. Examined the current router loader in `/usr/local/src/hush/hush-c/src/hush_router.c`:
   - Functions: `hush_router_load_items`, `hush_router_load_workers`, `hush_router_load_rest`, etc.
   - Each loader uses `hush_router_take_field` (for strings) and `hush_router_take_i64` (for integers) with `HUSH_ROUTER_TRY`.
   - On `HUSH_ERR_NOT_FOUND` (missing field), the loader currently returns an error and fails the load.
   - The loader does not tolerate unknown fields (it ignores them because it only looks up specific paths).

2. Reviewed the existing router state files (if any) in the workdir to understand current schema:
   - No router state files present in the workdir (only brief.md and state.md).
   - However, we can infer the schema from the loader and saver code.

3. Current schema constraints (from loader and saver):
   - **Workitems** (in `router/items` file, JSON array):
     - Required fields: `id` (string), `title` (string), `description` (string), `status` (string, parsed to phase), `channel_id` (string), `created` (int64), `updated` (int64), `assigned_worker_id` (string), `parent_id` (string).
     - Optional? Currently, all fields are required because the loader fails on missing field.
     - Types: strings for IDs and text, int64 for timestamps.
   - **Workers** (in `router/workers` file, JSON array):
     - Required fields: `id` (string), `item_id` (string), `specialist_id` (string), `job_dir` (string), `provider_id` (string), `started` (int64), `heartbeat` (int64).
   - **Decisions**, **Artifacts**, **Edges**, **State** have similar structures.

### Current Strictness:
   - The loader is strict: any missing required field causes `HUSH_ERR_NOT_FOUND` and the load fails.
   - Unknown fields are ignored (since the lookup is path-specific).
   - Type mismatches (e.g., string where integer expected) cause `HUSH_ERR_PARSE` and fail.

### Proposed Schema Evolution Strategy:
   - **Tolerant Reader Approach**: Modify the loader to treat missing fields as OK (use zero-initialized or default values) and only fail on actual parse errors (e.g., type mismatch when field is present but malformed).
   - This allows:
     - Adding new fields: older versions will ignore them (since they don't look them up) and newer versions will load them if present.
     - Making fields optional: older versions will use default values (zero-initialized) when the field is missing.
     - Changing types: not directly supported; but if we keep the same field name and change type, we would break. Instead, we should avoid changing types of existing fields; if necessary, we can introduce a new field with a new name and deprecate the old one.
   - We will implement helper functions:
     - `hush_router_take_field_optional`: returns `HUSH_OK` on `HUSH_ERR_NOT_FOUND` (leaving output untouched).
     - `hush_router_take_i64_optional`: similarly for integers.
   - For fields that require post-processing (e.g., `hush_router_phase_parse`), we only call the parser if the field was present.

### Implementation Plan:
   - Step 1: Modify `hush_router_load_items` to use optional take functions for all fields except `id` (which is truly required for array element identification).
   - Step 2: Similarly modify `hush_router_load_workers`, `hush_router_load_rest`, etc.
   - Step 3: Ensure that zero-initialized values are sensible (e.g., empty strings, zero timestamps, default phase pending).
   - Step 4: Update the saver to write all fields (it already does) so that new fields added by newer versions will be persisted.
   - Step 5: Create a migration tool (optional) that can explicitly add missing fields with default values, but not strictly necessary because tolerant loader will work.

### Evidence of Progress:
   - We have examined the code and identified the exact points of failure.
   - We have a clear plan to modify the loader to be tolerant.
   - Next step: implement the changes and run the existing test suite to ensure backward compatibility.

### Blockers:
   - None at this time.

### Commitment:
   - I will implement the tolerant loader changes in `hush_router.c` and run the test suite by end of day today (2025-04-08).
   - After successful test run, I will create a design document in this workdir detailing the changes and how they achieve schema evolution.
   - I will then report back with the design document as evidence.

