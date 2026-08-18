# RESEARCH: OAuth provider bleed, @mention replies, tool-rail anchors

Worktree: `/opt/repo/hush/worktrees/oauth-mention-rail`
Branch: `gb/oauth-mention-rail`
Base: `e30cb1bef` (PR #32, pills-rail-voice-exit)
Protocol: `/trouble` four-minds, mode **TOOLED**
Date: 2026-08-18

## Using the four-minds debug protocol

Mode: **TOOLED**. No failed-command exit-code triage.

### What is broken / tried / in play

- **Broken (quoted from the operator):** `@Happy` becomes a pill, `Hello. Tell me a joke` posts, “I never receive a message back.” “I expect a thread to start with Happy and for Happy to reply.” “Happy is configured using Grok Build.”
- **Broken:** Edit Happy → “Codex also showed it was authenticated.” Operator “can confirm the only one I auth’d was Grok Build.” Suspects “Oauth check was broad and all oauth providers turned blue.”
- **Broken:** “The tool rail UX works. Good. However, I cant’ anchor it anywhere and I can’t move it anywhere.”
- **Tried:** operator belief tagged `assumption` until evidence below.
- **Files:** `hush-c/demo/index.html`, `hush-c/src/hush_provider.c`, `hush-c/src/hush_http.c`, `hush-c/src/hush_relay.c`, `UI_SPEC.md` §12–§15.

### Phase 0 — Context Register

- MCP servers in session: analyze, apps, buzz_publish, developer, extensionmanager, skills, summon, todo.
- Docs / observability / bindings MCPs: **none present**.
- Consulted: none. Local code, `HOME` filesystem, and `grok --help` are the sources.
- **Data:** no library doc contradicts the local contracts. **[no M# items]**
- **Sherlock:** the docs MCP is silent on live robot replies. That silence matches the repo’s own non-goals.
- **Linus:** “We don’t read Wikipedia before fixing a null pointer.” No extra MCP calls.
- **Brian Cox:** PR #32 (rail, pills) landed immediately before this report. Live LLM reply was a non-goal in both prior slices. Causal mass sits on *absence of a runner* and *loose Codex home detect*, not on a shared OAuth boolean.

### Phase 1 — Evidence

**E1** `hush-c/demo/index.html:1454`

```
const ready = !!(OAUTH_PROVIDERS[pid] && st.has_home);
```

Ready is per radio `pid`, not global.

**E2** `hush-c/demo/index.html:730-733`

```
const OAUTH_PROVIDERS = {
  "grok-build": "Log in with Grok OAuth",
  "codex": "Log in with Codex OAuth"
};
```

**E3** `hush-c/src/hush_provider.c:568-589`

```
if (strcmp(id, HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0)
    snprintf(out, outsz, "%s/.grok/auth.json", home);
else if (strcmp(id, HUSH_ROSTER_PROVIDER_CODEX) == 0)
    snprintf(out, outsz, "%s/.codex", home);
…
st->has_home = hush_provider_path_exists(path);
```

Codex `has_home` is `stat(~/.codex)` on the **directory**.

**E4** `hush-c/src/hush_provider.c:753-754`

```
if (st->has_home && strcmp(st->family, HUSH_PROVIDER_FAMILY_HOME) == 0)
    st->configured = 1;
```

A leftover Codex directory also marks Codex `configured`.

**E5** Command output (this session, operator `HOME`):

```
grok auth.json EXISTS
codex dir EXISTS
codex auth.json MISSING
```

`~/.codex` listing: sqlite dbs, `installation_id`, `version.json`, `packages/`, `skills/`. No `auth.json`, no `config.toml`. Directory dates from 2026-07-28.

**E6** `RESEARCH.md:1824-1826`

```
- `~/.codex` directory exists → `has_home`.
- `~/.codex/auth.json` or `config.toml` present → `configured`.
```

Implemented `has_home` followed the first bullet; UI treats `has_home` as authenticated (UI_SPEC §12).

**E7** `UI_SPEC.md:259-260`

```
Mentioning a robot addresses it; this slice
does not spawn a live reply.
```

**E8** `RESEARCH_OAUTH_MENTION_GROUPS.md:17` non-goal: “Spawning Goose / Grok / Codex as a live reply process.”

**E9** `RESEARCH_PILLS_RAIL_VOICE_EXIT.md:13` non-goal: “Live LLM replies from a mentioned robot (still a follow-up).”

**E10** `hush-c/src/hush_http.c:522-535` — `POST /api/event` writes kind 1 + `h` + `p` tags. No spawn.

**E11** `hush-c/src/hush_relay.c:559-581` — pump is poll + HTTP + TURN refresh. No agent runner.

**E12** `hush-c/demo/index.html:363-376` — `#tool-rail` is `position:fixed; top:10px; right:12px`. `#rail-grip` is `height: 18px`.

**E13** `hush-c/demo/index.html:2499-2517` — drag listeners live only on `#rail-grip`. No snap/anchor. `pointerup` clears `railDrag` without clamping to a dock.

**E14** `grok --help` (this session): `-p, --single <PROMPT>` “Prints the response to stdout and exits”; `--output-format`; `--system-prompt-override`; `--always-approve`.

**E15** `hush-c/src/hush_launch.c:1855-1868` — raised-robot nsec is restored from `pass` `agents/<slug>/nsec` (or generated). In-memory identity exists for a reply author.

#### Smuggled assumptions

| ID | Claim | Tag | Falsifier |
| --- | --- | --- | --- |
| A1 | One Grok OAuth flipped every OAuth radio | `assumption` | E1 keys `.ready` by `pid`. Falsified as a UI-global flag. |
| A2 | Happy should already reply today | `assumption` | E7–E11: live reply was a documented non-goal. Now an authorized follow-up. |
| A3 | Happy’s stored provider is `grok-build` | `[UNVERIFIED]` | Session `agents[].provider`. Dispatch will key on that field. |
| A4 | Rail drag “should work” because code exists | `assumption` | E12–E13: 18px grip, no anchors, element-local pointer path. |

**Linus** strikes “Codex turned blue because Grok OAuth is shared” as `[not evidence]` — that restates the report. The clue is E3+E5.

**Sherlock** on A1: “What would falsify a shared OAuth bit?” A `ready` toggle that does not mention `pid`. It is not in the file.

**Brian Cox** timeline:

1. OAuth-mention slice ships pills + `p` tags; live reply deferred (E7–E8).
2. Pills/rail slice ships 18px movable rail; live reply still deferred (E9, E12).
3. Operator authenticates Grok Build (`~/.grok/auth.json` present).
4. `~/.codex` already existed (2026-07-28) without `auth.json`.
5. Edit Happy paints both OAuth radios `.ready` (E1+E3+E5).
6. `@Happy` stores a kind 1 and stops (E10–E11).
7. Rail cannot be pinned; grip is 18px (E12–E13).

Grok login cannot *cause* the Codex directory. Effect (Codex badge) predates the supposed cause.

### Phase 2 — Hypotheses

**H1** Codex `has_home` is directory existence, so a leftover `~/.codex` looks authenticated. Independent of Grok OAuth.
Explains E3–E6. Does not explain silence or the rail.
Falsifier: isolated `HOME` with an empty `~/.codex` dir → `has_home` true today.
Data **9/10**. Linus **9/10**. Sherlock **8/10**. Brian **9/10**.

**H2** UI OAuth ready class is not keyed by provider.
Explains the operator’s suspicion. Contradicted by E1.
Data **1/10**. Linus: “Dead. Read the ternary.”

**H3** Mention never dispatches a provider. Store-only.
Explains Happy’s silence with Grok configured. Does not explain badges or the rail.
Falsifier: a `fork`/`exec` of `grok` after `/api/event` — absent (E10–E11).
Data **9/10**. Sherlock **9/10**. Linus **9/10**. Brian **9/10**.

**H4** Grok OAuth failed, so a runner would have nothing to call.
Does not explain silence: there is no runner (E11). Time-reversal.
Data **2/10**.

**H5** Rail cannot be moved/anchored because the grip is 18px, pointer handlers are element-local, and no dock points exist.
Explains the rail report (E12–E13).
Data **7/10**. Linus **8/10**. Brian **7/10**.

### Phase 3 — Bayesian evaluation

OAuth trio H1 / H2 / H9 (Codex really signed in via a hidden auth file).

- P(H1)=0.60, P(E\|H1)=0.95 → 0.570
- P(H2)=0.15, P(E\|H2)=0.10 → 0.015
- P(H9)=0.20, P(E\|H9)=0.30 → 0.060 (dir listing showed no `auth.json`)

Sum = 0.645.

- P(H1\|E) = **0.884**
- P(H2\|E) = **0.023**
- P(H9\|E) = **0.093**

Mention trio H3 / H4 / H10 (mention never posted).

- P(H3)=0.70, P(E\|H3)=0.99 → 0.693
- P(H4)=0.15, P(E\|H4)=0.20 → 0.030
- P(H10)=0.15, P(E\|H10)=0.40 → 0.060

Sum = 0.783.

- P(H3\|E) = **0.885**
- P(H4\|E) = **0.038**
- P(H10\|E) = **0.077**

Rail trio H5 / H6 (covered by header) / H11 (CSS `right` wins over unsaved `left`).

- P(H5)=0.65, P(E\|H5)=0.90 → 0.585
- P(H6)=0.15, P(E\|H6)=0.25 → 0.038
- P(H11)=0.20, P(E\|H11)=0.50 → 0.100

Sum = 0.723.

- P(H5\|E) = **0.809**
- P(H6\|E) = **0.053**
- P(H11\|E) = **0.138**

### Phase 4 — Second debate

**Data:** all three leading posteriors are above 0.50 (0.884 / 0.885 / 0.809). **8/10** the numbers justify three scoped fixes, not one mega-refactor.

**Sherlock:** “What single extra fact would move these most?” A live `GET /api/provider` on the operator hive. We already have the home directory listing (E5). Waiting is **3/10**. Proceed.

**Linus:** H1 is a detect-path change. H5 is grip + snap. H3 is a new module — the user asked for the runner this time. Do not invent a plugin host. `fork` `grok -p`, write a kind 1 with `e`+`p`. **8/10**.

**Brian Cox:** three independent arrows. No shared state required. **8/10**.

Cross-examination: Sherlock challenged Data’s earlier paraphrase of A1 as a UI bug. Data withdrew H2. Linus: “No objection on H1/H3/H5.” Brian: “No objection on this point.”

### Phase 5 — Problem and scope

**Problem.** Codex paints “authenticated” because `has_home` is `stat(~/.codex)`, and this machine has a leftover Codex tree without a login file. The UI check is already per provider. `@Happy` does not reply because no code path invokes Grok Build after `POST /api/event`. The tool rail cannot be pinned and is hard to drag: 18px grip, no docks, element-local pointer listeners.

**Scope of work.** Tighten OAuth `has_home` to provider-specific auth artifacts. After a kind 1 with `p` tags, start a live Grok Build (`grok -p`) for robots whose provider is `grok-build` and whose home is authenticated; other mentioned robots post a short on-deck note so a thread still appears. Persist `e` as `reply_to` in `/api/events` and indent replies. Make the rail Fitts-sized, document-drag, and snap to six docks.

**Out of scope.** Streaming tokens. Multi-turn Grok sessions. Driving Codex/Goose TUIs. NIP-10 thread pane beyond indent. Tray. Remote auth. Shared OAuth boolean (it does not exist).

### Phase 6 — Plan pointer

See `PLAN_OAUTH_MENTION_RAIL.md`.

### Unanimous agreement gate (recorded)

| Voice | Execute scoped plan? | Confidence | Why |
| --- | --- | --- | --- |
| Data | yes | **8/10** | E3+E5+E11 quoted; runner still unverified until tests |
| Sherlock | yes | **8/10** | H2 is dead; remaining work maps 1:1 to H1/H3/H5 |
| Linus | yes | **8/10** | Smallest diffs; new module is the feature they asked for |
| Brian Cox | yes | **8/10** | Timeline matches; no time-reversal in the fix |

Hard cap: no fix is verified yet, so no voice may exceed **8/10** until Phase 7 output exists.

## Architecture lock

### OAuth `has_home`

| id | authenticated iff |
| --- | --- |
| `grok-build` | `~/.grok/auth.json` exists, is a regular file, size > 0 |
| `codex` | `~/.codex/auth.json` **or** `~/.codex/config.toml` exists, regular, size > 0 |
| `goose` | unchanged (`config.yaml` path exists, plus `~/.goose` fallback) |
| `cline` | unchanged |

UI `.ready` stays `OAUTH_PROVIDERS[pid] && st.has_home`. No global flag.

### Mention → reply

`hush_agent` module. `hush_agent_consider(store, launch, ev)` after a successful `POST /api/event`.

- Ignore non-kind-1 and self-`p` matches.
- Resolve each `p` tag against Payne + raised robots (npub or 64-hex).
- `grok-build` + `has_home` + `grok` on PATH: fork `grok -p <note> --system-prompt-override <standing orders> --output-format plain --always-approve --no-plan`. Capture stdout. Insert kind 1 authored as the robot, tags `h` (channel), `e` (parent id), `p` (human pubkey).
- Any other mentioned robot: insert an immediate on-deck note from that robot (same tags) so a thread exists.
- Jobs are non-blocking. `hush_relay_pump` calls `hush_agent_poll`. Cap 4 jobs. 90s timeout. Track pid via `hush_relay_track_child`.
- `/api/events` grows `reply_to` from the first `e` tag. UI indents `.note.reply`.

### Tool rail

- `#rail-grip` ≥ 44px. `touch-action: none`.
- `pointermove` / `pointerup` / `pointercancel` on `window`.
- While dragging, six docks (tl, tr, bl, br, ml, mr). Release within 48px snaps; else free position, clamped on-screen.
- `localStorage.hush-rail` = `{x,y,collapsed,anchor}`. Resize reapplies `anchor`.

## Risks

1. `grok -p` hangs → timeout + on-deck note.
2. stderr mixed into the chat → child stderr to `/dev/null`.
3. Blocking the poll loop → never `waitpid` in the HTTP handler.
4. Codex false-negative if a future CLI stores tokens elsewhere → documented; operator can drop `auth.json`.
