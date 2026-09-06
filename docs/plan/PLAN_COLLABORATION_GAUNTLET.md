# Collaboration gauntlet

## Scope and synthesis

Keep a corporate, calm collaboration workspace. Nostalgia belongs in avatars;
game lessons inform clear selection, immediate feedback, and reversible skill
assignment. Human goals, readable conversations, and reliable execution lead.

The baseline strict build and isolated full suite pass. Browser onboarding
works. Source and browser inspection expose these concrete gaps:

- API and Cline selections fall through to Grok; planning/election force Grok.
- Room instructions share a 255-byte topic field and the UI allows only 240
  characters. Creation cannot supply instructions.
- Conversation ownership is an unused channel-level table; it does not guide
  replies. Thread replies use the currently selected channel, not their root.
- Both composers erase drafts before the server accepts a message.
- Robot creation has duplicate labels and a dense, game-themed skill board.
- Chat history already persists in a bounded event ring. Verify restart,
  context scoping, and follow-ups before claiming conversational memory.
- The existing integration suite needs an isolated password store; otherwise
  the cold-session check may restore a real developer identity.

## Milestones

1. Room guidance: distinct persistent system prompt, maximum 500 Unicode
   characters, server validation, defaults on every creation path, creation
   and management UI, and injection into every room agent job.
2. Operational providers: dispatch only through a robot's ranked providers;
   real API request/response adapters; route team planning through the chosen
   harness; accurate readiness and failure messages. Test with controlled
   local HTTP endpoints/CLI fixtures, separately from live credentials.
3. Conversations: root-scoped ownership/context, reliable 1:1 and ordered
   team follow-ups, draft retention on failed sends, persisted history checks.
4. Workspace UI: readable robot names, clear create/edit/skill actions,
   restrained olive styling and ordinary workplace language. Browser checks
   at desktop and narrow widths plus interaction and request assertions.
5. Full gauntlet: strict build, full tests, focused browser flows, final diff
   and C checklist. Stop when every applicable category is at least 7.8.
   Commit/push each milestone on gb/collaboration-gauntlet; PR review and
   auto-merge; remove the merged worktree and branch.

## Baseline (iteration 0)

C1 8.5; C2 8.0; C3 3.0; C4 6.5; C5 6.0; C6 4.0; C7 6.0; C8 6.0;
C9 8.5; C10 8.5. Default weights: S=6.5, F=3.0, M=6, H=6. HARD FAIL.

Evidence: `/tmp/hush-gauntlet-build-baseline.log`,
`/tmp/hush-gauntlet-tests-baseline.log`, browser onboarding and screenshots
under `/tmp/hush-gauntlet-runtime/`, and the dispatch/room/composer source
audit. These are local verification artifacts, not shipped application data.
Scores are engineering judgments; no measured Bayesian probability is claimed.

## Protocol references

- [OpenAI Chat Completions](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)
- [Gemini generateContent](https://ai.google.dev/api/generate-content)
- [Cline CLI](https://docs.cline.bot/cli/cli-reference)

Use the existing C11 architecture and curl transport; add no frontend framework
or decorative dependency. API keys stay in pass and never enter argv or logs.

## Delivered behavior and review boundaries

- Current workspaces initialize their built-in skill files before creating the
  roster. No compatibility-only skill fallback or old Cline config lookup remains.
- Every channel creation path supplies nonempty guidance. The separate prompt
  accepts at most 500 Unicode scalar values; invalid updates fail before mutation.
  Prompt-only updates retain room participants. Explicit empty lists clear them.
- All six API selections send real provider-specific requests through curl.
  OpenAI uses the developer role, Anthropic uses its system field, and Gemini
  uses systemInstruction. Gemini thought blocks are excluded from chat output.
  Cline uses its current headless JSON CLI and current configuration directory.
  Planning and election use the selected integration too.
- Readiness means local prerequisites are present, not that authentication was
  proven. Ranking chooses the first locally available provider. A failed launched
  request produces a visible error; it does not automatically rerun tool actions
  through another harness. Live vendor accounts were not exercised by the tests.
- Skills contribute their actual instruction files. Removing a skill removes
  those instructions from later requests. Missing or oversized instructions stop
  the request instead of substituting an invented result.
- Replies retain their conversation root, original human owner, and room.
  New human turns reset bounded team-turn accounting; election, planning and
  sequential handoffs work on repeated requests in the same conversation.
- Memory is bounded conversation context, not semantic or unlimited memory:
  a persisted 1,024-event ring, excerpts of the opening note and six recent work
  notes (384-byte snippet budget, preserving UTF-8 boundaries), and the full current message. Events
  include metadata/presence, so this is not a promise of 1,024 chat messages.
  An evicted root cannot be resumed through the thread API.
- Named robot rows, room options, a normal create-channel dialog, explicit team
  provider selection, reversible skill assignment, and phone navigation preserve
  an ordinary collaboration application. Retro art stays in the avatar picker.
  Profile metadata and protocol traffic stay out of the chat view.
- Team creation writes the visible shared brief into real robots and assigns
  them to a room. It is accurately described as user-driven creation, not as an
  AI-generated team. Partial creation failures report the robots already created.

## Reproducible verification

Run from the worktree:

```sh
./configure && make
PASSWORD_STORE_DIR=/tmp/hush-gauntlet-test-pass make test
```

`make test` includes scoped JSON/Unicode unit cases and
`hush-c/tests/check_collaboration.py`. The latter owns temporary storage, HTTP
endpoints, test-only secrets and executable fixtures. Coverage includes six API
protocols and headers, selected model/identity, room and skill injection, restart,
long threads, repeated team handoffs, Cline progress filtering and exit status,
500/501 Unicode prompt boundaries, a 4,096-byte reply and oversized rejection,
and a complete multi-megabyte event response under socket backpressure.

Optional real-browser verification requires an existing Playwright installation
and browser; it installs no application dependency:

```sh
HUSH_PLAYWRIGHT_MODULE=/path/to/playwright \
HUSH_BROWSER_EXECUTABLE=/path/to/chrome \
HUSH_TEST_UI_ARTIFACTS=/tmp/hush-gauntlet-browser \
node hush-c/tests/check_collaboration_ui.cjs
```

The browser test starts and removes its own isolated workspace. It verifies room
creation/settings, Unicode boundaries, robot creation, avatar selection, ordered
providers and the correct Configure button, assigned skills persisted and removed,
1:1 continuation after room navigation, both failed-send drafts, actual team
creation, desktop/390px layouts, and absence of JavaScript errors.

Review evidence lives locally in `/tmp/hush-gauntlet-build-final.log`,
`/tmp/hush-gauntlet-tests-final.log`, `/tmp/hush-gauntlet-browser.log`, and
`/tmp/hush-gauntlet-browser/`. The targeted address/undefined sanitizer run used
`/tmp/hush-gauntlet-asan-relay` and `HUSH_TEST_RELAY_BIN` with the integration
suite; leak detection was disabled. This is no claim of a leak audit.

The C review checked touched function lengths, bounded input/output, cleanup of
worker pipes and child processes, private credential transport, and the final
whitespace diff. Existing callback signatures are retained where callers depend
on them; the new channel writer groups buffer and cursor state explicitly.
No dependencies, generated artifacts, or test fixtures enter application flows.
## Final gauntlet (iteration 1)

```text
=== HUSH GAUNTLET SCORECARD ===
Feature: collaboration-gauntlet
Iteration: 1

C1 Build Clean       s=8.7  w=1.0  evid: strict configure/build passes; no warnings
C2 Tests Green       s=8.5  w=1.0  evid: full suite and focused Unicode regression pass
C3 No Phantoms       s=8.3  w=1.2  evid: real dispatch, saved skills, explicit failure paths
C4 Legible C11       s=8.0  w=1.0  evid: touched-function, bounds, cleanup and sanitizer checks
C5 Zero Bloat        s=8.0  w=1.0  evid: no added dependencies; obsolete controls removed
C6 Functionality     s=8.2  w=1.2  evid: API/CLI fixtures, repeated team handoffs, browser flows
C7 UX Chill          s=8.2  w=1.0  evid: desktop/phone proof; readable rooms, robots and chat
C8 ARPG Feel         s=8.1  w=0.8  evid: immediate, reversible skill and provider selection
C9 Process Law       s=8.5  w=1.0  evid: all changes isolated on gb/collaboration-gauntlet
C10 Evidence Honesty s=8.8  w=1.2  evid: reproducible tests; live-account and memory limits documented

S=8.3  F=8.0  M=0  H=0
Verdict: ACCEPTABLE PASS
Next:
```

Unrounded composite: 8.340384615384615. All categories apply. The final review
also added a focused regression for a Unicode opening message whose excerpt
ends inside an emoji; incomplete trailing scalars are removed before transport.
The full suite passed before that localized correction, and the complete
collaboration integration test passed again afterward. Browser behavior passed
on the final UI. No aspire-only iteration follows this pass.
