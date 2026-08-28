# RESEARCH: Issue #116 — Stable presence `d`, three keys, durable claim ledger

**Date:** 2026-08-28  
**Worktree:** `worktrees/issue-116-stable-d` / `gb/issue-116-stable-d`  
**Issue:** https://github.com/coldcanuk/hush/issues/116  
**Gate:** synthesis before any fix. This note quotes the tree as of `9ef77663d`. It does not change behavior.

Hush forked Buzz in git history only. This note does **not** import kind 44102, NIP-MR, Dart mention-ack stores, Buzz ACP, PR 6612, or a self-wiping `HashSet`.

## Mental model (as given)

| Name | Meaning in this tree |
|---|---|
| Belt | One robot acts at most once on one thread for one triggering event |
| Pulley marks | Full robot pubkey hex, conversation root event id, triggering event id |
| Chalk | `g_id_seq` (`"fN"`), `g_intro_hex` / `g_intro_root`, `g_jobs[]`, `g_lines[]`, `g_ack` |
| Timing gun | Intro suppressed, 30315 Working, `events:[]`, `drops==0`, missing 30315 as “offline” |
| Law | Silence is not a reading. Chalk is not a mark. |

## Current vs required keys

| Key | Required | Current (evidence) |
|---|---|---|
| Delivery | `(robot hex, triggering event id)` — this exact kind-1 was already considered | **Missing.** `hush_agent_consider` walks `p` tags. `hush_agent_robot_busy` is RAM `(robot_pub, parent_id)` while a job is `busy`. After finish, the same event can start work again. No durable delivery key. |
| Work / presence `d` | Reconstructible `(robot hex, conversation root)`. Phase is **not** in `d`. | **Collapsed into the process nonce.** `hush_presence_make_d` prefixes the job token. Token is `"f%u"` from `g_id_seq`. |
| Local pipe id | `"fN"` for fixup/HTTP only. Must not appear in `d`, intro identity, or signed events | **Leaks into 30315 `d` and therefore into signed events.** `hush_agent_fill_job` calls `hush_agent_make_token`; `hush_agent_presence_put` sets `in.token = job->token`. |

Phase today lives nowhere durable. Intro is RAM. Jobs are RAM. Presence lines are RAM.

## Quote: current `hush_presence_make_d()`

From `hush-c/src/hush_presence.c`:

```c
hush_status_t hush_presence_make_d(char *out, size_t outsz, const char *token)
{
    int n;

    if (out == NULL || outsz < 8 || token == NULL || token[0] == '\0')
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz, "%s%s", HUSH_PRESENCE_D_PREFIX, token);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}
```

Header contract (`hush-c/include/hush_presence.h`): `Writes d = hive:<token> into out.`

`HUSH_PRESENCE_D_PREFIX` is `"hive:"`. `HUSH_PRESENCE_D_MAX` is 48.

Token mint (`hush-c/src/hush_agent.c` `hush_agent_make_token`):

```c
g_id_seq++;
n = g_id_seq;
(void)snprintf(out, outsz, "f%u", n);
```

`g_id_seq` is a static `unsigned`. `hush_agent_init` does **not** reset it. A new process starts at 0, so the first token is `"f1"`. `d` becomes `"hive:f1"`.

Required formula (issue #116, not implemented):

```
d = "hive:" + lowercase hex of the first 20 bytes of
    SHA-256( robot_hex || ":" || root_hex )
"hive:" + 40 hex = 45 characters.
```

Hash the **full** 64-char robot hex and **full** 64-char root. Do not slice to 12 characters. Use the SHA-256 already used by `hush_event_compute_id` (OpenSSL `EVP_sha256` in `hush-c/src/hush_event.c`). No new hash library.

Reference vector (computed during this research, not from production):

| robot_hex | root_hex | `d` |
|---|---|---|
| 64 × `a` | 64 × `b` | `hive:6173cf9d3267e6b926102c8b149812658d05d834` (45 chars) |

## Presence publish / clear / privacy

- `hush_presence_publish` computes `d` from `in->token`, occupies `g_lines[]` (16 RAM slots), inserts kind **30315** (replace on `(pubkey, kind, d)` via `hush_store_replace_addressable`) and kind **1038** (append-only).
- Idle/Waiting set `expire = now + HUSH_PRESENCE_IDLE_S` (45). Stuck does not (`expire == 0`).
- `hush_presence_clear` builds `d` from token, marks the RAM line not live, inserts empty-content 30315.
- `hush_presence_expire` strips `HUSH_PRESENCE_D_PREFIX` from `g_lines[i].d` and passes that suffix back into `clear` as a token. That only works while `d` is `hive:` + token.
- `hush_presence_req_ok` returns 0 for 30315/1038 when `vibe_public` is 0. `hush_relay.c` skips those kinds on NIP-01 REQ/events dump. `hush_presence_format_json` lists live `g_lines` with no vibe check (operator view).
- `hush_presence_in_t.token` is required for publish. `root` is stored on the line but is not part of `d`.

## Intro, jobs, store, home

| Table | Bound | Lifetime | Restart effect |
|---|---|---|---|
| `g_intro_hex` / `g_intro_root` | `HUSH_AGENT_INTRO_MAX` 32 | RAM, zeroed in `hush_agent_init` | Second intro for the same `(robot, root)` |
| `g_jobs` | `HUSH_AGENT_JOBS_MAX` 4 | RAM; `pid`/`fd` live here | Lost work; next boot remints `f1` |
| `g_lines` | `HUSH_PRESENCE_LINES_MAX` 16 | RAM, zeroed in `hush_presence_init` | No durable Working ghost **today** because the store is also RAM |
| Store ring | `HUSH_STORE_CAPACITY` 1024 | RAM | Full process restart wipes events |
| `g_id_seq` | unsigned | RAM, **not** zeroed in `init` | New process → `f1` again |

`hush_agent_on_deck`: one intro per `(bot->hex, thread root)` via `hush_agent_intro_seen` / `hush_agent_intro_remember`. That pair is chalk.

`hush_agent_begin_work`: intro (`on_deck`) then `hush_agent_start_grok` (fill job, mint token, spawn, `presence_put` Working). **No claim. Spawn before any durable mark.**

`hush_agent_finish_job` on success publishes **Idle** 30315, does not clear, does not record done. Timeout/disable call `hush_presence_clear` then `finish_job`. Job timeout is `HUSH_AGENT_TIMEOUT_S` **90** in `hush_agent.c` (file-local enum). `HUSH_PRESENCE_STALL_S` is **30** (beat gap to enter Stuck). They are not the same clock.

`hush_agent_job_t` stores `parent_id` as the **conversation root** (`hush_agent_event_root`). It does **not** store the triggering event id (`parent->id`) under a separate field. Delivery and work keys are therefore collapsed into one RAM string while the job is live.

## Home persist pattern (what the ledger must copy)

`hush_home_root` honors `HUSH_HOME`, else `$HOME/.hush`. Agents directory: `hush_home_agents_dir` → `<root>/agents`. `hush_home_ensure` mkdir 0700 of config/agents/skills trees. Tests isolate with `HUSH_HOME`.

Existing persist (`hush_launch_write_vibe_file`): write tmp, `rename`. **No fsync** on that path. Issue #116 requires fsync on claim and on done for the new ledger. Device id must be an install id in hush home, created once, reused. Not hostname. Not reminted every boot.

No SQLite. No new runtime deps. Makefile already links `-lcrypto`.

## Cevent (do not rewrite unless a test is missing)

`hush-c/include/hush_cevent.h`: `HUSH_CEVENT_JSON_MAX = 131072` on purpose. Ring 64. `hush_cevent_ack` advances `g_ack`. `hush_cevent_drops` counts overwrites of events with `seq > g_ack`.

`hush-c/tests/test_cevent.c` already asserts:

- wrap before ack → `drops == 2` after `HUSH_CEVENT_MAX + 2` emits (`"two drops after wrap"`)
- wrap after ack → `drops == 0` (`"acked eviction is not a drop"`)

Header does **not** yet say that `g_ack` has one owner (the PWA). That comment is in scope. `hush_cevent.c` is not, unless a test is actually missing.

## SHA-256 already in tree

`hush_event_compute_id` streams the NIP-01 preimage into `EVP_DigestInit_ex(ctx, EVP_sha256(), NULL)`. Hex encode is static `hush_event_hex_encode` (lowercase `0123456789abcdef`). Presence/wake must use this same OpenSSL SHA-256, not a new library, and must not slice pubkeys.

## Ghost condition (why `d` must stabilize now)

Today a full process restart wipes store and `g_lines`, so there is no durable Working ghost. If a later change persists the 1024 event ring **before** `d` is reconstructible, `hive:f3` remains on disk while the next boot mints `hive:f1`. Stuck does not expire. Private REQ hiding can conceal the orphan from the operator. **Issue order: stable `d` now. Refuse any persist-events-without-stable-`d` patch.**

## Out of scope (confirmed against tree)

- Persisting the 1024 event ring
- Inferring offline from a missing 30315
- Multi-box gossip beyond device id + lease
- SQLite, unbounded seen-sets, kind 44102, NIP-38 kind changes
- Rewriting `hush_cevent.c` for this issue

## Architecture implication (not implemented in this file)

New `hush_wake` module so `hush_agent.c` does not grow another god file. Public headers first. Ledger: bounded slot file under hush home agents dir, magic header, ~256 slots, 32-byte work key = full SHA-256(`robot_hex || ":" || root_hex`), state `empty|intro|claimed|done`, lease unix, device id, trigger id (32-byte decoded event id). `g_lines` stays RAM cache. 30315 stays “what I am doing.” Ledger is the lock.

Lease constant = job timeout 90s, documented next to `HUSH_AGENT_TIMEOUT_S`, **not** `HUSH_PRESENCE_STALL_S`.

## Baseline Bayesian scores (current tree, hoped-for code forbidden)

Formula: `Final = 0.4x + 0.4y + 0.2z`

### 1. Identity & `d` Stability

| Sub | Score | Evidence |
|---|---|---|
| X formula fidelity | 1.0 | `hush_presence_make_d` is `hive:` + token; `test_presence.c` `"d prefix"` expects `hive:job1`. No SHA-256 of robot+root. |
| Y three keys | 2.0 | Token is work `d`, job handle, and (implicit) identity. No delivery key. Phase not in `d` only because there is no phase. |
| Z restart sameness | 1.0 | New process remints `f1`. `hush_agent_init` zeros intros/jobs/lines. Same robot+root cannot reconstruct `d`. |
| **Final** | **1.4** | |

### 2. Ledger & Claim

| Sub | Score | Evidence |
|---|---|---|
| X durability | 0.5 | No wake file, no magic, no device id, no fsync claim path. Home persist exists for vibe JSON only. |
| Y claim rules | 0.5 | `hush_agent_robot_busy` is RAM. No other-device deny, no lease, no done+trigger replay. |
| Z single source of truth | 2.0 | Lock is `g_jobs` + `g_lines`. 30315 is derived from token chalk. Three-way disagreement is untestable because there is no ledger. |
| **Final** | **0.8** | |

### 3. Presence / Intro / Privacy

| Sub | Score | Evidence |
|---|---|---|
| X once-only intro | 3.0 | `hush_agent_intro_seen` works inside one process (`check_agent.sh` greps it). `hush_agent_init` zeros the table → second intro after restart. |
| Y work path | 3.0 | Spawn then Working. Finish → Idle, not done+clear. Kill/timeout does clear. Lease does not exist. Stall 30s ≠ job 90s. |
| Z operator view | 8.5 | `hush_presence_req_ok` + `format_json` already split REQ vs JSON. `test_presence.c` checks them on **separate** asserts, not one private-line-still-in-JSON case. |
| **Final** | **5.2** | |

### 4. C11 Legibility & Test Gauntlet

| Sub | Score | Evidence |
|---|---|---|
| X write-legible-c | 6.0 | Presence module is close (bounds, asserts on some leaves). Agent is a god file. No wake module. `make_d` takes a token, collapsing keys. |
| Y required tests | 2.0 | `test_presence.c` covers slugs, replace, Idle expiry, REQ hide. Missing restart `d`, ledger, two-device, lease-vs-Stuck, done replay, combined private JSON. |
| Z cevent contract | 8.5 | Drops/ack wrap tests exist in `test_cevent.c`. `g_ack` one-owner is **not** documented in `hush_cevent.h`. JSON_MAX untouched. |
| **Final** | **5.0** | |

No domain is ≥ 9.0. Keep going. Chalk is not a mark. Prove the pulley mark survives `init()`.
