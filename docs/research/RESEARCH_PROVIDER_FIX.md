# RESEARCH: deepseek turn failure + provider selector UI

**Scope.** Two linked complaints: (1) a chat with Major stopped working
mid-conversation with "did not return a usable reply through grok-build"
after the robots were flipped to the Deepseek API; (2) the agent editor's
provider selector colors are inconsistent/annoying and need three columns
plus explicit auth/token text labels.

## (a) Conversation failure — evidence chain

- `~/.hush/store.log` shows the failed job ran through **grok-build**
  (the failure note names it), i.e. the turn predates the deepseek flip or
  silently fell back to grok-build after it.
- The roster (`vibe.json`) ranks `deepseek-api` first, then
  `grok-build`, `copilot`.
- The live provider overlay (`~/.hush/config/providers.json`) has
  `{"deepseek-api":{"use_home":"false","host":"https://api.deepseek.com",
  "model":"","has_key":"true"}}` — **model is empty**.
- `hush_provider_ready()` requires `model != ""` for API providers, so
  deepseek-api is *not ready*; `hush_agent_pick_provider()` silently
  falls back to grok-build (intended failover — see check_failover.sh).
- Result: the user believes they are on Deepseek, the job actually runs
  grok-build, and the failure notice gives no clue why.
- The Deepseek API transport itself works: `/v1/models` lists
  deepseek-flash and deepseek-v4-pro with the stored key, and a streamed
  chat completion returns normal `content` deltas (reasoning_content and
  `content:null` chunks are skipped by the existing parser; `[DONE]`
  terminates correctly).

## (b) Provider selector UI

- `#agent-providers` grid is `1fr 1fr` (two columns); `.ready` labels
  get an accent border **and** an accent-dim background fill; `.picked`
  gets another border shade. The fills are the "useless and annoying"
  color difference.
- Status data already exists per provider (`ready`, `has_key`,
  `has_token`, `has_home`, `has_binary`, `family`) but is not
  shown as text.

## (b2) Follow-on: worker failures were invisible

The live relay kept failing with the generic note, so the worker's failure
surface was instrumented end-to-end:

- A dying worker writes HUSH_JOB_ERR:<reason> into its output stream; the
  relay splits the reason into job->diag and never publishes the marker
  (verified with a fake failing grok: the note now reads "worker exited
  with an error").
- The API transport now captures curl's own stderr and embeds it
  ("provider transport failed (curl: ...)"), so the next live failure
  names the actual network/HTTP problem.
- note_failure prefers the worker's diagnosis over the generic
  provider-config hint.

With the real overlay, host, model, and key, the full deepseek turn was
verified end-to-end against the live API from a scratch relay: the robot
answered a real mention.

## (c) Plan

1. **M1.2** — `hush_provider_missing_reason()` in hush_provider.c/h:
   builds "no model selected" / "no API token stored" / "not logged in" /
   "runtime not installed" / "" from a status struct.
2. **M1.3** — agent_dispatch.c: when a job starts on a fallback (the
   robot's first-choice provider is unready), post a one-line notice
   "<Primary> is not ready (<reason>); using <Fallback>." Also append
   the reason to `note_failure` when non-empty.
3. **M2.1** — index.html: three-column `#agent-providers` grid; remove
   the ready/picked fills (plain border only); add a small status line per
   label: API family → "api token present"/"no api token", local →
   "runtime installed"/"not installed", everything else →
   "authenticated"/"not authenticated".
4. **M2.2** — provider drawer: when an API provider saves with a host but
   no model, show "No model selected — turns skip this provider until you
   scan or type one."
5. **M3** — suite green incl. clean rebuild; check_failover.sh still
   passes (its assertions are presence-based, tolerant of the new notice).
6. **M4** — docs + PR + land; then set the live overlay model so the
   operator's next conversation runs deepseek.

## (d) Risks

1. **Notice noise** — a fallback notice posts on every job start while the
   primary is unready. Accepted: it is the only way the operator learns
   the flip did not take effect.
2. **UI test breakage** — check_collaboration_ui.cjs drives
   `.agent-provider-cb` checkboxes and the drawer; it does not assert
   grid columns or colors, so the restyle is safe if the checkbox classes
   stay.
3. **Live overlay edit** — setting `model` in `~/.hush/config/
   providers.json` is operator config, not a repo change; reported
   explicitly.
