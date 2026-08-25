# RESEARCH: Robot Cards UX + Modern Raise Form

Base: main 745103e0c (post server-ack-notes M5)

## Current Implementation Audit (verbatim)

### Backend (roster + launch + http)
- HUSH_ROSTER_CONTEXT_MAX = 3 (header)
- hush_roster_is_provider: 9 providers (goose, grok-build, codex, cline, gemini-api, xai-api, openai-api, anthropic-api, deepseek-api)
- hush_roster_fill_agent: requires name, prompt (non-empty), provider (is_provider)
- hush_roster_fill_context: rejects ncontext > 3, bad MIME via copy_context
- hush_roster_remove_agent: refuses PAYNE_SLUG, compacts
- http: /api/agent accepts "system_prompt", "provider", "context_name_*" etc. up to 3; delete by slug
- launch: add_agent / remove_agent delegate to roster; session JSON emits provider + prompt preview + ncontext

Evidence (hush_roster.c):
```
if (agent->prompt[0] == '\0') return HUSH_ERR_PARSE;
if (!hush_roster_is_provider(agent->provider)) return HUSH_ERR_PARSE;
if (in->ncontext > (size_t)HUSH_ROSTER_CONTEXT_MAX) return HUSH_ERR_FULL;
```

### UI (demo/index.html)
- `#robot-list` + `paintRobots()`: renders .robot-card (open/closed), +/- toggle, "Edit", call button
- Drawer (#agent-drawer): name input + pill, system prompt textarea + pill, context files (max 3 pills + +/-), 9 provider radios, "btn danger" delete, save
- JS: autoRobotName, paintNamePill/paintPromptPill/paintFilePills, chosenProvider, draftFiles limit, delete calls action=delete
- Help text still has one "standing orders" reference (line ~676)

### Tests
- test_roster.c: requires prompt + provider, rejects bad MIME, rejects > context max, remove refuses Payne, JSON contains provider/prompt/ncontext
- check_launch.sh: greps for "System Prompt", "agent-provider", "paintRobots", "id=\"robot-list\"", danger delete styles

### Gaps vs PLAN_ROBOT_CARDS_UX Success Criteria
1. "standing orders" string still appears in one rail help (minor).
2. Plan calls for "pill-commit" UX — already implemented (name/prompt pills + + buttons).
3. Red delete present and wired for non-Payne.
4. Server already rejects missing prompt/provider/4th file/bad MIME.
5. No separate RESEARCH file existed for this plan (plan itself served as frozen spec).
6. Some help text and one rail-pop still use legacy "standing orders".

### Risks (unchanged from plan)
- Session JSON growth: already using preview (160 chars) + ncontext count.
- No live LLM: out of scope.
- Delete only roster slot (no pass nsec purge required by plan).

### Conclusion for Atomic Work
Backend already satisfies M3 requirements. UI has cards + modern drawer. Remaining atomic work is:
- Hygiene: replace last "standing orders"
- Full verification that pill UX + delete + caps + provider radios match spec exactly
- M1 re-audit + docs, small UI strings, embed, test greps, critic gate, then PR lifecycle.

No large C changes expected. Focus on polish + process.
