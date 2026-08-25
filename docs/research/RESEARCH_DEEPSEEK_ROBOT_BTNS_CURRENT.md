# RESEARCH — Deepseek API provider + compact robot-edit buttons (Current Base Audit)

Methodology: **RDAP** verification gate on fresh worktree.
Worktree: `/opt/repo/hush/worktrees/deepseek-robot-btns`
Branch: `gb/deepseek-robot-btns`
Base: `main` `77fe023e3` (post #78 conv-intel-policy)

## Base State

PLAN_DEEPSEEK_ROBOT_BTNS.md is a verification/hygiene slice. The implementation is already present on this base from prior provider/robot-cards work:

**Provider id (9 runtimes):**
- HUSH_PROVIDER_COUNT = 9
- HUSH_ROSTER_PROVIDER_DEEPSEEK "deepseek-api"
- HUSH_PROVIDER_HOST_DEEPSEEK "https://api.deepseek.com"
- Meta row: { "deepseek-api", "Deepseek API", api family, host }
- Roster table includes deepseek-api
- UI PROVIDERS map includes "deepseek-api": "Deepseek API"
- Radio present: <input ... value="deepseek-api"> Deepseek API
- check_provider.sh GET includes "deepseek-api"

**Compact robot-edit buttons:**
- #agent-drawer .actions scoped CSS (nowrap, gap 6px, 36px min-height, danger auto width)
- Buttons inside .actions: Raise/Save Robot, Close, Delete Robot
- JS labels: resetAgentDraft/openAgentDrawer use "Raise Robot" / "Save Robot"
- Delete stays inside actions row, scoped danger
- check_launch greps for deepseek-api, Save/Raise Robot, Delete Robot (and old confirm text if present)

**Tests:**
- make -C hush-c test → ALL PASS
- test_provider: is_id("deepseek-api"), default host, family api, count==9
- test_roster: is_provider("deepseek-api")
- check_provider: deepseek-api in GET; optional POST save
- check_launch: deepseek-api, Save/Raise Robot, Delete Robot

## Verification Evidence (executed this worktree)

- Build: ./configure && make -C hush-c succeeds
- make -C hush-c test → ALL PASS
- Explicit greps:
  - provider/roster headers: COUNT=9, deepseek-api define
  - provider.c: meta row for deepseek-api
  - HTML: radio, deepseek-api in PROVIDERS, compact .actions CSS + labels
  - check_launch: deepseek-api + robot button labels
- No new C required

## Differences from original PLAN base

- Current base is later. Deepseek as 9th HTTP API radio and compact one-line drawer actions (Save/Raise Robot, Close, Delete Robot) were implemented in provider/robot-cards slices and are already on main.
- This worktree performs research/audit gate + verification + hygiene (matching the pattern of rail-prov, canvas-fim, provider-*, oauth-*, thread-*, onboard-*, splash-*, payne-*, vibe-*, pills-*, close-x, code-canvas, conv-intel, etc.) to close PLAN_DEEPSEEK_ROBOT_BTNS.md per user directive.

## Conclusion

Implementation satisfies every Success Criteria item.
No code changes needed.
H4 lock (9 providers, deepseek-api as HTTP API, compact scoped drawer actions, secrets in pass only) holds.

Proceed to VERIFIED.md + commit + full PR lifecycle.

## Commands executed
- git worktree add -b gb/deepseek-robot-btns from clean main
- make -C hush-c clean && make
- make -C hush-c test (ALL PASS)
- rg/grep for deepseek-api, HUSH_*_COUNT=9, Save Robot, Delete Robot, .actions scoped CSS
- Source + HTML + test inspection
