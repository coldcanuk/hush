# RESEARCH: Deepseek API provider + compact robot-edit buttons

Worktree: `/opt/repo/hush/worktrees/deepseek-robot-btns`
Branch: `gb/deepseek-robot-btns`
Base: `main` `d6d7f20be` (PR #42)

Using the four-minds protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP servers: analyze (C unsupported), apps, buzz_publish, developer,
  extensionmanager, skills, summon, todo.
- Consulted: official Deepseek docs (eight URLs below),
  `PRIME_DIRECTIVE.md`, `UI_SPEC.md` §9–§11, `hush_provider.h/.c`,
  `hush_roster.h/.c`, `demo/index.html` Raise/Edit drawer,
  `test_provider.c`, `test_roster.c`, `check_provider.sh`,
  `check_launch.sh`, `PLAN_PROVIDER_CONFIGURE.md`.
- Operator request is two product features, not a live incident.

Data: eight named runtimes today. HTTP API family is
`gemini-api` / `xai-api` / `openai-api` / `anthropic-api`. Deepseek
is an explicit prior non-goal (`UI_SPEC.md:231`,
`test_provider.c:94` `deepseek not yet`). Robot-edit footer is
two fat `.btn`s plus a full-width red Delete. **8/10**.
Sherlock: the prior plan already named the wire id `deepseek-api`.
Linus: do not invent a Deepseek client. Add one table row.
Brian Cox: tokens do not burn until a later live-job slice. This
slice only names the radio and stores the key.

## What is asked (quoted)

1. "when we edit a robot, the buttons at the bottom are big and
   unruly. bring down their size. [Save Robot], [Close],
   [Delete Robot] - they can be on one line."
2. "Robots must be able to support Deepseek API as a SaaS provider."
3. Read the official Deepseek docs (Quick Start + four guides + four
   API refs).
4. Full RDAP. Worktree. PR land. "Grok Build complete."

## Phase 1 — Evidence

### Deepseek official contract (fetched 2026-08-19)

**E1** Quick Start (`https://api-docs.deepseek.com/`):

- OpenAI-compatible and Anthropic-compatible.
- `base_url` OpenAI: `https://api.deepseek.com`
- `base_url` Anthropic: `https://api.deepseek.com/anthropic`
- Models: `deepseek-v4-flash` (V4-Flash-0731), `deepseek-v4-pro`
  (V4-Pro-0813). Call the short names; versions roll under them.
- Auth: `Authorization: Bearer ${DEEPSEEK_API_KEY}`
- First-call curl: `POST https://api.deepseek.com/chat/completions`
  (no `/v1` prefix in the official sample). Body is OpenAI chat
  (`model`, `messages[]`, optional `thinking`, `reasoning_effort`,
  `stream`).

**E2** Multi-round (`/guides/multi_round_chat`):

- `/chat/completions` is **stateless**. Client concatenates history.
- Same OpenAI `messages` append pattern. No server conversation id.

**E3** Chat prefix completion (`/guides/chat_prefix_completion`):

- Beta. Last message `role=assistant` and `prefix=true`.
- Requires `base_url=https://api.deepseek.com/beta`.
- Not needed to *name* a provider.

**E4** FIM (`/guides/fim_completion` + `/api/create-completion`):

- Beta. `POST /completions` with `prompt` + optional `suffix`.
- `base_url=https://api.deepseek.com/beta`. Max 4K tokens.
- Model list on FIM is `deepseek-v4-pro` only.
- Code-completion feature, not hive chat.

**E5** JSON mode (`/guides/json_mode`):

- `response_format: {type: json_object}` on chat completions.
- Prompt must contain the word "json" plus an example.
- Occasional empty content; prompt-side mitigation.

**E6** Chat Completions (`/api/create-chat-completion`):

- `POST /chat/completions`. Required: `messages` (≥1), `model`
  in `{deepseek-v4-flash, deepseek-v4-pro}`.
- `thinking.type` `enabled` (default) or `disabled`.
- `reasoning_effort` documented (page truncated at `low` in the
  scrape; first-call sample uses `high`).
- Assistant `prefix` and `reasoning_content` are beta.

**E7** Responses API (`/api/create-response`):

- `POST /responses`. OpenAI Responses shape. Also stateless.
- Same two model ids. Image/file inputs ignored.

**E8** List models (`/api/list-models`):

- `GET /models` → `{object:"list", data:[{id, object, owned_by}]}`.
- Example ids: `deepseek-v4-flash`, `deepseek-v4-pro`.

**E9** Live probe without a key (this host, 2026-08-19):

| URL | HTTP |
|---|---|
| `https://api.deepseek.com/models` | 401 |
| `https://api.deepseek.com/v1/models` | 401 |
| `https://api.deepseek.com/chat/completions` | 401 |
| `https://api.deepseek.com/v1/chat/completions` | 401 |

Both the documented path and the OpenAI `/v1` alias exist.
401 is "auth required", not "no such route".

**E10** Pricing page: both models, 1M context, JSON / tools /
Responses / Anthropic / prefix. FIM is non-thinking only.

### Hush provider inventory

**E11** Counts are hard `8` in two places:

- `HUSH_PROVIDER_COUNT = 8` (`hush_provider.h:19`)
- `HUSH_ROSTER_PROVIDER_COUNT = 8` (`hush_roster.c:19`)
- Comments: "eight named runtimes" (`hush_provider.h:78`,
  `hush_roster.h:90`)
- `test_provider.c:94` `!hush_provider_is_id("deepseek-api")`
- `test_provider.c:167` `n == HUSH_PROVIDER_COUNT` labeled `"eight"`

**E12** Meta table (`hush_provider.c:45-62`) is the single id/label/
family/host/binary site. HTTP API rows already carry a default host.

**E13** Scan dialect (`hush_provider_fill_curl_url:879-892`):

- Gemini: `GET {host}/v1beta/models?key=`
- Everyone else in the API family: `GET {host}/v1/models` +
  `Authorization: Bearer` (Anthropic swaps the header).

**E14** Secrets already generic: five kinds in `pass` at
`providers/<id>/{api_key,username,password,token,passkey}`. Overlay
`providers.json` holds `use_home`, `host`, `model`, `has_key` only.

**E15** Live jobs: `hush_agent` still only execs `grok-build`.
HTTP API providers are **labels + configure state**. Raising a
Gemini/OpenAI/Anthropic robot does not start a child. Deepseek
this slice is the same family: radio + drawer + persist. No
`hush_agent` Deepseek HTTP client.

**E16** UI radios (`demo/index.html:678-686` and `PROVIDERS` map
`:868-876`) list eight ids. No Deepseek label.

**E17** `UI_SPEC.md:231-232`: "Deepseek is not a radio this slice."
`PLAN_PROVIDER_CONFIGURE.md:22` same non-goal. This request
reverses that sentence only.

### Robot-edit buttons

**E18** Footer markup (`demo/index.html:693-697`):

```
<div class="actions">
  <button class="btn" id="agent-save">Raise this robot</button>
  <button class="btn ghost" id="agent-close">Close</button>
</div>
<button class="btn danger" id="agent-delete" disabled>Delete this robot</button>
```

**E19** Unruly CSS:

- `.btn` is a large pill (`padding: 10px 18px`).
- `.btn.danger { width: 100%; margin-top: 8px; }` — Delete is a
  full-width red bar under the pair. That is the "big and unruly"
  tell.
- `.actions { flex-wrap: wrap; gap: 8px; margin-top: 18px; }` —
  wrap + fat padding stacks them on a narrow drawer.

**E20** Edit mode only flips copy (`:1803-1805`): title "Edit robot",
save "Save this robot", delete enabled. Raise keeps delete disabled
but still renders the full-width bar.

**E21** `check_launch.sh:107` greps `Delete this robot`. Any label
change must update that check.

**E22** Fitts in `UI_SPEC.md` is ≥44px. Operator explicitly asked
to shrink and put three actions on one line. Compact row in this
drawer only; do not shrink hive Close/Exit or leave-chooser.

## Four minds

**Data.** Deepseek is OpenAI-chat with a documented host, Bearer
key, and two model ids. Hush already has that family. The button
bug is one CSS rule (`width: 100%` on `.btn.danger`) plus Delete
sitting outside `.actions`.

**Sherlock Holmes.** Why was Deepseek excluded? Because the last
slice froze eight radios and said "follow-up". The test even
reserves the id `deepseek-api`. The cheapest honest move is to
flip that assertion. Prefix/FIM/JSON/Responses are capability
docs for a client we do not ship this slice.

**Linus Torvalds.** One meta-table row. Bump `8` → `9` in the two
count enums. Reuse Bearer `/v1/models` scan. Do not add a third
curl dialect for `GET /models` when `/v1/models` already 401s
(exists) and type-in already works. Do not start a Deepseek job
in `hush_agent`. Buttons: put Delete in the same flex row and
stop making it `width: 100%`.

**Brian Cox.** A radio that cannot yet spend tokens is still the
right first particle. Configure + pass key is the conserved
quantity. Live `/chat/completions` is a later energy budget
(same as Gemini/OpenAI today).

Unanimous: HTTP API family + compact three-button row. No new
module. No live Deepseek child.

## Bayes (why this shape)

| Hypothesis | Prior | Likelihood | Posterior |
|---|---|---|---|
| H1 Deepseek is just another OpenAI-family radio | 0.45 | 0.90 | **0.62** |
| H2 Need a special `/models` (no `/v1`) scan | 0.20 | 0.40 | 0.12 |
| H3 Need a live chat client this slice | 0.15 | 0.20 | 0.05 |
| H4 Buttons are copy-only (no CSS) | 0.10 | 0.25 | 0.04 |
| H5 Anthropic-compat URL is required | 0.10 | 0.20 | 0.03 |

H1 wins. H2 is residual risk: if scan against
`https://api.deepseek.com/v1/models` fails with a real key,
the human types `deepseek-v4-pro`. We do not add a dialect
until a failing scan is observed.

## Locked architecture

### Deepseek provider

| Field | Value |
|---|---|
| Wire id | `deepseek-api` |
| Label | Deepseek API |
| Family | `api` (`HUSH_PROVIDER_FAMILY_API`) |
| Default host | `https://api.deepseek.com` |
| Secret | `pass` `hush/providers/deepseek-api/api_key` (+ optional username/password/token/passkey like the others) |
| Overlay | `providers.json` `host` + `model` |
| Scan | reuse OpenAI: `GET {host}/v1/models` + Bearer |
| Typed models | `deepseek-v4-pro`, `deepseek-v4-flash` (human may type either) |
| Login | none (not OAuth CLI) |
| Live job | **out of scope** (same as other HTTP APIs) |

`HUSH_PROVIDER_COUNT` and `HUSH_ROSTER_PROVIDER_COUNT` become 9.

### Robot-edit actions

Move `#agent-delete` into `#agent-drawer .actions`. One flex row,
no wrap. Compact padding. Danger no longer `width: 100%` inside
that row.

Copy (short so three fit):

| Mode | Primary | Ghost | Danger |
|---|---|---|---|
| Raise | Raise Robot | Close | Delete Robot (disabled) |
| Edit | Save Robot | Close | Delete Robot |

`confirm("Delete this robot?")` stays (sentence, not a button).

### What we will not build

- `POST /chat/completions` / `/responses` / `/completions` from C.
- Prefix completion, FIM, JSON mode, thinking-mode toggle, tool
  calls, context caching, DeepSeek Harness.
- Anthropic-compat base URL as a second host.
- A ninth *live* agent runtime.
- Shrinking hive Close/Exit or the leave chooser.
- Changing Payne's locked robot (still undeletable).

## Scope (frozen)

**Primary Goal**

1. Edit-robot footer is one compact line: Save/Raise, Close, Delete.
2. Deepseek API is a first-class SaaS radio with the same configure
   drawer as OpenAI/xAI.

**Non-Goals**

Live Deepseek inference. Beta endpoints. New secret kinds. New
modules. Foreign-home writes.

**Success Criteria**

- `hush_provider_is_id("deepseek-api")` true; default host
  `https://api.deepseek.com`; family `api`.
- `GET /api/provider` includes `"deepseek-api"`.
- Raise/Edit radio `value="deepseek-api"`.
- `#agent-save`, `#agent-close`, `#agent-delete` share one
  `#agent-drawer .actions` row.
- `./configure && make && make test`.
- PR merged, worktree removed, main clean.

**Constraints**

C11 + write-legible-c §14. Prime Directive worktree/PR.
`./scripts/embed-ui.sh hush-c/demo` after HTML. Secrets only in
`pass`. No feline. Pass checkbox stays default-on.

**Assumptions**

- `/v1/models` on `api.deepseek.com` will accept a real key the
  same way `/models` does (both 401 without one).
- Compact 36px drawer actions are acceptable against the 44px
  Fitts house rule because the operator asked to shrink this row.
- HTTP API robots remain labels until a later job slice.

**Environment**

`./configure && make && make test`. `gh` for the PR. `curl` already
used for scan.

**Top risks**

1. Scan path `/v1/models` vs documented `/models` — mitigate with
   type-in; do not add a dialect until observed.
2. Two count enums drift — bump both, one test asserts 9 and the
   new id.
3. `check_launch.sh` still greps `Delete this robot` — update with
   the short label.
4. `.btn.danger { width:100% }` used elsewhere — scope the override
   to `#agent-drawer .actions`.
5. Session JSON / overlay size — one extra object, same four keys.

## Decision table (this slice)

| Question | Decision |
|---|---|
| New module? | No. Table row + radio + CSS. |
| Host constant? | `HUSH_PROVIDER_HOST_DEEPSEEK` in `hush_provider.h`. |
| Scan dialect? | Reuse OpenAI Bearer `/v1/models`. |
| Prefill model? | No (same as other API ids). |
| Live chat? | No. |
| Button Fitts? | Compact this drawer only. |
| Land? | PR, not local merge. |
