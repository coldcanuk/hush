# PLAN: split hush_http.c into legible API modules (slice 4)

Follows RDAP. Research gate: `docs/research/RESEARCH_HTTP_SPLIT.md`.
Worktree `worktrees/http-split` on `gb/http-split`; commit per milestone;
land via PR only. Every moved function stays verbatim; only visibility
(`static`) and shared names change.

## Milestones

### M4.1 — Research + plan (this gate)

### M4.2 — Internal header + core shared surface
- New `hush-c/include/hush_http_internal.h`: accessors, shared helpers,
  module entry points (added incrementally over M4.3-M4.7).
- `hush_http.c`: drop `static` from the shared helpers and add their
  prototypes to the internal header; rename `hush_json_*` →
  `hush_http_json_*` everywhere (sed). No code moves yet.
- Verify: `./configure && make && make test` green.

### M4.3 — `http_static.c`
- Move `serve_asset` + `serve_icon_panel` + tables (entry
  `hush_http_static_serve`). Delete from core.
- Verify: `make test` green (check_pwa.sh covers assets).

### M4.4 — `api_status.c`
- Move status/events/session/chan-events/presence family (entries
  `hush_http_status_serve`, `hush_http_events_serve`,
  `hush_http_session_serve`, `hush_http_chan_events_serve`,
  `hush_http_presence_get_serve`, `hush_http_presence_post_serve`).
- Verify: `make test` green.

### M4.5 — `api_identity.c` + `api_agents.c`
- Move identity/profile/member/vibe (entries `hush_http_identity_serve`,
  `hush_http_profile_serve`, `hush_http_member_serve`,
  `hush_http_vibe_serve`) and agent/payne/skill/skillui (entries
  `hush_http_agent_serve`, `hush_http_agent_delete`,
  `hush_http_skills_serve`, `hush_http_skill_post_serve`,
  `hush_http_skillui_serve`).
- Verify: `make test` green (check_agent.sh exercises the real dispatch).

### M4.6 — `api_channels.c`
- Move channel/group/policy/note helpers/project/signal + the note POST
  (entries `hush_http_post_serve`, `hush_http_channel_serve`,
  `hush_http_group_serve`, `hush_http_project_serve`,
  `hush_http_signal_serve`).
- Verify: `make test` green (check_collaboration.py + check_launch.sh).

### M4.7 — `api_canvas.c` + `api_turn.c` + `api_provider.c`
- Move canvas/complete/fixup/cancel/reply (entries
  `hush_http_canvas_serve`, `hush_http_cancel_serve`,
  `hush_http_reply_serve`, `hush_http_fixup_serve`,
  `hush_http_complete_post_serve`, `hush_http_complete_get_serve`);
  turn/ice (entries `hush_http_turn_post_serve`,
  `hush_http_turn_get_serve`, `hush_http_ice_serve`); provider family
  (entries `hush_http_provider_get_serve`, `hush_http_provider_post_serve`,
  `hush_http_provider_scan_serve`, `hush_http_provider_login_serve`).
- Verify: `make test` green (check_fixup/complete/turn/provider suites).

### M4.8 — Docs + land
- NOSTR/SECURITY untouched (no behavior change); CHANGELOG entry; line-count
  before/after in the PR body. Push, PR, auto-merge, cleanup, clean
  `make test` on main.
