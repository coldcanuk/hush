# RESEARCH: splitting hush_http.c into legible API modules

**Scope.** P1 slice 4: decompose `hush_http.c` (2,874 lines) into a slim
router/core plus per-family API modules behind one internal header. Strictly
behavior-preserving; the full `make test` suite is the acceptance gate after
every move. `hush_agent.c` (4,524 lines) is the follow-on slice.
**Baseline.** `main` @ `e034ffc78` (rate limits merged, PR #163); suite green.
Worktree `worktrees/http-split`, branch `gb/http-split`.

## (a) Verified structure

The file is already family-ordered (prototype block at 119-299 mirrors
definition order), so each family is a contiguous region:

| Family | Prototypes | Includes |
|---|---|---|
| core: dispatch, guard, auth, rate, reply/session, JSON, headers, limits | 107-133, 283-299, 394, 2467 | `hush_http_serve` entry, `serve_api_post` router |
| status/events/session/presence | 135-148 | `serve_status/events/session/chan_events/presence_*` |
| note POST + identity + profile + member | 149-154 | `serve_post` (note ingest), identity routes |
| agent + payne + skills + skillui | 155-180 | agent CRUD, loadout, context slots |
| vibe + channel + group + policy | 181-204 | channel manage/policy, indexed collect |
| note helpers + project + signal | 205-228 | mentions, reply-to, project |
| canvas + complete + fixup + cancel + reply | 229-246 | canvas FIM + provider one-shots |
| turn + ice | 247-252 | coturn control |
| lifecycle (close/exit/window) | 258-262 | stays in core |
| provider | 264-280 | provider CRUD/scan/login |

Shared statics: `g_launch`, `g_turn`, `g_set_cookie`, `g_context_text`,
`g_ip_req`, `g_complete_lim`, `g_fixup_lim`, `g_limits_ready`.
Heavily shared helpers (293 use sites total): `hush_http_reply`,
`hush_http_reply_session`, `hush_json_field`, `hush_http_body`.

## (b) Design

1. **One internal header** `hush_http_internal.h` (new, not installed): the
   accessors (`hush_http_launch()`, `hush_http_turn()`), the shared reply/
   write/body/JSON/header helpers, and every module entry point the router
   calls. Everything else stays `static` inside its module.
2. **Module map** (moved functions stay verbatim; only `static` drops from
   entry points):
   - `http_static.c` — `serve_asset`, `serve_icon_panel`, asset/panel tables
   - `api_status.c` — status/events/session/chan-events/presence
   - `api_identity.c` — identity/profile/member/vibe(+rotate)
   - `api_agents.c` — agent/payne/skills/skillui/context
   - `api_channels.c` — channel/group/policy/note helpers/project/signal
   - `api_canvas.c` — canvas/complete/fixup/cancel/reply
   - `api_turn.c` — turn/ice
   - `api_provider.c` — provider routes
   - `hush_http.c` keeps the server core: sniff helpers, guard, host/auth/
     rate checks, limits init, setters, dispatch router, lifecycle routes.
3. **JSON helpers** rename `hush_json_field/has_key/bare_field/unescape_copy`
   → `hush_http_json_*` (they are HTTP-private; the shared `hush_json.h`
   module keeps its own surface).
4. **Mechanics**: families are contiguous, so each move is an awk line-range
   extraction into the new file plus a sed range delete; `static` drops only
   on entry points; the internal header gains their prototypes; the router's
   call sites stay unchanged (same names, same signatures).
5. **Verification**: compile + targeted suites after each move; full
   `make test` at every milestone boundary; `git diff --stat` sanity
   (net line delta ≈ 0 apart from headers/comments).

## (c) Risks

1. **Hidden cross-family deps** — mitigated by compiling after each move;
   -Werror catches undeclared uses immediately; the internal header is the
   only repair surface.
2. **Sed drift** — mitigated by verifying each extracted range's first/last
   lines before delete.
3. **Behavior change** — none intended; the full suite is the gate, and any
   fix must be a pure visibility/name repair.

## (d) Final state (all milestones landed)

| Module | Lines | Owns |
|---|---|---|
| `hush_http.c` | 977 | guard, host/auth/rate checks, limits, reply/session/JSON helpers, dispatch router, lifecycle |
| `http_static.c` | 91 | PWA assets, icon panels |
| `api_status.c` | 245 | status/events/session/chan-events/presence |
| `api_identity.c` | 141 | identity/profile/member/vibe |
| `api_agents.c` | 436 | agent/payne/skills/skillui/context |
| `api_channels.c` | 519 | note POST/channel/group/project/signal |
| `api_canvas.c` | 374 | canvas/complete/fixup/reply/cancel |
| `api_provider.c` | 250 | provider CRUD/scan/login |
| `api_turn.c` | 75 | coturn control/ICE |

`hush_agent.c` (4,524 lines) remains the follow-on split.
