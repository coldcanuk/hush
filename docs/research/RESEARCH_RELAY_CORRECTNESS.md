# RESEARCH_RELAY_CORRECTNESS

Scope: verification of an external technical review of Hush 0.0.1 @ `5f67c65eb`
(relay core, store, conversation tables, wire parser) against the code in
`worktrees/review-hardening`. Source only: no build, no tests, no runtime probing.
All paths below are relative to the repo root unless the file is named directly.

Evidence base read in full or in the cited ranges:
`hush-c/src/hush_store.c`, `hush-c/src/hush_proto.c`, `hush-c/src/hush_relay.c`,
`hush-c/src/hush_relay_main.c`, `hush-c/src/hush_intel.c`, `hush-c/src/hush_agent.c`
(cited ranges), `hush-c/src/hush_presence.c` (cited ranges), `hush-c/src/hush_http.c`
(cited ranges), `hush-c/src/hush_json_read.c`, `hush-c/src/hush_filter.c`,
`hush-c/src/hush_wake.c` (cited ranges), `hush-c/src/hush_launch.c` (cited ranges),
plus `hush-c/include/*.h` and the tests named in section (b).

---

## (a) Claim-by-claim

Verdicts: **verified** / **partially verified** / **refuted**. "Partially" is used
only where the claim's stated mechanism is wrong or over-broad while its
observable consequence holds.

### 1. Every message rewrites and fsyncs the whole store on the poll loop — **verified**

`hush_store_insert` always saves when persistence is on, on both paths
(`hush-c/src/hush_store.c:136-149`):

    hush_status_t hush_store_insert(hush_store_t *store, const hush_event_t *ev)
    {
        ...
        if (hush_store_replace_addressable(store, ev)) {
            if (store->persist_on)
                (void)hush_store_save(store);      /* line 141-142 */
            return HUSH_OK;
        }
        hush_store_write(store, ev);
        if (store->persist_on)
            (void)hush_store_save(store);          /* line 146-147 */
        return HUSH_OK;
    }

`hush_store_save` (`hush-c/src/hush_store.c:527-571`) rewrites **every** record into
`store.ring.tmp`, fsyncs the file, renames, then fsyncs the parent directory:

    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, HUSH_STORE_FILE_MODE);   /* 540 */
    ... for (i = 0; i < store->count; i++) hush_store_write_event(...)     /* 551-557 */
    if (hush_store_fsync_fd(fd) != HUSH_OK) { ... }                        /* 558 */
    if (rename(tmp, store->persist_path) != 0) { ... }                     /* 567 */
    return hush_store_fsync_dir(store->persist_path);                      /* 571 */

Ring size and file name: `HUSH_STORE_CAPACITY = 1024` (`hush-c/include/hush_store.h:12`),
`#define HUSH_STORE_FILE "store.ring"` (`hush-c/include/hush_store.h:15`).

Single-threaded: inserts run inside `hush_relay_pump` (`hush-c/src/hush_relay.c:715-742`),
via `hush_service_clients` (456-484) -> `hush_on_bytes` (486-516) ->
`hush_on_nostr_line` (518-530) -> `hush_handle_event_msg` (545-554), and via the HTTP
handler at 499. `hush_agent_poll` / `hush_intel_poll` run on the same loop (728-730).

Persistence switch (`hush-c/src/hush_store.c:298-310`):

    home = getenv(HUSH_HOME_ENV);            /* "HUSH_HOME"       hush_home.h:13 */
    if (home != NULL && home[0] != '\0') return 1;
    cfg  = getenv(HUSH_HOME_ENV_CONFIG);     /* "HUSH_CONFIG_DIR" hush_home.h:14 */
    if (cfg != NULL && cfg[0] != '\0') return 0;
    return 1;

So the disabling env var is `HUSH_CONFIG_DIR`, and only when `HUSH_HOME` is unset
or empty; if both are set, persistence stays **on** (several integration checks set
both, e.g. `hush-c/tests/check_agent.sh:36-37`). There is **no CLI flag**:
`hush_parse_args` accepts only `--open/--no-open/--quit/--close/-h/--help` and a port
(`hush-c/src/hush_relay_main.c:59-83`). Persistence is on by default
(`hush_store_may_persist` final `return 1`, line 309).

Exact insert path for one human UI message (note + presence + trail):

1. POST /api/event -> `hush_http_serve_post` (`hush-c/src/hush_http.c:721-748`);
   `hush_http_parse_note` 729, `hush_http_resolve_conversation` 730,
   `hush_event_compute_id` 736, **note insert 737**.
2. `hush_intel_consider` 743 -> for each p-tag `hush_intel_handle_robot`
   (`hush-c/src/hush_intel.c:637`) posts the durable ack `hush_intel_post_line`
   -> **insert** at `hush-c/src/hush_intel.c:486` (plus deny/recap notes on other paths).
3. `hush_http_note_presence` 744 (def 776-787) -> `hush_presence_publish`
   (`hush-c/src/hush_presence.c:153-176`) -> `hush_presence_insert_pair`
   (465-504) -> **line insert 493 (kind 30315)** and **trail insert 496 (kind 1038)**.

Minimum three full-store rewrites + fsync pairs per plain human message
(three `hush_store_insert` calls: 737, 493, 496), more when a robot is mentioned.
A robot reply adds `hush_agent_publish_reply` -> insert (`hush-c/src/hush_agent.c:2948`)
plus wake-claim gossip insert (`hush-c/src/hush_wake.c:1015`).

### 2. HUSH_MAX_CLIENTS=16, no idle timeout, no output queue, silent short/EAGAIN write — **verified**

- `HUSH_MAX_CLIENTS = 16` (`hush-c/src/hush_relay.c:39`). Accept beyond that closes the
  fd and returns FULL (`hush_accept_new`, 423-444, close at 442).
- `struct client` (81-89) holds only input state — `fd`, `buf`, `len`, `is_http`,
  `has_sub`, `sub_id`, `filter`. No output buffer, no last-activity timestamp;
  a repo-wide grep for client expiry/timeout finds none.
- pollfds request `POLLIN` only (686-704); there is no POLLOUT path.
- `hush_send_str` is `void` and stops on any non-positive write
  (`hush-c/src/hush_relay.c:532-543`):

      static void hush_send_str(int fd, const char *s)
      {
          size_t n = strlen(s);
          size_t off = 0;
          while (off < n) {
              ssize_t w = write(fd, s + off, n - off);
              if (w <= 0)
                  break;
              off += (size_t)w;
          }
      }

  The socket is non-blocking (`hush_set_nonblock`, 300-306; applied to the accepted fd
  at 431), so `write` can return `-1/EAGAIN`. Callers are void-blind: 552 (OK ack),
  571 (REQ result), 574 (EOSE), 590 (fanout). Nuance: a positive short write **is**
  retried in the loop; the defect is that EAGAIN/EPIPE (or a retry that then EAGAINs)
  terminates the frame silently. With no output queue the remainder of the JSON line is
  lost and the next frame is appended to a truncated line.

### 3. Wire parser gaps — **verified**

Tags and created_at are dropped; created_at is hard-coded
(`hush-c/src/hush_proto.c:120-135`):

    static hush_status_t hush_parse_event_object(const char *s, hush_event_t *out)
    {
        ...
        const char *idp = strstr(s, "\"id\":\"");
        if (idp) sscanf(idp + 6, "%64[^\"]", out->id);
        ... pubkey ... kind ... content ...
        out->created_at = 1720000000;        /* line 133 */
        return HUSH_OK;
    }

No `tags` parsing at all, so every wire event has `tag_count == 0`
(fanout/REQ still work because `hush_filter_match` only uses tags when the filter
declares them, `hush-c/src/hush_filter.c:33-48`).

Filter fields: only `kinds` (all values, loop 142-151), `authors` first value only
(152-160) and `#h` first value only (161-171). There is no `'"ids"'`, `'"since"'`,
`'"until"'` lookup in the function. The struct has all of them
(`hush-c/include/hush_filter.h:22-27`), and `hush_filter_match` uses them
(`hush-c/src/hush_filter.c:19-31`), so they are simply always zero -> no-op.

AUTH/COUNT: `hush_proto_parse_line` (`hush-c/src/hush_proto.c:15-58`) compares only
`EVENT`, `REQ`, `CLOSE`; everything else becomes `HUSH_MSG_UNKNOWN` (56-57) and is
silently ignored by `hush_on_nostr_line` (518-530). `HUSH_MSG_COUNT` is declared in
the enum (`hush-c/include/hush_proto.h:15`) but is never assigned anywhere (repo grep).

Kind 5/7: no delete or reaction handling exists in `hush-c/src` (repo grep for
deletion/reaction/kind 5/7 finds only the roster HTTP `delete` action,
`hush-c/src/hush_http.c:1074`, and a comment at `hush-c/src/hush_intel.c:655`).
An EVENT of kind 5/7 would be stored and fanned out like any other kind.

Line-protocol dispatch (the strongest form of this claim):
`hush-c/src/hush_relay.c:545-554`:

    static void hush_handle_event_msg(struct client *c, const hush_client_msg_t *msg)
    {
        char line[HUSH_BUF_SZ];
        (void)hush_store_insert(g_store, &msg->event);
        (void)hush_wake_ingest(&msg->event);
        if (hush_proto_format_ok(msg->event.id, 1, "", line, sizeof(line), NULL) == HUSH_OK)
            hush_send_str(c->fd, line);
        hush_fanout(&msg->event);
    }

No `hush_intel_consider` and no `hush_agent_consider`. The only production caller of
`hush_intel_consider` is the HTTP POST at `hush-c/src/hush_http.c:743`; `hush_intel_poll`
only flushes expired holds (`hush-c/src/hush_intel.c:134-153`) and never ingests store
events. So wire events are stored/acked/fanned but never reach intel/agent dispatch.

### 4. Hold/follow table overflow returns slot 0 — **verified** (both)

`hush-c/src/hush_intel.c:414-436`:

    for (i = 0; i < (size_t)HUSH_INTEL_HOLD_MAX; i++) {
        if (g_holds[i].live) continue;
        memset(&g_holds[i], 0, sizeof(g_holds[i]));
        g_holds[i].live = 1;
        ... return &g_holds[i];
    }
    return &g_holds[0];                          /* line 435 */

`HUSH_INTEL_HOLD_MAX = 8` (`hush-c/include/hush_intel.h:12`; table `hush_intel.c:47`).
The caller does not NULL-check and immediately mutates the returned slot
(`hush-c/src/hush_intel.c:669-670`): `hold = hush_intel_take_hold(...);`
`hush_intel_fold_note(hold, ev);`. Because a hold is identified by the triple
`(channel, root, robot)` (`hush_intel_find_hold`, 397-412), the overflow return binds
a different conversation's live hold to this robot/root and folds foreign notes into it.

`hush-c/src/hush_agent.c:3602-3622` (claim's line 3618 exact):

    for (i = 0; i < (size_t)HUSH_AGENT_FOLLOW_MAX; i++) {
        if (!g_follow[i].live) {
            memset(&g_follow[i], 0, sizeof(g_follow[i]));
            g_follow[i].live = 1;
            hush_agent_copy(g_follow[i].root, sizeof(g_follow[i].root), root);
            return &g_follow[i];
        }
    }
    memset(&g_follow[0], 0, sizeof(g_follow[0]));   /* line 3618 */
    g_follow[0].live = 1;
    hush_agent_copy(g_follow[0].root, sizeof(g_follow[0].root), root);
    return &g_follow[0];                            /* line 3621 */

Worse than intel: the `memset` destroys an in-flight wave (`at`, `nnext`, `next[]`,
`group[]`, `inflight`). `HUSH_AGENT_FOLLOW_MAX = 8` (`hush-c/src/hush_agent.c:56`),
table at 316. Callers dereference without a NULL check (`hush_agent_prepare_group`,
3096-3103; `hush_agent_follow_push`, 3687-3700).

### 5. `hush_agent_consider` has zero callers; `hush_agent_reset_follow` never runs — **verified**

Definition `hush-c/src/hush_agent.c:677-692`; it is the **only** caller of
`hush_agent_reset_follow` (line 686; definition 4084-4095). Repository grep for
`hush_agent_consider` returns the declaration (`hush-c/include/hush_agent.h:25-26`),
the definition, and docs only — no call in `hush-c/src` or `hush-c/tests`.
The live HTTP path calls `hush_intel_consider` (`hush-c/src/hush_http.c:743`), which
routes to `hush_agent_mention` (`hush-c/src/hush_intel.c:566` and 663) and therefore
bypasses `reset_follow`. Consequence: stale follow slots are never reset per new human
request.

### 6. Silent start failures + unconditional wave inflight++ — **partially verified**

Silent for the three named cases:

- Job table full: `hush_agent_start_grok` returns `HUSH_ERR_FULL` with no note
  (`hush-c/src/hush_agent.c:2189-2191`, `hush_agent_find_slot` 868-881; jobs max 4,
  line 30).
- Ledger full: `hush_wake_claim` -> `HUSH_ERR_FULL` (`hush-c/src/hush_wake.c:209-211`,
  `HUSH_WAKE_SLOT_MAX = 256`, `hush-c/include/hush_wake.h:17`).
- Claim denied: `hush_wake_claim` -> `HUSH_ERR_DENIED` (`hush-c/src/hush_wake.c:206-207`,
  documented contract `hush-c/include/hush_wake.h:61-65`).

`hush_agent_begin_work` emits `JOB_START` only on success
(`hush-c/src/hush_agent.c:3816-3818`), and `start_grok`'s failure path just
`hush_agent_release_line` + `hush_agent_close_job` + `return` (2203-2207); nothing is
posted to the thread.

Refuted part: "**any** start failure" is over-broad. Three failure classes do post a
visible note:
- no provider runtime (`can_start` false) -> `hush_agent_note_no_runtime`
  (3811-3813; text at 1235-1238);
- guidance load failure -> `hush_agent_report_guidance` (2195-2198; 2221-2227);
- turn cap -> `hush_agent_nudge_chaperon` (3801-3803; 4185-4214).

Wave inflight (verified): `hush_agent_follow_kick` calls `hush_agent_begin_work(&in)`
then increments unconditionally (`hush-c/src/hush_agent.c:3995-3996`):

    hush_agent_begin_work(&in);
    slot->inflight++;

The decrement only happens when a stored non-human kind-1 note for that root triggers
`hush_agent_on_posted` -> `hush_agent_follow_kick` (700-710, 3960-3963). If
`begin_work` returned early (turns full, no runtime, table full, claim denied, spawn
failure), no note is ever posted, so `inflight` never returns to 0 and the remaining
waves for that root never dispatch.

### 7. Replies > 4096 B become failures — **verified** (mechanism corrected)

The cap is `HUSH_EVENT_MAX_CONTENT = 4096` (`hush-c/include/hush_event.h:14`). The
worker that captures provider output rejects anything at or over 4097 **before**
writing to the relay pipe (`hush-c/src/hush_agent.c:1891-1902`):

    static hush_status_t hush_agent_capture_reply(char *out, size_t outsz, char *capture,
                                                 const char *provider)
    {
        ...
        size_t len = strlen(capture);
        if (len >= outsz) return HUSH_ERR_FULL;     /* 1898-1899; outsz = 4097 */
        memcpy(out, capture, len + 1);
        return len == 0 ? HUSH_ERR_PARSE : HUSH_OK;
    }

`hush_agent_run_worker` then exits 127 without writing
(`hush-c/src/hush_agent.c:1915-1920`):

    char reply[HUSH_EVENT_MAX_CONTENT + 1] = {0};
    if (hush_agent_capture_reply(reply, sizeof(reply), capture, job->provider) != HUSH_OK)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    /* PIPE_BUF is at least one event on this Linux relay; incomplete writes fail closed. */
    size_t len = strlen(reply);
    if (write(output_fd, reply, len) != (ssize_t)len) _exit(HUSH_AGENT_EXEC_FAILURE);

The relay sees EOF with an empty `job->out` -> `hush_agent_finish_job` treats it as a
failure (2885-2886) -> `hush_agent_note_failure` posts
"%s did not return a usable reply through %s. Check that provider's login, model, and
connection, then send your request again." (2853-2873).

Correction to the claim's parenthetical: the `read_job` path that erases `out`
(`job->out[0] = '\0'` at 2967) requires `hush_agent_read_chunk` to return `HUSH_ERR_IO`
(2972-2992), which needs **more** than 4096 bytes already in the relay pipe
(`room == 0` then `read` 1 byte, 2978-2991) — the worker never writes more than 4096
(1918-1920), so for the oversized case `out` was never filled rather than erased. The
observable result (pipe closed, no usable reply, failure note) is as claimed.

Existing boundary coverage: `hush-c/tests/check_collaboration.py:130-131` returns
exactly 4096 for mode "full" and 4097 for "oversized"; 214-218 asserts the 4096 reply
is stored intact; 309-316 asserts the 4097 reply produces "did not return a usable
reply".

### 8. cooldown not enforced; max_jobs first-p-tag only; robot_hops boolean — **verified** (all three)

- `cooldown_s` is defaulted (`hush-c/src/hush_launch.c:1008`), read from storage
  (2589-2593), validated (2917, `hush_launch_cooldown_ok` 2880-2889), serialized in
  status (`'"cooldown_s":%d'`, 1762-1768) and the HTTP policy key (2247-2248). No
  enforcement read exists: repo grep for `cooldown` in `hush-c/src` finds only
  `hush_launch.c` and `hush_http.c:1629` (API setter). Neither `hush_intel.c` nor
  `hush_agent.c` reads it; only `burst_ms` is enforced
  (`hush_intel_burst_ready`, `hush-c/src/hush_intel.c:571-582`).
- `max_jobs` gate (`hush-c/src/hush_intel.c:607-608`):

      if (hush_intel_jobs_busy() >= ch->max_jobs &&
          hush_intel_is_lead_p(launch, ev, hex)) {
          hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_JOBS);
          return 1;
      }

  `hush_intel_is_lead_p` (680-697) returns `strcmp(found, hex) == 0` at the **first**
  resolvable `p` tag (692-695); robots that are not the first resolvable p-tag bypass
  the gate entirely. Also the counter is global-queue occupancy, not per-channel:
  `hush_intel_jobs_busy` (444-457) counts `'{'` characters in `hush_agent_status`.
- `robot_hops` is coerced to a boolean on load (`hush-c/src/hush_launch.c:2594-2595`):

      ch->robot_hops = hush_launch_take_named_int(json, idx,
                                                  "channel_robot_hops", 0) != 0;

  validation accepts only 0 or 1 (2921-2922), and the sole consumer tests zero
  (`hush-c/src/hush_intel.c:603`, `ch->robot_hops == 0` -> deny hop). The field is
  `int` in both structs (`hush-c/include/hush_launch.h:80,97`) and the UI already sends
  0/1 (`hush-c/demo/index.html:5804`). It is a flag, not a hop counter.

### 9. Three JSON parsers with inconsistent decoding — **verified**

1. Structured parser: `hush-c/src/hush_json_read.c` — `hush_json_lookup` (87-105,
   validates the whole document), `hush_json_decode` (107-123), full escape/Unicode
   handling including surrogate pairs (`hush_json_take_escape`, 327-356).
2. `strstr`/`sscanf` parser: `hush-c/src/hush_proto.c:120-172` (claim 3 evidence).
   It performs no unescaping; `content` stops at the first `'"'` byte and embedded
   `\n` / `\"` are copied literally.
3. Ad-hoc HTTP field search that drops backslashes:
   `hush-c/src/hush_http.c:533-545` (`hush_json_unescape_copy`) and 547-569
   (`hush_json_field`):

      while (*src != '\0' && *src != '"' && i + 1 < dstsz) {
          if (*src == '\\' && src[1] != '\0')
              src++;                 /* drops the backslash, copies the next byte */
          dst[i++] = *src++;
      }

   No `\uXXXX` decoding, no rejection of malformed escapes; falls back to
   `hush_json_bare_field` (589). It is used pervasively for /api fields (e.g. `kind`
   766, `slug` 909, `channel` 917, `system_prompt` 1568 uses the structured parser
   instead). Within one `/api/event` request, `content`/`channel` are decoded by
   parser 1 (756-761) while `kind` is decoded by parser 3 (766), so the same body is
   read by two decoders with different escape semantics.

### Call-site inventories (requested)

`hush_store_insert` — production:
`hush-c/src/hush_store.c:126` (reload in `hush_store_persist_open`; `persist_on` is 0
until line 132, so no save-during-load), `hush-c/src/hush_relay.c:549`,
`hush-c/src/hush_http.c:737,2467`, `hush-c/src/hush_presence.c:201,221,493,496`,
`hush-c/src/hush_wake.c:1015`, `hush-c/src/hush_agent.c:1152,2948`,
`hush-c/src/hush_intel.c:486`, `hush-c/src/hush_roster.c:711,730`,
`hush-c/src/hush_launch.c:1403,1419,1442`.
Tests: `hush-c/tests/test_store.c:66,74,80,111`;
`hush-c/tests/test_intel.c:126,149,171,194,227,241`;
`hush-c/tests/test_chan_rails.c:228,261,316,324`.

`hush_send_str` — declaration `hush-c/src/hush_relay.c:130`, definition 532-543,
call sites 552, 571, 574, 590. No test references it.

`hush_event_validate` — declaration `hush-c/include/hush_event.h:38`, definition
`hush-c/src/hush_event.c:95`, call sites **tests only**:
`hush-c/tests/test_event.c:88,90,93`. Zero production callers, so wire events are
never structurally validated before insert (a line-protocol `kind` is whatever
`atoi` produced, `hush-c/src/hush_proto.c:130`).

---

## (b) Fix surface

| # | Defect | Function(s) that must change | Existing test that covers current behavior (or "none") |
|---|--------|------------------------------|--------------------------------------------------------|
| 1 | Whole-store snapshot + 2 fsyncs per insert | `hush_store_insert` (`hush_store.c:136`), `hush_store_save` (527), `hush_store_may_persist` (298); flush point in `hush_relay_pump` (`hush_relay.c:715`) | `test_store.c` (persist/reload/cap semantics: 84,88-95,113-119); integration checks that set `HUSH_HOME` persist (e.g. `check_agent.sh:36-37`) |
| 2 | Silent truncated writes / no output queue / no idle timeout | `hush_send_str` (532) -> status; `struct client` (81) add out state; `hush_fill_pollfds` (686) add `POLLOUT`; `hush_service_clients` (456) drain; callers 545,556,577 | none (no test touches `hush_send_str` or >1 client) |
| 3 | Parser gaps: tags/created_at, filter ids/since/until/extra values, AUTH/COUNT, kind 5/7, no intel dispatch from wire | `hush_proto_parse_line` (15), `hush_parse_event_object` (120), `hush_parse_filter_object` (137); `hush_on_nostr_line` (`hush_relay.c:518`) for AUTH/COUNT; `hush_handle_event_msg` (`hush_relay.c:545`) to call `hush_intel_consider`/`hush_agent_consider`; kind 5/7 policy in `hush_store_insert` or a dispatcher | `test_proto.c:22-28` (REQ kinds/sub-id only); none for tags/created_at/filters/AUTH/COUNT/kinds 5-7 |
| 4a | Hold overflow returns `&g_holds[0]` | `hush_intel_take_hold` (`hush_intel.c:414`), caller `hush_intel_handle_robot` (669) | `test_intel.c` (1-2 holds only); none over 8 |
| 4b | Follow overflow returns `&g_follow[0]` and clobbers live slot | `hush_agent_follow_take` (`hush_agent.c:3602`), callers `hush_agent_prepare_group` (3096), `hush_agent_follow_push` (3687) | `test_chan_rails.c` (one group at a time); none over 8 |
| 5 | `hush_agent_consider` dead -> `hush_agent_reset_follow` never runs | wire a caller (`hush_http.c:743`, `hush_relay.c:545`) or delete both; `reset_follow` (`hush_agent.c:4084`) | none calls `hush_agent_consider`; `test_intel.c:242` drives `hush_agent_on_posted` directly |
| 6 | Silent failures: job table full, ledger full, claim denied; unconditional `inflight++` | `hush_agent_start_grok` (2186) post a note on FULL/DENIED; `hush_agent_begin_work` (3791) return started/not; `hush_agent_follow_kick` (3939) increment only on start (3995-3996) | `check_collaboration.py:308-316` (provider-failure notice only); `test_chan_rails.c:327-331` (turn-cap notice) |
| 7 | Reply >4096 fails | `hush_agent_capture_reply` (1891), `hush_agent_run_worker` (1904), `hush_agent_read_job`/`hush_agent_read_chunk` (2960/2972), `hush_agent_note_failure` message (2853) | `check_collaboration.py:214-218` (4096 stored) and 309-316 (4097 -> failure notice) pin the current boundary |
| 8a | `cooldown_s` never enforced | `hush_intel_policy_blocks` (`hush_intel.c:584`); needs a per-channel/per-robot last-run timestamp (holds already carry `last`, 634) | `test_launch.c:259-294` (round-trip only); `check_launch.sh:414` (API accepts it); no enforcement test |
| 8b | `max_jobs` only for first resolvable p-tag; global counter | `hush_intel_policy_blocks` (584) and `hush_intel_is_lead_p` (680); `hush_intel_jobs_busy` (444) if per-channel counting is intended | `test_intel.c:140-163`, `test_chan_rails.c:247-280` set the field but never exceed it |
| 8c | `robot_hops` boolean | `hush_launch.c:2594-2595`, validation 2921-2922, consumer `hush_intel.c:603`; struct fields `hush_launch.h:80,97` | `test_intel.c:186-196` (hop-0 deny); `test_launch.c` policy round-trip |
| 9 | Three decoders | Unify `hush_json_field`/`hush_json_unescape_copy` (`hush_http.c:533,547`) and `hush_proto.c:120-172` on `hush_json_lookup`/`hush_json_decode` (`hush_json_read.c:87,107`) | `test_json_read.c` (decode/escape/fail cases), `test_json.c` (escape), `test_proto.c` (parser smoke) |

Additional adjacent defects noticed while verifying (not in the review list, no test):
- A single nostr line longer than `HUSH_BUF_SZ - 1` (32767 B) is dropped mid-frame:
  `read(c->fd, c->buf + c->len, HUSH_BUF_SZ - c->len - 1)` becomes a 0-byte read once
  the buffer is full, and `n <= 0` drops the client (`hush-c/src/hush_relay.c:475-479`).
- `hush_store_insert` ignores `hush_store_save`'s status (141-147), so a failed
  snapshot is invisible to the HTTP caller (still answers 200/OK).

---

## (c) Invariants / contracts at declaration sites

### Table-overflow clobbers

- **Intel holds.** `HUSH_INTEL_HOLD_MAX = 8` (`hush-c/include/hush_intel.h:12`) is a
  bounded cache keyed by the triple `(channel, root, robot)` — the identity predicate
  is explicit in `hush_intel_find_hold` (`hush-c/src/hush_intel.c:397-412`, all three
  `strcmp`s). `hush_intel_take_hold` is `static` with no documented failure mode
  (forward decl 77-79) and its only caller treats the result as non-NULL
  (669-670). Any fix must either return `NULL` and make `hush_intel_handle_robot` skip
  the fold (and, for `HUSH_LAUNCH_REPLY_CONFIRM`, decide whether the ack still posts),
  or explicitly evict a dead/oldest hold *before* reusing a slot so the identity
  invariant is never silently violated. The header contract for the whole module is
  "channel leash + burst hold in front of hush_agent" (`hush_intel.h:1`), and
  `hush_intel_consider` is documented to hold or forward (22-24); nothing promises a
  fixed number of concurrent conversations, so failing closed is compatible with the
  documented surface.
- **Follow slots.** `HUSH_AGENT_FOLLOW_MAX = 8` (`hush-c/src/hush_agent.c:56`), table
  `g_follow` (316). `hush_agent_follow_take` (3602) is not in the public header
  (`hush-c/include/hush_agent.h` exposes only consider/mention/on_posted/poll/status/
  fixup); its callers (`hush_agent_prepare_group` 3090-3104, `hush_agent_follow_push`
  3674-3701) fully assume a fresh zeroed slot and immediately set channel/human/ask/
  mode. The overflow branch additionally zeroes slot 0, so a fix must return a
  distinguishable failure and callers must post a visible "cannot queue" note (or
  complete the current wave) instead of proceeding. The repo already has the
  fail-closed pattern to copy: `hush_presence_take_slot` returns `NULL`
  (`hush-c/src/hush_presence.c:445-454`) and `hush_presence_publish` maps that to
  `HUSH_ERR_FULL` (171-174).
- Both tables are indexed by a *derived* root id (`hush_agent_event_root` /
  `hush_intel_event_root`), so eviction policy must preserve the "one live slot per
  root" property, not merely free a slot.

### Partial writes / durability

- `hush_send_str` is `void` by declaration (`hush-c/src/hush_relay.c:130`), so error
  reporting requires an interface change; `struct client` (81-89) has no output
  buffer or activity timestamp, and `hush_fill_pollfds` (686-704) never asks for
  `POLLOUT`. `hush_service_clients` (456-484) only inspects `POLLIN|POLLHUP|POLLERR`
  (465) and drops a client when `read <= 0` (476-479). A queue fix therefore touches
  the struct, the pollfd builder, the service loop, and the four send sites.
- The in-repo reference for correct non-blocking writes is `hush_http_write_all`
  (`hush-c/src/hush_http.c:484-497`): retry on positive writes, retry `EINTR`, poll
  `POLLOUT` with timeout on `EAGAIN`/`EWOULDBLOCK`, return `HUSH_ERR_IO` otherwise.
  `hush_store_write_bytes` (`hush-c/src/hush_store.c:343-358`) loops on partial writes
  but treats `EINTR` as `HUSH_ERR_IO` (`w <= 0` -> return), unlike the HTTP helper.
- Snapshot failure is swallowed: `hush_store_insert` discards `hush_store_save`'s
  status (141-147), and `hush_store_save` itself cleans up on every failure path
  (`unlink(tmp)` at 549, 554, 561, 565, 568). If a fix moves to append/debounce, it
  must keep the atomic-replace invariant: write tmp -> `fsync(fd)` (558) -> `close` ->
  `rename` (567) -> `fsync(parent dir)` (571) — and `hush_store_destroy` saves on
  shutdown only when `persist_on` (60-65).
- Worker-side reply writes are bounded to `HUSH_EVENT_MAX_CONTENT` and rely on
  `PIPE_BUF >= 4096` for all-or-nothing writes
  (`hush-c/src/hush_agent.c:1918-1920`); the reader `hush_agent_read_chunk` treats
  "count > 0 but count > room" as a hard error and closes the fd (2978-2991), which is
  the overflow contract that makes >4096 impossible rather than truncated.
- `hush_store_persist_open` reloads via `hush_store_insert` **before** setting
  `persist_on` (`hush-c/src/hush_store.c:118-134`), which is what keeps load from
  re-saving on every record; any refactor that enables persistence earlier turns reload
  into O(n) snapshots.

---

## (d) Open questions

1. Durability intent: is per-insert whole-file fsync a deliberate MVP guarantee, or is
   debounce/append acceptable? `test_store.c` asserts file existence and reload
   semantics only, not fsync frequency, so the intent is not pinned by tests.
2. Test switch: should persistence be disabled by a first-class flag? Today
   `HUSH_CONFIG_DIR` disables it **only when `HUSH_HOME` is unset**
   (`hush_store.c:303-308`), and several integration checks set both, so they persist.
   Is that intended?
3. Slow readers: is the expected client set only the local browser (drop on EAGAIN is
   acceptable), or must the relay tolerate slow/partial readers? No test exercises a
   stalled reader, so there is no behavioral contract to preserve.
4. kind 5/7: should unsupported kinds be ignored (current), or answered with
   `["OK", id, false, ...]` / a NOTICE? They are currently stored and fanned out
   unchanged.
5. `max_jobs`: per-channel or global? `hush_intel_jobs_busy` counts the whole job
   table (444-457) while the setting lives on a channel. Should the gate apply to every
   mentioned robot rather than the first resolvable p-tag?
6. `cooldown_s`: per-robot or per-channel, and how does it interact with
   `burst_ms`, which is already enforced (`hush_intel_burst_ready`)? Nothing records
   a last-reply time for it today.
7. `robot_hops`: was a hop *count* ever intended (the name and the `int` fields), or
   is the boolean "robots may reply to robots" the final semantics? The launch loader
   and validator already collapse it to 0/1.
8. `hush_event_validate`: zero production callers. Was validation intentionally
   replaced by per-provider parsing, or should it run at the wire/insert boundary?
9. Reply cap: is a >4096 reply supposed to fail (current, test-pinned at
   `check_collaboration.py:214-218`) or be truncated/prefixed and still posted?
10. Dead `hush_agent_consider`: deleting it also deletes the only `reset_follow` call;
    wiring it in changes follow-slot lifetime for every HTTP post. Which direction is
    intended? (`hush_agent.h:24` still advertises "Starts a reply for each robot p-tag"
    as public API.)
11. Should the intel dispatch for wire events use the same entry point as HTTP
    (`hush_intel_consider`), given that wire events have `tag_count == 0` today and
    therefore no `h` channel tag to key holds on
    (`hush_intel_event_channel` falls back to a constant, `hush_intel.c:179-195`)?

## Unverifiable from source alone

Nothing in the claim list was left unverifiable. Claims about runtime symptoms
(fsync latency, corrupted frames actually observed by a slow client) were verified by
code inspection only, since this task forbids building or running the binary.
