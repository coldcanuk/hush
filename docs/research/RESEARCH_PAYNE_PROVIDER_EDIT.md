# RESEARCH — Edit Sgt. Major Payne (provider + order only)

Worktree: `/opt/repo/hush/worktrees/payne-provider-edit`
Branch: `gb/payne-provider-edit`
Base: `main` `00f10480f` (PR #44)

Methodology: RDAP. Mode: **TOOLED**. Code + tests + UI_SPEC only.

## Phase 0 — Context Register

- Consulted: `PRIME_DIRECTIVE.md`, `UI_SPEC.md` §9 / §11 / robot cards,
  `README.md`, `hush-c/demo/index.html` (`robotModels`, `paintRobots`,
  `openAgentDrawer`, `#agent-drawer`), `hush_launch.h/.c`,
  `hush_roster.h/.c`, `hush_provider.h`, `hush_agent.c`
  (`hush_agent_lookup_robot`, `hush_agent_handle_mention`),
  `hush_http.c` (`hush_http_serve_agent`), `test_roster.c`,
  `test_launch.c`, `check_launch.sh`,
  `RESEARCH_DEEPSEEK_ROBOT_BTNS.md` (Payne still undeletable).
- No live hive required for this slice. The request is a missing
  Edit affordance, not an incident.
- write-legible-c §14 applies to every C change.

## Phase 1 — Evidence

**E1** Payne is a seeded identity, not a roster agent.
`hush_launch_t` holds `hush_identity_t payne`. Session JSON emits
`payne:{name,npub,pubkey,about}` from `HUSH_LAUNCH_PAYNE_NAME` /
`HUSH_LAUNCH_PAYNE_ABOUT` constants. No `provider` field today.

**E2** UI hard-codes Payne as locked Goose:

```
slug: "sgt-major-payne",
provider: "goose",
locked: true
```

`paintRobots` paints **Edit** only when `!bot.locked`. Payne has
no Edit button.

**E3** `openAgentDrawer(bot)` only enters edit mode when
`bot && bot.slug && !bot.locked`. A locked Payne never prefills
the drawer and never sets `editSlug`.

**E4** Delete already refuses Payne: UI
`if (editSlug === "sgt-major-payne")` → `"Payne stays on deck."`
C `hush_roster_remove_agent` / `hush_launch_remove_agent` return
`HUSH_ERR_DENIED` for `HUSH_ROSTER_PAYNE_SLUG`.
`test_roster.c` asserts that refuse.

**E5** `POST /api/agent` only creates (`hush_launch_add_agent`)
or deletes (`action:"delete"`). There is no update path. Creating
an agent named "Sgt Major Payne" would slugify to
`sgt-major-payne` and collide with the seeded slug.

**E6** Agent dispatch looks Payne up separately and forces Goose:

```
out->provider = HUSH_ROSTER_PROVIDER_GOOSE;
out->prompt = HUSH_LAUNCH_PAYNE_ABOUT;
```

Live spawn only runs when `bot.provider == grok-build` and
`hush_agent_grok_ready()`. Every other provider posts the
on-deck note. Changing Payne's stored provider to `grok-build`
is the only way this hive will actually start a job for him.

**E7** Raised robots store one `provider` string
(`HUSH_ROSTER_PROVIDER_MAX = 32`). vibe.json persists
`agent_provider_N`. There is no `provider_1` / order list on
any robot, including Payne. Session `payne` object has no
provider keys.

**E8** UI_SPEC §9: Delete is enabled when editing an existing
**non-Payne** robot. "Payne cannot be deleted." Deepseek
research explicitly left "Changing Payne's locked robot" out
of scope. Robot cards research: Payne first; Edit on
non-Payne only.

**E9** `HUSH_PROVIDER_COUNT = 9`. Wire ids: `goose`,
`grok-build`, `codex`, `cline`, `gemini-api`, `xai-api`,
`openai-api`, `anthropic-api`, `deepseek-api`. Configure
drawer already exists (`#provider-drawer`).

**E10** Name and about are compile-time constants. Session
always reprints `HUSH_LAUNCH_PAYNE_NAME` /
`HUSH_LAUNCH_PAYNE_ABOUT`. There is no persist field for a
renamed Payne and no prompt editor for him.

## Assumptions (tagged)

- Operator wants Edit on the Payne card. `verified` by request.
- Name and standing orders stay locked. `verified` by request:
  "can't rename or update the instructions."
- "Which AI provider to use and in which order" means a ranked
  list, not a single radio. `assumption` until synthesis lock;
  request text is the source.
- Live SaaS spawn for API-family providers is **not** this
  slice. `verified` by E6 and prior Deepseek non-goal.
- Pass checkbox default-on stays. `verified` UI_SPEC.

## What "order" means (locked)

A ranked list of distinct provider ids. First entry is the
primary. Later entries are fallbacks **when a later slice
implements a live client for that family**. This slice:

1. Stores the ranked list on Payne.
2. Surfaces it in the Payne edit drawer.
3. Uses `providers[0]` as `hush_agent` lookup's provider so a
   first-choice of `grok-build` can already spawn (E6).
4. Does **not** walk fallbacks at job time (no second spawn
   path, no API HTTP client).

Empty / missing list restores to `[goose]` so existing hives
keep today's behavior.

## Locked architecture

### Identity stays locked

| Field | Rule |
|---|---|
| Name | Always `Sgt Major Payne`. Input disabled / omitted. |
| Slug | Always `sgt-major-payne`. |
| About / system prompt | Always `HUSH_LAUNCH_PAYNE_ABOUT`. Prompt field disabled / omitted. |
| nsec / npub | Unchanged. Still `pass` + vibe restore. |
| Delete | Still denied. Delete button stays disabled. |
| Context files | Not on Payne. Hide the file row in Payne edit. |
| pass checkbox | Hide on Payne edit (no new nsec). |

### What the human may change

A ranked list of 1…`HUSH_LAUNCH_PAYNE_PROVIDERS_MAX` (named
constant **4**) distinct valid provider ids.

UI: reuse the existing nine radios as **add-to-order**, plus a
pill row `#payne-provider-pills`. `+` on a checked radio appends
if not already present and under cap. Each pill has `−` to drop
and ▲ / ▼ to reorder (or drag-free up/down buttons; Hick: two
44px movers, not a freeform text box). Primary is index 0.
Collapsed card subtitle shows `providerLabel(providers[0])`
plus `+N` when more follow.

Copy (Payne voice): “Choose the runtime. First on deck speaks
first.”

### Persist

On `hush_launch_t`:

```
char payne_providers[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX];
size_t npayne_providers;
```

vibe.json string fields (same style as `agent_provider_N`):

```
"npayne_providers":"2",
"payne_provider_0":"grok-build",
"payne_provider_1":"goose"
```

Missing keys → default one slot `goose`. Invalid id skipped.
Duplicates dropped. Cap 4. Never store name/about overrides.

Session `payne` object grows:

```
"provider":"<primary>",
"providers":["grok-build","goose"]
```

`provider` is `providers[0]` for the card subtitle and for
older UI that reads one id.

### HTTP

`POST /api/agent` with `slug: "sgt-major-payne"` and
`provider_0`…`provider_N` (same mention_N style) updates the
ranked list and saves vibe.json. Name / `system_prompt` in that
body are **ignored**. `action:"delete"` on Payne still denied.

Do **not** invent `/api/payne`. One route, one extra branch.

### Dispatch

`hush_agent_lookup_robot` for Payne:

```
out->provider = launch->payne_providers[0]
                if npayne_providers > 0
                else HUSH_ROSTER_PROVIDER_GOOSE;
out->prompt   = HUSH_LAUNCH_PAYNE_ABOUT;   /* still locked */
```

No fallback walk this slice. `handle_mention` stays: grok-build
+ ready → spawn; else on-deck note.

### Drawer mode

`openAgentDrawer(bot)` accepts locked Payne:

- Title: `Edit Sgt Major Payne`
- Save: `Save Robot`
- Delete: disabled
- Name + prompt + files + pass row: hidden (`hidden` attr or
  `display:none` via a `payne-lock` class)
- Provider radios + pencil + new order pills: visible
- Save posts `{slug:"sgt-major-payne", provider_0, …}`

Raise path unchanged.

## Rejected alternatives

1. **Put Payne on the roster as a normal agent.** Would let
   rename/delete/prompt-edit leak through existing Raise/Save.
   Breaks E4 / E8.
2. **Single radio, no order.** Request asked for order. A
   one-id field cannot express fallbacks.
3. **Walk fallbacks and spawn API clients now.** Out of scope;
   Deepseek live client was already refused. Order is stored
   for the next spawn slice.
4. **Allow renaming / rewriting standing orders.** Explicitly
   forbidden.
5. **New module `hush_payne.c`.** Too much surface. Launch
   already owns Payne + vibe.json.
6. **Let POST /api/agent create a second Payne.** Slug
   collision. Refuse add when slug is Payne; update instead.

## Scope (frozen)

**Primary Goal**

Click **Edit** on Sgt. Major Payne. Configure which SaaS /
CLI providers he may use, and in which order. Name and
instructions stay locked. Payne stays undeletable.

**Non-Goals**

- Rename Payne or edit `HUSH_LAUNCH_PAYNE_ABOUT`.
- Delete Payne.
- Context files on Payne.
- Live spawn for goose / Codex / Cline / HTTP API families.
- Fallback walk at job time.
- Raised-robot multi-provider order (Payne only this slice).
- Hive Close/Exit size. Foreign-home writes.

**Success Criteria**

1. Payne card shows **Edit**.
2. Drawer cannot change name or prompt (fields absent or
   disabled; save ignores them).
3. Save persists `payne_provider_N` in vibe.json and
   `session.payne.providers`.
4. Delete on Payne stays disabled / denied.
5. `hush_agent_lookup_robot` uses `payne_providers[0]`.
6. `./configure && make && make test`.
7. PR merged, worktree removed, main clean.

**Constraints**

C11 + write-legible-c §14. Prime Directive worktree / PR.
Embed UI via Makefile (`make` regenerates `hush_ui_html.h`).
No feline words. Pass checkbox default-on on Raise. Never
write `~/.grok` `~/.codex` `~/.config/goose`.

**Assumptions**

Default order is `[goose]`. Cap 4. Distinct ids only.
Configure pencil still opens the existing provider drawer.

**Environment**

gcc, make, curl, gh. Tests use `HUSH_CONFIG_DIR`.

**Risks**

1. Session JSON size. Mitigation: 4 short ids.
2. Humans expect fallbacks to fire now. Mitigation: card
   subtitle shows the list; on-deck note still honest when
   primary is not grok-build.
3. `hush_launch_format_head` snprintf grows. Mitigation: add
   providers in a helper, not one mega-printf.
4. Accidental prompt edit if fields stay visible. Mitigation:
   hide name/prompt/files on Payne mode; server ignores them.
5. Function budget on `hush_http_serve_agent`. Mitigation:
   new `hush_http_update_payne` leaf, not a fatter serve.

## Updated plan for remaining phases

See `docs/plan/PLAN_PAYNE_PROVIDER_EDIT.md`. Phase 2 specs
the Edit exception. Phase 3 persist + session + HTTP. Phase 4
agent lookup. Phase 5 UI. Phase 6 tests. Phase 7 land.
