# RESEARCH — Thread memory, providers, PWA polling, streaming, cancel, budgets

Scope: verify an external technical review of Hush 0.0.1 @ 5f67c65eb against the actual
`gb/review-hardening` checkout, then research (a) incremental SSE parsing inside the
existing single-threaded poll loop, (b) pushing partial output to the PWA, (c) cancellation
of fork/exec jobs, (d) durable thread briefs, (e) token/cost accounting.

All code evidence paths are relative to `hush-c/` unless stated. Line numbers are from
this worktree at the time of writing.

---

## (a) Claim verification table

| # | Claim (external review) | Verdict | Key evidence |
|---|-------------------------|---------|--------------|
| 1 | Memory is a six-note snippet window: `HUSH_AGENT_THREAD_MAX = 6`; opening note + last 6 work notes at 384 bytes each + current message; no summarization/retrieval/durable per-thread state | **Verified** (two precision notes) | `src/hush_agent.c:36,42`, `1504-1532`, `1535-1566`, `1743-1752` |
| 2 | Robot context files are RAM-only, lost on restart | **Verified** | `include/hush_roster.h:52-59` (comment 56-58); `src/hush_roster.c:981-988`; `src/hush_agent.c:1581-1621` |
| 3 | If the thread root falls out of the 1024-event store ring, context degrades to current message only | **Partially verified** | `include/hush_store.h:12`; `src/hush_store.c:225-241`; `src/hush_agent.c:1546-1566` — real degradation is "no opening note + header + current message", and root-e-tagged replies still match |
| 4 | API providers get ONE flattened user message; `"stream":false`; no tools, no JSON mode, no multi-turn `messages[]` | **Verified** (OpenAI-family body has a system/developer message *plus* one user message) | `src/hush_inference.c:150-177`; CLI path `src/hush_agent.c:1957-2104` |
| 5 | No streaming, no cancel, no budget; one blob after up to 90 s; POST /api/fixup blocks the relay loop up to 90 s | **Verified** (ordinary replies never block the loop — curl runs in a worker; CLI output *does* arrive incrementally in the worker but is forwarded as one write) | `include/hush_agent.h:14`; `src/hush_agent.c:1904-1923,2240-2249,2994-3000`; `src/hush_http.c:51-52,1867-1912,2053`; `src/hush_relay.c:486-503,715-742` |
| 6 | PWA polls /api/status, /api/events, /api/session, /api/presence every 1000 ms; `hush_cevent` ring is never consumed by the PWA | **Verified** | `demo/index.html:5721-5746,6124-6125`; route at `src/hush_http.c:325-328,854-878`; no `chan-events`/cevent reference in index.html |

### Claim 1 — six-note window (verified)

`src/hush_agent.c:36,42`
```c
HUSH_AGENT_THREAD_MAX = 6,
...
HUSH_AGENT_SNIP_MAX = 384,
```

Assembly path: `hush_agent_fill_job_note()` → `hush_agent_fill_thread()` → `hush_agent_collect_thread()`
+ `hush_agent_walk_thread()`:

`src/hush_agent.c:1504-1521` (collection — only kind-1 notes in the ring whose id/e-tag matches
the root, excluding trigger and root, and only "work notes"):
```c
if (event.kind != HUSH_AGENT_KIND_NOTE || strcmp(event.id, trigger) == 0 ||
    strcmp(event.id, root) == 0 || !hush_agent_event_is_root(&event, root) ||
    !hush_agent_is_work_note(event.content))
    continue;
hush_agent_push_thread(out, &found, &event);
```

`src/hush_agent.c:1523-1532` (sliding window — oldest is dropped when the seventh arrives):
```c
if (*count == (size_t)HUSH_AGENT_THREAD_MAX) {
    memmove(out, out + 1, (*count - 1) * sizeof(*out));
    --*count;
}
```

`src/hush_agent.c:1550-1565` (opening note is a separate lookup; header; current message):
```c
if (hush_store_find(store, &original, root) == HUSH_OK)
    human = original.pubkey;
...
hush_agent_copy(out, outsz, HUSH_AGENT_THREAD_HEAD);
if (original.id[0] != '\0' && strcmp(original.id, parent->id) != 0)
    hush_agent_append_turn(out, outsz, &original, "Conversation owner");
hush_agent_walk_thread(out, outsz, events, count, &walk);
size_t used = strlen(out);
int written = snprintf(out + used, outsz - used, "\nCurrent message: %s", parent->content);
```

Byte limits: each transcript line is flattened by `hush_agent_snip_line()` with a soft cap of
384 bytes (npub tokens are copied whole past the cap) — `src/hush_agent.c:1366-1411` and the
line buffer `char line[HUSH_AGENT_SNIP_MAX + HUSH_IDENTITY_NPUB_MAX + 1]` at `:1430`. The note
buffer is `HUSH_AGENT_NOTE_MAX = HUSH_EVENT_MAX_CONTENT * 3 + 1 = 12289` (`:59`,
`include/hush_event.h:14`); rendering stops at `used + 8 >= outsz` (`:1440-1441`), and the
final `snprintf` replaces the *entire* note with the bare current message if it overflows
(`:1564-1565`).

Precision notes (not contradictions):
- The **current message is not snipped to 384** — it is `parent->content` verbatim (up to 4096).
- There is no summarizer, no retrieval, and no per-thread file. The provider CLI's own memory is
  explicitly disabled: `HUSH_AGENT_GROK_NOMEM "--no-memory"` (`:150`) is in grok argv (`:2013`).
- Durable *event* state does exist (`store.ring`, see claim 3), but no durable *thread brief*.

### Claim 2 — context files RAM-only (verified)

`include/hush_roster.h:52-59`:
```c
typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char mime[HUSH_ROSTER_NAME_MAX];
    size_t bytes;
    /* Plaintext/Markdown body, kept in memory for turn injection. Not
     * serialized to JSON (session payload stays lean); lost on restart. */
    char text[HUSH_ROSTER_CONTEXT_BYTES + 1];
} hush_roster_context_t;
```

Confirmed by the session serializer, which emits only the count — `src/hush_roster.c:981`
(`"\"ncontext\":%zu,\"skills\":"`) — and by the only consumer, `hush_agent_append_context()`
(`src/hush_agent.c:1581-1621`), which reads the in-memory struct at turn time.

### Claim 3 — root out of the ring (partially verified)

Ring: `include/hush_store.h:12` `HUSH_STORE_CAPACITY = 1024`; eviction in
`src/hush_store.c:233-240`. Lookup is a linear scan that returns `HUSH_ERR_NOT_FOUND`
(`src/hush_store.c:198-210`).

What actually happens when the root is absent (`src/hush_agent.c:1546-1566`):
1. `hush_store_find(store, &original, root) != HUSH_OK` → no `Conversation owner` line, and
   the "human" fallback becomes `parent->pubkey` (`:1549-1551`).
2. `hush_agent_collect_thread()` still matches the root **by e-tag**
   (`hush_agent_event_is_root()`, `:1305-1319`), so any ring-resident reply still enters the
   window — and replies tag the root, not the immediate parent:
   `job->parent_id` is the root (`:1650`), and `hush_agent_fill_note()` writes it as the first
   e-tag (`:1114-1120`).
3. Only when *all* thread notes have also been evicted does the note become just
   `HUSH_AGENT_THREAD_HEAD` + `"\nCurrent message: …"` (`:1558,1563`) — never the current
   message alone. The "current message only" fallback at `:1748-1749` is unreachable while
   `store != NULL` because the header is written first.

Also relevant: `store.ring` is persisted **by default**, so a restart alone does not evict the
ring. `hush_store_may_persist()` returns 1 unless `HUSH_CONFIG_DIR` is set with no
`HUSH_HOME` (`src/hush_store.c:298-310`), insert snapshots+fsyncs on every insert
(`:136-149`), and `hush_store_persist_open()` reloads it (`:67-134`).

### Claim 4 — one flattened user message (verified)

`src/hush_inference.c:150-177` is the whole encoder:
```c
if (strcmp(provider, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0) {
    ... "{\"model\":\"%s\",\"max_tokens\":%d,\"system\":\"%s\\n%s\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}" ...
} else if (strcmp(provider, HUSH_ROSTER_PROVIDER_GEMINI) == 0) {
    ... "{\"systemInstruction\":{\"parts\":[{\"text\":\"%s\\n%s\"}]},"
        "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}]}" ...
} else {
    ... "{\"model\":\"%s\",\"messages\":[{\"role\":\"%s\",\"content\":\"%s\\n%s\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":false}" ...
}
```

So: OpenAI-compatible/XAI/DeepSeek/Custom → one `system` (OpenAI: `developer`) message with
system+rules, one `user` message, `"stream":false`; Anthropic → `system` string + one user
message (no explicit stream flag); Gemini → `systemInstruction` + one user part. No `tools`,
no `response_format`/JSON mode, never more than one user turn. Caps:
`HUSH_INFERENCE_OUTPUT_TOKENS = 2048` (`:28`), combined prompt text < 32768 bytes
(`:141-142`), response read capped at 131072 (`:23`).

CLI providers instead get one concatenated string — `src/hush_agent.c:2019-2031`:
```c
n = snprintf(out, outsz, "%s\n%s\n%s", job->prompt, job->rules, job->note);
```
passed as `grok -p <note> --system-prompt-override <prompt> --rules <rules> --no-memory`
(`:1986-2017`), `copilot -p <combined>` (`:2033-2047`), `codex exec … <combined>`
(`:2049-2065`), `goose run --text <combined>` (`:2067-2081`), `ollama run <model> <combined>`
(`:2083-2104`), `cline --json --cwd … <combined>` (`:1973-1984`). The API path is
`hush_agent_exec_api()` (`:1957-1971`), which maps `system=job->prompt`, `rules=job->rules`,
`message=job->note`.

### Claim 5 — no streaming/cancel/budget, 90 s blob, blocking fixup (verified)

Timeout and finish:
`include/hush_agent.h:14` `HUSH_AGENT_TIMEOUT_S = 90`; `src/hush_agent.c:2994-3000`
`return now >= job->started + (time_t)HUSH_AGENT_TIMEOUT_S;`. `hush_agent_poll()`
(`:738-795`) reaps jobs once per pump iteration (the pump's poll timeout is 1000 ms —
`src/hush_relay.c:43,733`).

One blob: the supervisor child buffers the whole provider output and writes it to the relay
pipe once — `src/hush_agent.c:1918-1921`:
```c
/* PIPE_BUF is at least one event on this Linux relay; incomplete writes fail closed. */
size_t len = strlen(reply);
if (write(output_fd, reply, len) != (ssize_t)len) _exit(HUSH_AGENT_EXEC_FAILURE);
```
The relay-side reads are chunked but only accumulate:
`hush_agent_read_job()` (`:2960-2970`) + `hush_agent_read_chunk()` (`:2972-2992`). API curls are
buffered to a `tmpfile()` and read whole after exit (`src/hush_inference.c:287-319`), with
`--max-time 80` and `--max-filesize 131072` (`:271-272`). Because curl runs in the forked
worker, ordinary replies do **not** block the relay loop (`include/hush_inference.h:28`).

Stop paths: 90 s timeout → `hush_agent_kill_job()` (`:2240-2249`) sends
`kill(-job->pid, SIGTERM)`; disabling the robot mid-job kills it on the next poll
(`:763-767`). There is no HTTP cancel route (grep for cancel/stop/abort finds no route).

Fixup blocks the loop: constants at `src/hush_http.c:51-52`
(`HUSH_HTTP_FIXUP_SLEEP_NS = 50000000`, `HUSH_HTTP_FIXUP_WAIT_MAX = 1800` = 90 s); the wait
loop `hush_http_wait_fixup()` (`:1867-1885`) calls `hush_agent_poll(NULL)` then
`nanosleep(50 ms)`, and `hush_http_serve_fixup()` (`:1887-1912`) runs inside the single
pump thread: `hush_service_clients()` → `hush_http_serve()` (`src/hush_relay.c:486-503`,
`src/hush_http.c:298-353`). While it waits, no other socket is read or written.

No accounting: `hush_inference_extract()` reads only text
(`/choices/0/message/content`, `/content`, `/candidates/0/content/parts` —
`src/hush_inference.c:321-334`); grep for `usage`, `prompt_tokens`, `cost`, `budget` finds
no counters. The only budget-shaped artifacts are prompt/loop guardrails: `max_tokens=2048`,
`HUSH_LAUNCH_TURNS_DEFAULT = 4` (`include/hush_launch.h:41`, enforced by
`hush_agent_turns_full()` `src/hush_agent.c:4142-4162` → chaperon nudge), and the
`skills/system/token-budget/SKILL.md` prompt-level skill ("Count robot work notes … At
max_robot_turns, emit the canned chaperon line").

### Claim 6 — PWA polling, cevent unconsumed (verified)

`demo/index.html:5721-5728`:
```js
async function tick() {
  try {
    const [st, evRes, sess, pres] = await Promise.all([
      fetch(API + "/api/status").then((r) => r.json()),
      fetch(API + "/api/events"),
      fetch(API + "/api/session").then((r) => r.json()),
      fetch(API + "/api/presence").then((r) => r.json()).catch(() => null)
    ]);
```
and `demo/index.html:6124-6125`: `tick(); setInterval(tick, 1000);`. Grep for
`chan-events|/api/cevent|cevent` in index.html: **no matches**. The route exists
(`src/hush_http.c:325-328`, `854-878`) and the ring is explicitly designed for a delta
consumer — `include/hush_cevent.h:50-55` (since-cursor) and `:61-70`
("g_ack has one owner: the PWA") — but only tests call it (`tests/check_agent.sh:291`).
Emit sites: `job_start` (`src/hush_agent.c:3817`), `job_done` (`:2871,2955`), `follow`
(`:3982`), `chaperon` (`:4204`), plus mention/intro/hop_denied/jobs_held/presence/stuck.

---

## (b) Current prompt / context / provider code map

Assembly (note jobs), in call order:

1. `hush_agent_start_grok()` `src/hush_agent.c:2186-2210` → `hush_agent_fill_job()` `:1623-1637`
2. `hush_agent_init_job()` `:1639-1657` — `parent_id = root(parent)`, `trigger_id`,
   `ask = in->ask ?: parent->content`, token minted.
3. `hush_agent_bind_job()` `:1659-1673` — human pubkey from the stored root when present.
4. `hush_agent_fill_directive()` `:1727-1741` → worker prompt (`:1712-1725`) / leader plan
   (`:1702-1710`) / election (`:1731-1735`).
5. `hush_agent_fill_rules()` `:1037-1048` → `HUSH_AGENT_RULES` (`:142-145`).
6. `hush_agent_fill_job_note()` `:1743-1752`:
   `fill_thread` → `humanize_ask` → `append_context`.
7. `hush_agent_add_guidance()` `:1767-1786` — robot prompt, room system prompt, topic, skills.
8. `hush_agent_exec_api()` `:1957-1971` or a CLI exec `:1973-2104`.

Resulting note text (\n-joined):

```
Thread so far. Do not repeat a prior joke. Fulfill only your specific part of the last human ask.
Conversation owner: <384-byte snip>          (only if the root is still in the ring)
<Name>: <384-byte snip>                      (up to 6 work notes)
Current message: <parent content, <=4096, may replace everything on overflow>
[file: <name>]                               (per attached context file)
<chunk from hush_seg_split, bounded by remaining note space>
```

Provider/context map:

| Layer | File | Notes |
|---|---|---|
| Request struct | `include/hush_inference.h:12-17` | `{provider, system, rules, message}`, borrowed |
| Encode + JSON escape | `src/hush_inference.c:134-148` | total text < 32768 |
| Body format | `src/hush_inference.c:150-177` | 3 shapes; no tools/JSON mode; `"stream":false` |
| Endpoint | `src/hush_inference.c:179-213` | `/v1/chat/completions`, `/v1/messages`, Gemini `/v1beta/models/{m}:generateContent` |
| Transport | `src/hush_inference.c:258-319` | fork+exec curl, config on stdin (secrets never in argv), response `tmpfile()` |
| Extract | `src/hush_inference.c:321-371` | content/delta paths only; skips `thought:true` parts |
| Job spawn | `src/hush_agent.c:2106-2133` | pipe + fork, `O_NONBLOCK` read end, `hush_relay_track_child(pid)` |
| Worker | `src/hush_agent.c:1868-1923` | capture child, single write back to relay |
| Poll | `src/hush_agent.c:738-795` | presence, stall, timeout, finish |

---

## (c) PWA polling + render map

- `tick()` (1 Hz, `demo/index.html:5721-5746`) fetches the four endpoints above, sets
  `session`, `whisperReady`, `lastPresence`, then `render(ev.events, st)`.
- Robot activity is **status-derived**, not streamed: `/api/status` includes
  `"thinking":[{"name","parent","slug","provider","stage"}]` built by `hush_agent_status()`
  (`src/hush_agent.c:712-736`, `hush_agent_status_append` `:1077-1097`) — no partial text.
- `/api/events` is the full visible event list (kind-1/h-tag filtered); `/api/session` is the
  roster/channel snapshot; `/api/presence` is the live line table.
- `/api/chan-events` (cevent) and `/api/presence` POST exist but only the GET presence tick is
  used by the UI; the cevent ring has no UI consumer (tests only).
- Render: `render()`/`paint()`/`paintThreadStream()` (`demo/index.html:4806-4845`), with
  `#thread-think` used for an optimistic "thinking" strip (`:4266-4267`). No incremental
  assistant text path exists.

---

## (d) Options analysis

### D1. SSE parsing inside the single-threaded poll loop

Current constraint: job pipes are **not** in the poll set — `hush_fill_pollfds` watches only
the listener + clients (`src/hush_relay.c:686-704`), and `hush_agent_poll` runs once per
iteration before a 1000 ms `poll()` (`:715-742`). Worst-case detection latency for new child
bytes is therefore ~1 s today.

- **A. Dynamic timeout.** Keep everything, but make the pump timeout
  `hush_agent_busy() ? 25-50 ms : HUSH_POLL_TIMEOUT_MS`. Smallest diff; bounded CPU when idle.
- **B. Add job fds to the poll set** (recommended for latency). New accessor such as
  `size_t hush_agent_fill_pollfds(struct pollfd *fds, size_t max)` (job table is static,
  `src/hush_agent.c:315`) plus a readiness→slot call into `hush_agent_read_chunk()`.
  Still one thread (note `tests/check_complete.sh:96-97` asserts no `pthread`).

Incremental SSE parser shape (no async, no new deps):
1. Per job keep a bounded line buffer + `hush_sse_t` state.
2. On readable bytes: append; for each `\n` handle CRLF; skip empty lines and `:` comments;
   require a `data:` field, strip one leading space (WHATWG ABNF).
3. If payload is `[DONE]` → mark EOF. Otherwise `hush_json_lookup()` a JSON pointer on the
   NUL-terminated line: OpenAI `/choices/0/delta/content` (+ `/choices/0/finish_reason`,
   `/usage`), Anthropic `/delta/text` for `content_block_delta` (+ `/usage` on
   `message_start`/`message_delta`), Gemini SSE `/candidates/0/content/parts/0/text`.
4. Append decoded text to `job->out` with the existing bound (`HUSH_EVENT_MAX_CONTENT`); on
   `[DONE]`/EOF close as today.
5. curl changes: `"stream":true` + `--no-buffer` (`-N`, "Disables the buffering of the output
   stream"), and connect curl's stdout to the relay pipe (or relay bytes through the worker)
   instead of the response `tmpfile()`. Keep `--fail` semantics: exit non-zero *and* no parsed
   text → the existing "did not return a usable reply" notice
   (`src/hush_agent.c:2861-2873`). Optionally retry once without `stream` for providers that
   reject it.

Risk: providers differ; gate streaming per provider id (the code already branches per
provider) and keep a non-streaming fallback path.

### D2. Pushing partial output to the PWA

The writer today **requires Content-Length** and closes each connection — `hush_http_reply()`
`src/hush_http.c:515-524`:
```c
"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n" ...
```
There is no chunked encoding anywhere (grep: only `Content-Length`), and `hush_http_write_all()`
(`:484-497`) does bounded blocking writes (`poll(POLLOUT, 250 ms)`, 8 retries).

- **Option 1 — poll a growing buffer.** Add a truncated partial (`job->out`) to
  `hush_agent_status_append` or a new `/api/reply?t=<token>` GET modelled on the existing
  `/api/complete` long-poll (`src/hush_http.c:1994-2019`: `{"ok":true,"pending":true}` or
  `{"ok":true,"text":...}`). Zero writer changes, reuses the 1 Hz tick (or 250 ms while busy).
  Cost: whole-buffer resend, JSON escaping per poll, no token-level cadence.
- **Option 2 — true SSE.** New close-delimited response helper: status line +
  `Content-Type: text/event-stream`, `Cache-Control: no-cache`, `Connection: close`, **no**
  Content-Length (HTTP/1.1 permits close-delimited bodies; SSE framing per WHATWG), then
  `data: {json}\n\n` per delta and `data: [DONE]\n\n` at the end. Requires a stream flag +
  root/token filter on `struct client` in `src/hush_relay.c` and a fanout from the parsed
  deltas; drop the client on EAGAIN/disconnect. Browser `EventSource` consumes it directly.
- **Option 3 — NDJSON.** Same framing problem: with `Content-Length` it is not streaming; with
  close-delimited framing it works and `fetch()` + line reader parses it, but SSE is the
  better-supported browser idiom and matches the cevent "signals" concept.

Recommendation: Option 1 first (tiny, testable, fixes the UX gap), Option 2 as the follow-on,
at which point the PWA should also consume `/api/chan-events` via its `since=`/ack cursor
(`include/hush_cevent.h:50-70`) instead of inventing a second signal channel.

### D3. Cancellation of fork/exec jobs

Already present:
- The supervisor child becomes its own process-group leader —
  `src/hush_agent.c:1788-1798` `setpgid(0, 0)` (and resets SIGCHLD/SIGTERM/SIGINT).
- Timeout kill signals the **group**: `hush_agent_kill_job()` `:2240-2249`
  `kill(-job->pid, SIGTERM)`. The capture worker, the provider CLI, and curl are all
  descendants in that group.
- The relay tracks children for shutdown reaping: `hush_relay_track_child(pid)` `:2131`;
  `hush_relay_reap_children()` `src/hush_relay.c:744-754` (SIGTERM then SIGKILL at
  `src/hush_relay.c:1132-1135`).

Missing for user cancel:
- A stable handle exposed to the UI. Note jobs already mint `job->token` at
  `hush_agent_init_job()` `:1656`, but `hush_agent_status()` never emits it
  (`:1077-1097`); fixup tokens are exposed only via the fixup POST response.
- `hush_agent_cancel(token)`: send SIGTERM to `-pid`, arm a SIGKILL deadline (needs a new
  `time_t kill_at` on `hush_agent_job_t`, because `kill_job` currently finishes the job
  immediately), close the read fd, and mark the job cancelled so `hush_agent_finish_job()`
  (`:2875-2893`) writes an honest "stopped" line instead of `hush_agent_note_failure()`.
- A route, e.g. `POST /api/cancel {token}` in `hush_http_serve_api_post()`
  (`src/hush_http.c:2021-2076`), plus a PWA stop button.
- The fixup waiter (`src/hush_http.c:1867-1885`) should treat "token unknown/cancelled" as
  done rather than waiting out 90 s.

### D4. Durable thread briefs

What already exists and can be reused:
- `store.ring` — bounded 1024-event **snapshot**, not append-only: every insert rewrites the
  whole file + fsync + rename (`src/hush_store.c:136-149,527-572`). Durable across restart by
  default, but O(ring) per insert and no per-thread structure.
- `wake.ledger` — header + 256 fixed slots + delivery table, atomic tmp/fsync/rename commit
  (`src/hush_wake.c:728-783`); keyed by 32-byte hashes, stores no text. Its commit pattern is
  the template to copy for an index.
- `hush_json_lookup()` JSON-pointer reader (`src/hush_json_read.c`) already parses borrowed
  NUL-terminated buffers, so JSONL lines need no new parser.
- `hush_seg_split()` (`src/hush_seg.c`) for bounded, structure-aware excerpts.

Options:
- **A. Append-only JSONL + index (recommended).** `$HUSH_HOME/agents/threads/<root>.jsonl`,
  one object per turn `{"t":…,"id":…,"who":…,"pub":…,"text":…}`; `O_APPEND` single `write()`
  per line; a small `threads.idx` (root → count/offset) rewritten with the same
  tmp+fsync+rename helper. Read the tail (last K lines) with a bounded reverse scan; ignore a
  truncated final line. Hostile input is already bounded by event content (4096) and the
  384-byte snip on injection. Dependency-free (C11 + POSIX + OpenSSL only).
- **B. SQLite.** Reject: new mandatory dependency against `hush-c/Makefile:8`
  (`LDFLAGS := -lcrypto`) and the repo's C11/no-crates rule; also heavier than the problem
  (a few lines per thread).

Integration point: `hush_agent_fill_thread()` (`src/hush_agent.c:1535-1566`) — prefer ring
notes, fill missing slots from the JSONL brief, and keep the same 384-byte-per-line flatten.
Also consider appending every published robot reply in `hush_agent_publish_reply()` (`:2941`).

### D5. Token/cost accounting

What providers return today (unparsed by Hush):
- OpenAI-compatible non-streaming bodies include
  `"usage":{"completion_tokens":…,"prompt_tokens":…,"total_tokens":…}` (MS Learn example).
- Streaming can add a final usage chunk with `stream_options:{"include_usage":true}`
  (`choices: []` on that chunk) — OpenAI community announcement.
- Anthropic returns `usage` (input/output tokens) on the message/stream events; Gemini on
  `usageMetadata`.
- Hush discards all of it: `hush_inference_extract()` `src/hush_inference.c:321-334`.

Plan: store per-job `{in,out,total}` on `hush_agent_job_t`, add cumulative per-provider and
per-root counters in a small static table exposed via `/api/status`
(`hush_http_serve_status`), and optionally a price table read from the existing
`providers.json` overlay (no network, no new dep). Keep `max_tokens=2048` as the hard cap.

---

## (e) Recommended incremental design (single-threaded, no new mandatory deps)

M1 — **SSE core as a pure module.** `include/hush_sse.h` + `src/hush_sse.c`:
`hush_sse_feed(state, buf, n, callback)` emitting `{kind: delta|done|usage|error, text}`.
Unit-testable with byte-wise feeds; reuses `hush_json_lookup`. No sockets, no globals.

M2 — **Stream API providers.** In `hush_inference.c`: emit `"stream":true`,
`--no-buffer`, and pipe curl stdout to the job pipe (drop the response tmpfile for API jobs).
Parse in the relay with M1 and append deltas to `job->out` in `hush_agent_read_chunk()`.
Fallback: one non-streaming retry when the provider rejects `stream` or emits no deltas.
Publish at `[DONE]`/EOF exactly as today, so store/fanout/presence paths are untouched.

M3 — **Latency.** Add `hush_agent_fill_pollfds()` (option B1) so job readiness wakes the pump;
otherwise use a dynamic poll timeout while any job is busy. Keep the 90 s timeout and
`kill(-pid, SIGTERM)`; add SIGKILL escalation after a grace period.

M4 — **Partial UI.** Extend `hush_agent_status_append()` with a bounded tail of `job->out`
and add `GET /api/reply?t=<job token>` returning `{pending,text,done}` (clone of the
`/api/complete` pattern). PWA: while the thread's robot is busy, poll that endpoint at
~250 ms and render a ghost bubble; keep the 1 Hz tick unchanged.

M5 — **Cancel.** Expose the job token, add `hush_agent_cancel()` + `POST /api/cancel`,
SIGTERM the group, SIGKILL after ~5 s, and write a "stopped by you" note. PWA stop button.

M6 — **Durable briefs.** Append-only JSONL per root + atomic index; consulted by
`hush_agent_fill_thread()` only for slots the ring cannot fill; same 384-byte flatten.

M7 — **Budgets.** Parse usage into per-job + cumulative counters; expose in `/api/status`;
surface in Settings. No pricing network calls.

M8 — **SSE endpoint + cevent consumption** (after M4 proves the payload): close-delimited
`text/event-stream` writer, per-client stream subscriptions, and PWA `EventSource` wiring;
then have the PWA finally consume `/api/chan-events` with its `since=`/ack cursor.

---

## (f) Test strategy

Existing coverage to extend (all wired into `hush-c/Makefile:65-81`):

| Area | Existing test | How to extend |
|---|---|---|
| SSE core | none (`tests/test_json_read.c`, `tests/test_json.c` for pointer/decode) | new `tests/test_sse.c`: partial lines across feeds, CRLF, comments, `data: [DONE]`, malformed JSON, oversized line, delta accumulation, usage chunk |
| API providers | `tests/check_collaboration.py:105-137` `Endpoint` fixture (asserts paths, model, headers, prompt contents); `:255-305` `check_memory` | add `Endpoint.mode="stream"` emitting SSE chunks with sleeps; assert the relay still assembles `API_REPLY_*` once, and that the request body gained `"stream": true`; assert the memory assertions still hold when the reply arrives in pieces |
| curl argv / transport | `tests/check_collaboration.py:352-369` fake `cline`; `:154-168` fake `pass` | install a fake `curl` first on PATH that logs argv, asserts `--no-buffer`, and replays a canned SSE stream; also assert exit-code failure still yields the "did not return a usable reply" note |
| Thread transcript | `tests/check_agent.sh:79` (context marker into `grok -p`), `:304-305` (`HUSH_AGENT_THREAD_HEAD`), `:400` (no raw npubs) | add a >6-turn thread and assert only the last six notes plus the opening appear; add a durable-brief test with a temp `HUSH_HOME`: 7+ turns, restart, assert the opening survives eviction; truncate the last JSONL line and assert graceful recovery |
| Cancel | `tests/check_fixup.sh:1-81` (blocking fixup shape) | new `tests/check_cancel.sh`: fake provider that traps SIGTERM and sleeps; POST `/api/cancel`; assert the trap fired, the job left `/api/status`, no orphan process group (`kill -0 -pgid` fails), and the thread got a "stopped" note |
| PWA | `tests/check_pwa.sh:23-63` (HTML greps), `tests/check_launch.sh:239-259` (thread UI greps) | grep for the new endpoint / `EventSource`; `tests/check_collaboration_ui.cjs` (Playwright) with a slow stream fixture can assert incremental text before completion |
| Budgets | none | extend the `Endpoint` fixture to include `usage` and assert `/api/status` counters increment; unit-test the counter math |
| Legibility/architecture | `tests/check_complete.sh:96-97` asserts no `pthread` | streaming/cancel must stay single-threaded; new files follow write-legible-c |

Order: M1 unit tests first (pure function), then the fixture-based integration tests, then the
shell/PWA greps; `make test` must stay green (Makefile:65-81).

---

## (g) Cited URLs

Fetched and read during this research (external, untrusted content — used as documentation
only):

- WHATWG HTML — Server-sent events (event-stream ABNF, `text/event-stream`, UTF-8, CR/LF/CRLF
  line endings): https://html.spec.whatwg.org/multipage/server-sent-events.html
- MDN — Using server-sent events ("respond using the MIME type text/event-stream… each
  notification is sent as a block of text terminated by a pair of newlines"):
  https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events
- OpenAI Developer Community — usage stats for streaming, `stream_options:{"include_usage":true}`,
  final chunk with `choices: []`:
  https://community.openai.com/t/usage-stats-now-available-when-using-streaming-with-the-chat-completions-api-or-completions-api/738156
- OpenAI-compatible SSE chunk example (`data: {...}`, `delta.content`, `data: [DONE]`):
  https://blog.georgeck.me/how-to-handle-streaming-in-openai-gpt-chat-completions
- Microsoft Learn — Azure/OpenAI chat completions response with
  `"usage":{"completion_tokens":…,"prompt_tokens":…,"total_tokens":…}`:
  https://learn.microsoft.com/en-us/azure/ai-foundry/openai/how-to/chatgpt
- curl `-N, --no-buffer` ("Disables the buffering of the output stream"):
  https://curl.se/docs/manpage.html#-N (quoted from the mirror at
  https://linux.die.net/man/1/curl because the canonical page truncated in fetch)
- NDJSON specification (newline-delimited JSON streams):
  https://github.com/ndjson/ndjson-spec

Not verified in this session (JS-rendered or truncated, no content extracted): the OpenAI
platform streaming guide/API reference pages and the Anthropic streaming docs. Anthropic's SSE
event names and usage fields are stated from the code's existing JSON-pointer conventions and
should be re-checked against those docs before M2 implementation.
