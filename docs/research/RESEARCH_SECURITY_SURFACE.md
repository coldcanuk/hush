# RESEARCH: Hush HTTP/security surface (0.0.1 @ 5f67c65eb, worktree gb/review-hardening)

Verifier: research subagent. Method: read-only inspection of the worktree source at
`/opt/repo/hush/worktrees/review-hardening`. All paths below are relative to that worktree root.
No build, no test run, no code change.

Claims reviewed are those raised by the external technical review of Hush 0.0.1 @ 5f67c65eb.

---

## (a) Claim-by-claim verification

| # | Claim (abridged) | Verdict | Primary evidence |
|---|------------------|---------|------------------|
| 1 | Listener binds INADDR_ANY while printing http://127.0.0.1 | **verified** | `hush-c/src/hush_relay.c:287-288` (`INADDR_ANY`, `bind`), `:676`, `:337`; no address flag/env exists (`hush-c/src/hush_relay_main.c:66-88`) |
| 2 | HTTP API unauthenticated; `logged_in` is only a process flag | **verified** | dispatch `hush-c/src/hush_http.c:313-350, 2021-2076`; only gates at `:724`, `:905`; flag set at `hush-c/src/hush_launch.c:438, 457, 493`, cleared `:558` |
| 3 | `/api/project` passes arbitrary path to `system("mkdir -p '%s' && git init -q '%s'")` | **verified** | `hush-c/src/hush_http.c:1745-1752`; `hush-c/src/hush_launch.c:1066-1094` (`1084-1088`); `hush-c/src/hush_launch.c:1881-1884` |
| 4 | Every reply carries `Access-Control-Allow-Origin: *` | **verified** | only two emission sites: `hush-c/src/hush_http.c:520` (generic `hush_http_reply`) and `:648` (`/api/events` hand-rolled header). Grep for `Access-Control` across `hush-c/` returns exactly those lines |
| 5 | `/api/session` returns nsec + join token; `/api/ice` returns live TURN password; `/api/provider` overwrites secrets; `/api/provider/scan` is SSRF; `/api/exit` shuts down | **verified** (each, with nuances below) | session `hush-c/src/hush_launch.c:1595-1636`; ice `hush-c/src/hush_turn.c:216-250`; provider `hush-c/src/hush_http.c:2252-2283` → `hush-c/src/hush_provider.c:293-312, 894-911`; scan `hush-c/src/hush_provider.c:1029-1042, 1072-1117`; exit `hush-c/src/hush_http.c:2086-2092` → `hush-c/src/hush_relay.c:196-201` |
| 6 | Private vibe join token generated/displayed but never checked; no relay code compares a pubkey against channel membership | **verified** | token sites only: `hush-c/src/hush_launch.c:786-788, 818-822, 1614/1631, 2135-2136, 2422-2423`; PWA display `hush-c/demo/index.html:3281-3283, 3842-3843`; membership stored `hush-c/src/hush_launch.c:953-994`, consumed only by JSON format `:1711-1750` and robot routing `hush-c/src/hush_intel.c:318-342` |
| 7 | Build hardening: only `-Wl,-z,noexecstack`; no SSP/FORTIFY/PIE/RELRO/NOW | **verified** | `hush-c/Makefile:7-8, 11`; `configure:236-237` (writes `config.mk` CFLAGS/LDFLAGS with no hardening); repo-wide grep for `stack-protector|FORTIFY|relro|fPIE|-pie` matches nothing |
| 8 | `/tmp` cwd and TURN state dirs accepted if they already exist, no ownership/symlink check | **verified** | `hush-c/src/hush_agent.c:1050-1075`; `hush-c/src/hush_canvas.c:224-249`; `hush-c/src/hush_turn.c:301-326, 383-392, 407`; same shape at `hush-c/src/hush_home.c:210-218`, `hush-c/src/hush_provider.c:598-609`, `hush-c/src/hush_relay.c:797-798` |

No claim was refuted or left unverifiable. Two claims need precision notes:

- **Claim 3 line number.** The command string is *built* at `hush_launch.c:1881-1883`; `system()` is invoked at `:1884`. The reviewer's cited line 1881 is the `snprintf` that constructs the injection point, so the citation is accurate but the executing call is one line lower.
- **Claim 5 / `/api/ice`.** The credential is returned only when the TURN manager is enabled; when off, the response contains only a public Google STUN URL (`hush_turn.c:227-232`). "Live TURN password" is correct in the enabled state.

### Claim 1 detail — bind vs. printed address

```c
/* hush-c/src/hush_relay.c:280-291 (hush_listen_on) */
ls = socket(AF_INET, SOCK_STREAM, 0);
...
addr.sin_family = AF_INET;
addr.sin_port = htons(port);
addr.sin_addr.s_addr = htonl(INADDR_ANY);
if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
```

- `hush_listen_on(uint16_t port)` (`:274`) is the only bind path; called from `hush_relay_bind` (`:655`).
- Address controls: none. `hush_parse_args` accepts only positional port plus `--open|--no-open|--close|--quit|-h|--help` (`hush-c/src/hush_relay_main.c:66-88`). No `HUSH_BIND`/`HUSH_ADDR` env is read anywhere for the socket.
- Printed/browser addresses are all loopback:
  - `hush-c/src/hush_relay.c:676` `fprintf(stdout, "listening on http://127.0.0.1:%u/\n", ...)`
  - `hush-c/src/hush_relay.c:336-337` browser address `"http://127.0.0.1:%u/"`
  - `hush-c/src/hush_relay.c:661-665` EADDRINUSE message
  - `hush-c/src/hush_relay.c:1176` `--app=http://127.0.0.1:%u/`
  - `hush-c/src/hush_relay_main.c:95` close hint
- Consequence: the process advertises loopback while accepting connections on every interface (`0.0.0.0`).

### Claim 2 detail — authentication is a process flag

The only two 401 sites in `hush_http.c`:

```c
/* hush-c/src/hush_http.c:724-728 (hush_http_serve_post, POST /api/event) */
if (g_launch == NULL || !g_launch->logged_in) {
    const char *error = "Sign in before sending a message.\n";
    hush_http_reply(fd, "401 Unauthorized", "text/plain", error, strlen(error));
    return HUSH_ERR_DENIED;
}
```

```c
/* hush-c/src/hush_http.c:905-908 (hush_http_serve_presence_post, POST /api/presence) */
if (g_launch == NULL || !g_launch->logged_in) {
    hush_http_reply(fd, "401 Unauthorized", "text/plain", "login\n", 6);
    return HUSH_ERR_DENIED;
}
```

`logged_in` is set/cleared only by in-process state transitions:
`hush_launch_create_identity` (`hush-c/src/hush_launch.c:438`), `hush_launch_import_identity` (`:457`),
`hush_launch_restore_identity` (`:493`, startup restore from `pass`), `hush_launch_logout` (`:558`).
No request carries or proves a credential: there is no `Authorization`, `Cookie`, `X-*`, or
token comparison anywhere in `hush_http.c` (grep for those names returns only unrelated provider
secret fields and canvas job tokens). Once the browser process is logged in, every other client on
the network can call the API with the same authority.

### Claim 3 detail — project path injection

Request → handler (no path validation):

```c
/* hush-c/src/hush_http.c:1745-1752 (hush_http_serve_project) */
if (!hush_json_field(body, "path", path, sizeof(path)))
    path[0] = '\0';
if (hush_json_field(body, "git", gitbuf, sizeof(gitbuf)) &&
    (strcmp(gitbuf, "1") == 0 || strcmp(gitbuf, "true") == 0))
    init_git = 1;
return hush_http_reply_session(fd,
                               hush_launch_add_project(g_launch, store,
                                                       name, path, init_git));
```

Path is copied with only whitespace trim + truncation:

```c
/* hush-c/src/hush_launch.c:1084-1088 (hush_launch_add_project) */
if (path != NULL && path[0] != '\0')
    hush_launch_copy_name(proj->path, sizeof(proj->path), path, "");
if (init_git && proj->path[0] != '\0') {
    if (hush_launch_git_init(proj->path) != HUSH_OK)
        return HUSH_ERR_IO;
}
```

```c
/* hush-c/src/hush_launch.c:1881-1884 (hush_launch_git_init) */
if (snprintf(cmd, sizeof(cmd), "mkdir -p '%s' && git init -q '%s'",
             path, path) >= (int)sizeof(cmd))
    return HUSH_ERR_ARG;
if (system(cmd) < 0 && stat(gitdir, &st) != 0)
    return HUSH_ERR_IO;
```

- No escaping/validation of `'`, `;`, ``$()``, backticks, or newlines. A single quote in `path`
  terminates the quoting and the remainder is executed by `/bin/sh`.
- Bounds: `path` is `HUSH_LAUNCH_PATH_MAX = 256` (`hush-c/include/hush_launch.h:18`), `cmd` is
  `HUSH_LAUNCH_CMD_MAX = 768` (`hush-c/src/hush_launch.c:28`).
- Callers: `hush_launch_git_init` is called only from `hush_launch_add_project` (`:1087`);
  `hush_launch_add_project` is called only from `hush_http_serve_project` (`hush-c/src/hush_http.c:1751`)
  and the unit test `hush-c/tests/test_launch.c:316`.
- Trigger condition: request must include `"git":"true"` (or `"1"`) and a non-empty `path`.
- Reaching it needs `launch->has_vibe` (`hush-c/src/hush_launch.c:1076-1077`), i.e. after setup — but
  setup itself is unauthenticated (claim 2), so any client that can reach the port can chain
  `/api/identity create` → `/api/vibe` → `/api/project`.

### Claim 4 detail — CORS

Two and only two HTTP header emitters:

```c
/* hush-c/src/hush_http.c:515-524 (hush_http_reply) */
n = snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Headers: Content-Type\r\n"
             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             "\r\n",
             status, ctype, blen);
```

```c
/* hush-c/src/hush_http.c:647-649 (hush_http_serve_events) */
const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n"
    "Connection: close\r\n\r\n{\"events\":[";
```

Grep `HTTP/1.1` in `hush-c/src/*.c` matches only these two sites (`:516`, `:647`); every
`/api/*` handler replies through `hush_http_reply`. `OPTIONS` short-circuits to a 204 through the
same function (`:306-309`). So the claim "every reply carries ACAO: *" is accurate.

### Claim 5 detail — secret disclosure and destructive routes

**`/api/session` (nsec + join token).** `hush-c/src/hush_http.c:321-324` → `hush_http_serve_session`
(`:836-852`) → `hush_launch_format_session` → `hush_launch_write_session_open`:

```c
/* hush-c/src/hush_launch.c:1611-1631 (excerpt) */
"\"npub\":\"%s\",\"pubkey\":\"%s\",\"nsec\":\"%s\","
"\"vibe\":{\"name\":\"%s\",\"about\":\"%s\","
"\"visibility\":\"%s\",\"discoverable\":%s,"
"\"join_token\":\"%s\"},"
...
launch->logged_in ? launch->human.npub : "",
launch->logged_in ? launch->human.pubkey_hex : "",
(launch->logged_in && !launch->backup_acked)
    ? launch->human.nsec : "",
...
launch->has_vibe ? launch->vibe_token : "",
```

- nsec is exposed while `logged_in && !backup_acked` (onboarding window); it is dropped after
  `ack_backup` (test asserts this: `hush-c/tests/check_launch.sh:358, 363`).
- `join_token` is emitted whenever `has_vibe`, including public vibes
  (`hush_launch_create_vibe` always mints one at `hush-c/src/hush_launch.c:786-788`).

**`/api/ice` (TURN credential).** `hush-c/src/hush_http.c:2393-2407` →
`hush_turn_format_ice`:

```c
/* hush-c/src/hush_turn.c:234-243 (excerpt) */
"{\"ok\":true,\"compiled\":true,\"running\":%s,"
"\"iceServers\":["
"{\"urls\":[\"stun:%s:%u\"]},"
"{\"urls\":[\"turn:%s:%u?transport=udp\","
"\"turn:%s:%u?transport=tcp\"],"
"\"username\":\"%s\",\"credential\":\"%s\"}]}\n",
...
turn->username, turn->password);
```

Password is generated once (`hush-c/src/hush_turn.c:97-100`, 32 hex chars,
`HUSH_TURN_PASS_HEX` `hush-c/include/hush_turn.h:23`) and written into `turnserver.conf`
(`:410-418`, `user=%s:%s`). Any client that reads `/api/ice` obtains relay credentials.

**`/api/provider` POST (secret overwrite).** `hush-c/src/hush_http.c:2252-2283` →
`hush_provider_save` (`hush-c/src/hush_provider.c:293-312`) → writes every supplied
`api_key/username/password/token/passkey` via `hush_pass_save`
(`hush-c/src/hush_provider.c:894-911`). No authentication and no confirmation; a single POST can
replace the credentials of any known provider id (also flips `host`, `model`, `use_home`).

**`/api/provider/scan` (SSRF).** `hush-c/src/hush_http.c:2317-2336` reads `host` straight from the
JSON body; `hush_provider_scan` copies it unchecked into the URL:

```c
/* hush-c/src/hush_provider.c:1029-1042 (hush_provider_fill_curl_url) */
if (strcmp(id, HUSH_ROSTER_PROVIDER_GEMINI) == 0) {
    if (api_key != NULL && api_key[0] != '\0')
        snprintf(url, urlsz, "%s/v1beta/models?key=%s", host, api_key);
    else
        snprintf(url, urlsz, "%s/v1beta/models", host);
    return;
}
snprintf(url, urlsz, "%s/v1/models", host);
```

```c
/* hush-c/src/hush_provider.c:1053-1055 (curl config) */
"silent\nshow-error\nmax-time = %d\nurl = \"%s\"\n"
```

curl is then executed with `--config` (`hush-c/src/hush_provider.c:1104-1117`). The caller supplies
the full scheme/host, so `http://127.0.0.1:<port>`, RFC1918 hosts, link-local metadata
(`http://169.254.169.254/...`) and `file://`-style schemes are all reachable from the relay
process; the `?key=` variant additionally places a caller-supplied API key in the URL. Response
models are parsed back out (`:1169+`), so this is a read primitive, not blind.

**`/api/exit` (shutdown).** `hush-c/src/hush_http.c:2064-2065` → `:2086-2092`:

```c
static hush_status_t hush_http_serve_exit(int fd)
{
    hush_relay_note_leave(1);
    ...
}
```

→ `hush_relay_note_leave` (`hush-c/src/hush_relay.c:196-201`) sets `g_shutdown = 1`
(`hush_relay_request_shutdown`, `:173-176`), which the pump observes (`:708`). `/api/close`
(`hush_http.c:2080`) only sets the leave ack and leaves the relay running.

### Claim 6 detail — join token has no consumer

Every occurrence of `vibe_token` in the tree:

- mint: `hush-c/src/hush_launch.c:786-788` (create vibe), `:818-822` (mint when flipped private)
- persist: `:2135-2136` (write `vibe_token` field), `:2422-2423` (restore)
- expose: `:1614/1631` (session JSON)
- display only: `hush-c/demo/index.html:3281-3283` and `:3842-3843` render
  `"Join token: " + session.vibe.join_token`

No `hush_http.c` reference to `vibe_token`, no request field named `token`/`join_token` in the
HTTP layer, and no comparison of any submitted value against it. The `/api/member` handler
(`hush-c/src/hush_http.c:1012-1025`) takes `npub` + `name` and never asks for the token; the PWA
`invite-human` flow (`hush-c/demo/index.html:3279-3294`) shows the token but posts only
`{npub, name}`.

Membership data exists but is never consulted for authorization: `hush_launch_set_channel_roster`
stores `ch->humans[]`/`ch->robots[]` (`hush-c/src/hush_launch.c:953-994`); the only readers are the
session JSON formatter (`:1711-1750`) and `hush_intel_robot_on_channel`
(`hush-c/src/hush_intel.c:318-342`), which decides whether a *robot* may answer on a channel.
`hush_http_is_robot_key` (`hush-c/src/hush_http.c:789-806`) is conversation routing. No code
compares a connecting pubkey, address, or token against channel membership.

### Claim 7 detail — build flags

```make
# hush-c/Makefile:7-11
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2
LDFLAGS := -lcrypto
# Parent ./configure writes HUSH_HAVE_X11 and -lX11 when Xlib is present.
-include ../config.mk
override LDFLAGS += -Wl,-z,noexecstack
```

`configure` writes `config.mk` at `configure:236-237` with the same CFLAGS plus OpenSSL/X11
detection — no `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2/3`, `-fPIE`, `-pie`,
`-Wl,-z,relro`, or `-Wl,-z,now`. A repo-wide grep of `Makefile`, `hush-c/Makefile`,
`configure`, `debian/`, `freebsd/`, `openbsd/`, `hush-relay.spec` for those tokens returns only
the `noexecstack` line. Note the top-level `Makefile:16,21` includes `config.mk` and forwards
`CFLAGS="$(CFLAGS)"` to `hush-c`, so hardening must be added in both the hush-c defaults and
`configure`'s generated CFLAGS/LDFLAGS to survive either invocation path.

### Claim 8 detail — pre-existing directory/symlink trust

TURN state dir resolution and creation:

```c
/* hush-c/src/hush_turn.c:301-326 (resolve; excerpt) */
env = getenv("HUSH_STATE_DIR");
...
else if (home != NULL && home[0] != '\0')
    ... "%s/.local/state/hush" ...
else
    hush_turn_copy(turn->state_dir, ..., "/tmp/hush");
if (strlen(turn->state_dir) > (size_t)HUSH_TURN_STATE_DIR_CAP)
    hush_turn_copy(turn->state_dir, ..., "/tmp/hush");
```

```c
/* hush-c/src/hush_turn.c:383-392 */
static hush_status_t hush_turn_ensure_dir(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return HUSH_ERR_ARG;
    if (mkdir(path, 0700) == 0)
        return HUSH_OK;
    if (errno == EEXIST)
        return HUSH_OK;
    return HUSH_ERR_IO;
}
```

`turnserver.conf` is then written with `fopen(turn->conf_path, "w")` (`:407`), which follows
symlinks, and contains `user=%s:%s` with the live password (`:410-418`). If `HOME`/`XDG_STATE_HOME`
are unset (or the path exceeds the 300-char cap), the predictable world-writable `/tmp/hush` is
used. There is no `lstat`, `S_ISLNK`, `st_uid`, or `O_NOFOLLOW` check anywhere in this path
(repo grep for those tokens matches only `hush-c/src/hush_codex.c:113-124`, which does
`lstat` + `S_ISDIR` after EEXIST — still no uid check).

Agent/canvas cwd:

```c
/* hush-c/src/hush_agent.c:1060-1074 (hush_agent_prepare_cwd; excerpt) */
cfg = getenv(HUSH_AGENT_ENV_CONFIG);
base = getenv("TMPDIR");
leaf = HUSH_AGENT_CWD_TMP;              /* "hush-agent-cwd" (:80) */
if (base == NULL || base[0] == '\0')
    base = HUSH_AGENT_TMP_FALLBACK;      /* "/tmp" (:81) */
...
n = snprintf(out, outsz, "%s/%s", base, leaf);
...
(void)mkdir(out, (mode_t)HUSH_AGENT_CWD_MODE);   /* result discarded */
```

`hush_canvas_prepare_cwd` is the same code (`hush-c/src/hush_canvas.c:224-249`). The resulting path
is handed to the spawned tool as `--cwd` / `--cd` (`hush_agent.c:1979, 2007-2008, 2059`;
`hush_canvas.c:321-322`). A pre-created `/tmp/hush-agent-cwd` (owned by another local user, or a
symlink to a directory they control) is accepted silently because `EEXIST` is ignored and no
ownership or symlink check follows. Same pattern: `hush_home_mkdir` (`hush-c/src/hush_home.c:210-218`),
`hush_provider_ensure_dir` (`hush-c/src/hush_provider.c:598-609`, writes provider secrets into that
dir), and the pidfile dir `(void)mkdir(dir, 0700)` (`hush-c/src/hush_relay.c:797-798`).

---

## (b) Complete `/api/*` route inventory

Dispatch is a linear chain in `hush_http_serve` (`hush-c/src/hush_http.c:298-353`) followed by
`hush_http_serve_api_post` (`:2021-2076`). Path extraction (`hush_http_path`, `:386-401`) strips
the query string, so `?k=v` is available in the raw `req` buffer but not matched.

"Gate" means any credential/authorization check. `logged_in` is the in-process flag from claim 2.

| Method(s) actually dispatched | Path | Dispatch / handler | Gate | Returns (sensitivity) |
|---|---|---|---|---|
| ANY (no method guard) | `/api/status` | `:313-316` → `:615-642` | none | counts, port, whisper, turn_running, vibe_public, thinking (low) |
| ANY | `/api/events` | `:317-320` → `:644-666` | none | **all visible stored events** (content of every message; high) |
| ANY | `/api/session` | `:321-324` → `:836-852` | none | **human nsec during onboarding**, npub, **vibe join_token**, roster/channels/projects (critical) |
| ANY | `/api/chan-events` | `:325-328` → `:854-878` | none | channel signal events; `?since=N` also **acknowledges** (mutating read) (medium) |
| GET only | `/api/presence` | `:329-332` → `:880-892` | none | presence lines (low/medium) |
| ANY | `/api/skills` | `:333-336` | none | skill catalog JSON (low) |
| ANY | `/api/turn` | `:337-340` → `:2375-2391` | none | TURN mode/status (low) — **also swallows POST /api/turn** (see note) |
| ANY | `/api/ice` | `:341-344` → `:2393-2407` | none | **TURN username + password when enabled** (high) |
| GET only | `/api/provider` | `:345-346` → `:2191-2216` | none | provider metadata + has_* flags, hosts/models; no secrets echoed (low/medium) |
| GET only | `/api/complete` | `:347-348` → `:1994-2019` | none (capability token `?t=`) | completion text for the job token (medium) |
| POST | `/api/event` | `:2026-2027` → `:721-748` | **`logged_in`** (401) | stores a note as the logged-in human (high) |
| POST | `/api/presence` | `:2028-2030` → `:894-937` | **`logged_in`** (401) | publishes presence (low) |
| POST | `/api/identity` | `:2031-2032` → `:965-989` | none | create/import/logout the human identity; import accepts any nsec (critical) |
| POST | `/api/profile` | `:2033-2034` → `:991-1010` | none | writes roster profile (medium) |
| POST | `/api/member` | `:2035-2036` → `:1012-1025` | none | adds an npub to the roster (medium) |
| POST | `/api/agent` | `:2037-2038` → `:1027+` | none | create/update robots, providers, prompts (high) |
| POST | `/api/skill` | `:2039-2040` | none | skill assignment/edits (medium) |
| POST | `/api/skillui` | `:2041-2042` | none | skill UI state (medium) |
| POST | `/api/vibe` | `:2043-2044` → `:1424-1451` | none | create vibe / set visibility; mints join token (high) |
| POST | `/api/channel` | `:2045-2046` → `:1453-1473` | none | create/delete/group channels, prompts, rosters (high) |
| POST | `/api/group` | `:2047-2048` → `:1500-1509` | none | add channel group (medium) |
| POST | `/api/project` | `:2049-2050` → `:1733-1753` | none | **`system()` with caller path** (critical) |
| POST | `/api/canvas` | `:2051-2052` → `:1811-1840` | none (rel-path `..` check at `:1768-1775` only) | writes files under a registered project root (high) |
| POST | `/api/fixup` | `:2053-2054` → `:1867-1912` | none | spawns agent fixup work (high) |
| POST | `/api/complete` | `:2055-2056` → `:1970-1992` | none | starts a canvas completion job, returns token (medium) |
| POST | `/api/turn` | `:2057-2058` → `:2409-2437` | none | **unreachable** — line `:337` matches first for every method, so POST returns the GET status and TURN is never enabled |
| POST | `/api/signal` | `:2059-2061` → `:2439-2472` | none | injects a kind-25000 signal event; anonymous pubkey fallback `…0001` at `:2456-2459` (medium/high) |
| POST | `/api/close` | `:2062-2063` → `:2078-2084` | none | leave ack, relay keeps running (low) |
| POST | `/api/exit` | `:2064-2065` → `:2086-2092` | none | **stops the relay** (high) |
| POST | `/api/window` | `:2066-2067` → `:2094-2117` | none | minimize/maximize/undecorate the app window (low) |
| POST | `/api/provider` | `:2068-2069` → `:2252-2283` | none | **overwrites provider secrets/host/model** (critical) |
| POST | `/api/provider/scan` | `:2070-2071` → `:2317-2336` | none | **SSRF** via caller host, leaks caller-supplied key in URL (critical) |
| POST | `/api/provider/login` | `:2072-2073` → `:2338-2361` | none | spawns a provider login terminal (high) |
| OPTIONS | any path | `:306-309` | none | 204 + ACAO headers (low) |
| GET | `/`, `/index.html`, `/manifest.webmanifest`, `/sw.js`, icons | `:403-482` | none | static PWA shell (low) |
| any other | — | `:351-352`, `:2074-2075` | — | 404 |

Notes:

- The same TCP port also speaks the raw Nostr line protocol; protocol selection is a prefix sniff
  (`hush-c/src/hush_relay.c:491-492`), and `hush_handle_event_msg` stores parsed events with no
  signature verification (`:545-554`; `hush-c/include/hush_event.h:37` explicitly says
  "Does not verify signature"). Any HTTP-layer gate is therefore bypassable on the same socket.
- No `Set-Cookie` exists anywhere in `hush-c/` (grep: zero matches), so there is no cookie notion
  today.
- No `Authorization`/`X-*` header is parsed anywhere; only `Content-Length` is read
  (`hush-c/src/hush_http.c:369-384`).

---

## (c) Design constraints for a session-token gate

### PWA integration facts (answers to the "ALSO ANSWER" questions)

- **Same-origin.** `hush-c/demo/index.html:1509`:
  `const API = (location.protocol === "file:") ? "http://127.0.0.1:10555" : "";`
  When served by the relay (the normal case, and what `hush_ui_html.h` embeds via
  `scripts/embed-ui.sh`), all calls are relative → same origin. Only a `file://`-opened copy
  targets `http://127.0.0.1:10555` cross-origin.
- **How routes are called.** One helper funnels nearly everything —
  `index.html:1640-1649`:
  `fetch(API + path, opt)` where `opt` is `{}` for GET and
  `{method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(body)}` for POST.
  It does **not** set `credentials`, `mode`, or any auth header. A handful of raw calls bypass the
  helper: `/api/status`, `/api/events`, `/api/session`, `/api/presence` in `tick()`
  (`index.html:5721-5728`, polled every 1s via `setInterval(tick, 1000)` at `:6125`), and
  `/api/complete?t=…` (`:4451`). The service worker never intercepts `/api/`
  (`hush-c/demo/sw.js:33-34`), so API responses are not cached.
- **Credentials/cookies.** No `credentials` option and no cookie code; browser default
  `same-origin` would send a same-origin cookie if the server ever set one. Today the server sets
  no cookie (no `Set-Cookie` anywhere).
- **How `/api/session` is consumed.** `tick()` assigns `session = sess` (`index.html:5731`), then
  `paint()`; the one-time nsec is copied into the JS variable `pendingNsec`
  (`applySession`, `:1663-1673`, specifically `:1670`), rendered in the backup gate
  (`:1750`), copied to the clipboard (`:1839-1840`), and cleared when `backup_acked`
  (`:1671`). `join_token` is only written into DOM text for the human to copy
  (`:3281-3283`, `:3842-3843`); it is never posted back.
- **Token/cookie notion today.** None for sessions. The only client-supplied token is the
  per-job canvas/fixup completion token (`/api/complete?t=`, `hush-c/src/hush_http.c:1948-1968,
  1970-2019`), minted by `hush_canvas_start`/`hush_agent_start_fixup` for correlation, not
  authorization. `/api/member` does not take a join token.

### Where to mint

- Mint a process-lifetime random token at relay startup, next to the existing wiring in
  `hush_relay_run`: `hush_http_set_launch` / `hush_http_set_turn` are called at
  `hush-c/src/hush_relay.c:621-628`. Reuse the existing CSPRNG helper pattern
  `hush_launch_make_token` (`hush-c/src/hush_launch.c:1914-1938`: 8 bytes from `/dev/urandom` →
  16 hex chars); a session gate should use at least 16 bytes.
- The token is a **runtime secret**, so it must not be persisted into `vibe.json` or `pass`;
  persistence would turn it into a stored credential and survive restarts. Keep it in the
  `hush_http` module's static state or a new field on the launch struct that is never serialized.
- Alternative/stronger binding: derive the token per-start and print it to stdout on the listen
  line, so the terminal (and `hush-relay --close/--quit` operators) can retrieve it.

### How the PWA would attach it

Because the PWA is same-origin and funnels through one helper, the lowest-risk options are:

1. **Same-origin cookie set by the relay.** The relay already serves `/`; add `Set-Cookie:
   hush_session=<token>; HttpOnly; SameSite=Strict; Path=/` on asset replies. `fetch` default
   `credentials:"same-origin"` attaches it automatically, so `index.html` needs no API changes.
   Constraints: (a) `Access-Control-Allow-Origin: *` must be removed or made origin-specific —
   browsers refuse to expose credentialed responses to a wildcard origin; (b) the raw `tick()`
   fetches are same-origin so they are fine; (c) a `file://`-opened UI hits
   `http://127.0.0.1:10555` cross-origin and would need `credentials:"include"` plus an explicit
   allowed origin, or would simply stop working; (d) state-changing POSTs must still require a
   token in the body/header, because cross-origin HTML forms can POST cookies without CORS.
2. **Query parameter** (`?k=<token>`), mirroring `/api/complete?t=` and `hush_http_path`'s
   query stripping (`hush-c/src/hush_http.c:386-401`). Easy to add in the `api()` helper, but the
   token then lands in request lines/logs and must be re-added to the four raw `tick()` URLs and
   the `/api/complete` poll. The PWA must first learn the token; the natural carrier is an
   inline substitution in the served HTML or a `Set-Cookie` on `/`.
3. **Custom header** (`X-Hush-Token`). Cleanest semantically and forces a CORS preflight for
   cross-origin callers (which the wildcard ACAO currently permits). Cost: the relay has **no
   header parser** — only `Content-Length` is read via `strstr` (`hush-c/src/hush_http.c:369-384`),
   and `hush_http_path` only parses the request line. A small, bounded header lookup must be
   added. The PWA change is confined to `index.html:1640-1649` plus the raw `tick()` calls.

The bootstrap problem is the same in all three: the token has to reach the browser from the
process that minted it, and there is no existing channel (no cookie, no injected HTML variable).
`Set-Cookie` on `/` is the smallest new mechanism.

### Which routes to gate

- **Must gate (state/secret/destructive):** `/api/identity`, `/api/vibe`, `/api/channel`,
  `/api/group`, `/api/member`, `/api/profile`, `/api/agent`, `/api/skill`, `/api/skillui`,
  `/api/project`, `/api/canvas`, `/api/fixup`, `/api/complete` (POST), `/api/signal`,
  `/api/window`, `/api/close`, `/api/exit`, `/api/provider`, `/api/provider/scan`,
  `/api/provider/login`, `/api/event`, `/api/presence` (POST), `/api/chan-events` (because of the
  `?since=` ack side effect).
- **Should gate (secret reads):** `/api/session` (nsec + join token), `/api/ice` (TURN
  credential), `/api/events` (all message content).
- **May stay open (app shell / liveness):** `/` and static assets, `/api/status`, `/api/skills`
  (if considered public), `/api/turn` (GET), `/api/provider` (GET, metadata only), `/api/presence`
  (GET). Note that leaving `/api/status` open still leaks port/version/activity.
- **Bug to fix first:** `POST /api/turn` is dead code due to the method-agnostic guard at
  `hush-c/src/hush_http.c:337`; any gate work should add the missing `memcmp(req,"GET",3)` there
  (and to `/api/status`, `/api/events`, `/api/session`, `/api/chan-events`, `/api/skills`,
  `/api/ice`) so the intended POST handlers are reachable and can be gated independently.
- **Same-socket bypass:** gating HTTP alone leaves the Nostr line protocol open
  (`hush-c/src/hush_relay.c:491-515, 545-554`). A complete design must either gate the protocol
  sniff (reject non-HTTP until a token preamble) or accept that events can still be injected by
  any reachable client.

### Loopback and bind caveats

- The socket is `INADDR_ANY` (`hush-c/src/hush_relay.c:287`), so "localhost-only" is not true:
  any host on the LAN can reach the port. Either bind `127.0.0.1`/`::1` (add an opt-in
  `HUSH_BIND` for LAN use) or treat the token as the only boundary.
- `ACAO: *` plus no auth means any website the user visits can read relay responses and drive
  state-changing endpoints via `fetch` (the browser blocks only *reading* cross-origin responses,
  not sending simple requests). Removing the wildcard is part of the fix.
- DNS rebinding lets a malicious page reach `127.0.0.1` with a non-loopback `Host`; the relay
  never validates `Host`, so a token (not origin checks) is required.
- Local processes of other users can connect to `127.0.0.1:10555`; loopback is not a user
  boundary. The token must therefore be unguessable and compared in constant time, not merely
  "present".
- `/api/exit` and `/api/provider/scan` should get separate confirmation semantics even after a
  token exists (a token-holding page could otherwise kill the relay or pivot to internal hosts).

### Existing tests that call these routes and would need updating

There is no C unit test that drives `hush_http_serve` (grep across `hush-c/tests/*.c` is empty);
all route coverage is shell/python/CJS integration tests that start a relay and use credential-free
`curl`/HTTP. A gate breaks all of them unless they are taught to fetch and present the token:

- `hush-c/tests/check_launch.sh` — densest: `/api/identity` create/ack (`:354-365`),
  `/api/vibe` (`:366`), channels/groups, `/api/project` + git-init assertion (`:430-434`),
  `/api/canvas` (`:435-445`), `/api/member` (`:446`), `/api/agent` (`:450`), many
  `/api/session` reads, and it asserts the nsec appears/clears (`:358, 363`).
- `hush-c/tests/check_provider.sh` — `/api/provider` POST (`:111-121`), `/api/provider/scan`
  (`:124-128`), `/api/session`.
- `hush-c/tests/check_turn.sh` — `/api/turn` GET+POST (`:23, 55-58`, note it only greps
  `"compiled":`, which is why the dead POST route is not caught), `/api/ice` (`:26`),
  `/api/status` (`:28`), `/api/identity` (`:38-41`), `/api/vibe` with the join-token assertion
  (`:42-47`), `/api/signal` (`:48`), `/api/events` (`:52`).
- `hush-c/tests/check_exit.sh` — `/api/close` (`:99`), `/api/exit` (`:105`), `/api/session`.
- `hush-c/tests/check_agent.sh`, `check_codex.sh`, `check_failover.sh`, `check_ollama.sh`,
  `check_fixup.sh`, `check_complete.sh` — all do `/api/identity` + `/api/vibe` bootstrap and
  then POST work; `check_collaboration.py` (uses `/api/session`, `/api/channel`, `/api/event`,
  ... and at `:75` is the **only** test asserting the existing 401 for an unowned message),
  `check_collaboration_ui.cjs`, `check_pwa.sh` (`/api/complete`, `/api/fixup`, `/api/status`,
  `/api/window`), `check_win.py`, `check_browser_launch.py`.
- C-level tests that touch code a gate signature change would touch:
  `hush-c/tests/test_launch.c` (`hush_launch_format_session` at `:49, 56, 111, 138, 167, 198,
  208, 319, 347`; `hush_launch_add_project` at `:316`) and `hush-c/tests/test_turn.c` for the
  TURN formatting helpers. `make test` in `hush-c/Makefile:65-81` runs all of them.
- Test harnesses assume no credentials at all: they use bare `curl -sf` against
  `http://127.0.0.1:${port}` and would need a shared "read token from startup output" helper
  (e.g., extend the readiness loop at `check_turn.sh:14-21`).

---

## (d) Open questions

1. **Threat model.** Is the relay intended to be exposed beyond loopback (the code binds
   `INADDR_ANY`), or is binding 0.0.0.0 an oversight? The answer decides whether the token is a
   LAN boundary or only a hostile-webpage/local-process boundary.
2. **What should the gate protect first?** If the priority is "no arbitrary code execution",
   `/api/project` (system), `/api/provider/scan` (SSRF), `/api/canvas`+`/api/fixup`+`/api/agent`
   (spawn agents), and `/api/exit` can be gated without breaking the onboarding banner reads; if
   it is "no secret exfiltration", `/api/session`, `/api/ice`, and `/api/events` must be gated too,
   which breaks the current unauthenticated `tick()` polling model.
3. **Nostr protocol path.** Should the raw line protocol require the token, or be disabled
   entirely on the UI port? Without a decision, an HTTP-only gate is cosmetic
   (`hush-c/src/hush_relay.c:491-515`).
4. **No signature verification.** `hush_handle_event_msg` stores events without checking the
   Nostr signature (`hush-c/include/hush_event.h:37`). Is that deliberate for the local-only
   design, and does it change once the socket is reachable from the LAN?
5. **Token lifetime and rotation.** Per-process random token (restart invalidates open tabs) vs.
   persisted secret (survives restart, becomes another credential to protect)? No persistence
   exists for such a token today.
6. **Same-origin cookie vs. explicit token.** Does the project accept a `Set-Cookie` +
   `SameSite=Strict` model (requires dropping `ACAO: *`), or does it insist on an explicit
   client-side token to keep the server stateless and CORS-open? The `file://` PWA mode
   (`index.html:1509`) only works in the cookie model with `credentials:"include"` plus a
   reflected origin.
7. **`POST /api/turn` dead route.** Confirm intent: the PWA (`index.html:3855`) and
   `check_turn.sh:55` both POST, but `hush-c/src/hush_http.c:337` intercepts it. Is TURN
   enable/disable currently expected to work from the UI at all?
8. **`/tmp` hardening scope.** Is fixing the predictable-dir trust (claim 8) in scope for the
   same change set, given a session token does not protect against a hostile local user
   pre-creating `/tmp/hush` or `/tmp/hush-agent-cwd`?
9. **Should `/api/member` / channel rosters ever enforce membership?** The join token exists in
   the data model but has no semantics; either give it a request-path meaning (and document it)
   or delete it from the session payload to reduce false assurance.
