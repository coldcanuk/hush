# RESEARCH: thinking, thread pane, Grok reply hygiene, relay-live

Worktree: `/opt/repo/hush/worktrees/thread-think-hygiene`
Branch: `gb/thread-think-hygiene`
Base: `main` `7095430f1` (PR #33)

Using the four-minds debug protocol. Mode: **TOOLED**.

Phase 0 MCP: no documentation or observability servers in this session
(apps, buzz_publish, analyze, skills, todo only). Context Register: none
consulted. No M# items.

## What is broken (quoted)

1. Thinking: “I need somekind of feedback that tells me the robot is
   thinking or processing… Small enough to not be obtrusive but big
   enough to catch me eye.”
2. Thread: “The reply needs to be in a thread… I, the user, would see a
   thread button appear. I'd click this thread button and the thread
   I'm chatting with Happy would appear in it's own dialogue… only
   Happy and myself are in the thread. I can exit the thread and return
   back to the chat and I can return to the thread by click the thread
   button.”
3. Dump: “WTF kind of reply was that? it seems that the thought process
   was sent back. I don't know what the GEMINI STATUS means? is that a
   reference to google gemini? why did it reference uptime and python
   version? Why did it reference our nostr address and not our name?
   WHy did it not just post the joke?”
4. Relay live: “In the bottom left hand corner of the main screen
   `relay live` it reports stored, project and socks. Yet when I click
   on here nothing happens… I want a window giving me the details on
   stored, projects and sockets. [x] button in teh right to close.”

Operator paste of the live reply (verbatim fragment):

```
I'll look up that Nostr identity and any Nostr tools, then hit you with a joke.
No Nostr MCP here — I'll look up that npub so the joke can land on the right person.
**GEMINI STATUS** (kiff, Pop!_OS 24.04)
- Disk: 197G / 937G (23%)
- Uptime: 5d 4h
- Python 3.12.3 · nvm not loaded
Hey npub1tw3zm7…kc3as — joke incoming
**Why don’t scientists trust atoms?**
```

## Phase 1 — Evidence

**E1** `hush-c/src/hush_agent.c:418-427` execs:

```
argv[0] = grok; argv[1] = -p; argv[2] = job->note;
argv[3] = --system-prompt-override; argv[4] = job->prompt;
argv[5] = --output-format; argv[6] = plain;
argv[7] = --always-approve; argv[8] = --no-plan;
```

No `--cwd`, no `--tools` / `--disallowed-tools`, no `--max-turns`, no
`--reasoning-effort`, no `--no-subagents`, no `--disable-web-search`,
no `--verbatim`, no `--rules`. Child inherits the relay cwd.

**E2** `hush_agent_finish_job` (`hush_agent.c:499-509`) inserts
`job->out` verbatim as the kind-1 body. Trim is trailing whitespace
only (`hush_agent_trim`). No thought/status strip.

**E3** `hush_agent.h` exports only `init` / `shutdown` / `consider` /
`poll`. There is no job-status query. `/api/status`
(`hush_http.c:453-456`) emits `ok, version, events, clients, port,
whisper, turn_running, vibe_public`. No thinking array.

**E4** `demo/index.html:2380-2391` render: `n.className = e.reply_to ?
"note reply" : "note"`. No thread button. No thinking row. Composer
submit (`2023-2040`) posts `{content, kind, channel, mention_N}` only.
No `reply_to` field.

**E5** `hush_http_serve_post` (`hush_http.c:529-542`) never reads a
`reply_to` JSON field. `hush_http_add_mentions` only writes `p` tags.

**E6** `UI_SPEC.md:266-270` (prior slice): “Mentioning a robot starts
a thread… The stream indents `.note.reply` when `reply_to` is set.”
`RESEARCH_OAUTH_MENTION_RAIL.md:219` non-goal: “NIP-10 thread pane
beyond indent.”

**E7** `#stats` (`index.html:457`) is a `<div class="stats">`. CSS
`.stats` (`136`) is muted text. No `addEventListener` on `stats`.
`render` (`2363-2367`) writes `relay live` + stored + projects +
sockets as innerHTML. Click does nothing because nothing is bound.

**E8** `who()` (`index.html:2396-2403`) returns `"you"` for the signed
in pubkey, else a roster name, else `pk.slice(0, 8)`. It never uses
`session.profile.first_name`. Grok is handed `parent->content` which
contains `nostr:<npub>`, not the human name
(`hush_agent_fill_job:400`).

**E9** `~/.grok/README.md:1554-1568` (this session): “Grok reads these
files and appends their contents to the system prompt.” Scan order:
`~/.grok/`, then if inside a git repo every directory from repo root
→ cwd, else only cwd. Filenames include `AGENTS.md`.

**E10** `/home/chuck/AGENTS.md` (quoted via
`~/.grok/sessions/.../prompt_context.json` this session) begins:

```
# GEMINI - Personal Desktop Assistant for Chuck's GMKtec NUC (Pop!_OS)
Role: You are my always-on, truth-maximizing personal sysadmin
OS: Pop!_OS (Ubuntu-based)
```

That is the operator desktop prompt, not Hush’s `gemini-api` provider
(`hush_roster.h:36` `HUSH_ROSTER_PROVIDER_GEMINI "gemini-api"`).

**E11** `~/.grok/README.md:585-619` headless flags: `--tools` allowlist,
`--disallowed-tools` denylist (`run_terminal_cmd`, `web_search`,
`web_fetch`, `read_file`, …), `--max-turns`, `--reasoning-effort`
(`none`…`max`). `--cwd` documented at line 702. JSON output is
`{"text":"...","stopReason":"EndTurn",...}` (line 733-739).

**E12** `hush_agent_consider` ignores non-kind-1 and human self-`p`
(`hush_agent.c:136-137`, `584-585`). Robot replies tag `p` = human, so
they do not re-dispatch. Channel follow-ups without a `p` tag never
dispatch. There is no “same thread, same robot” path.

**E13** Jobs live in `g_jobs[4]`, timeout 90s, pipe stdout, stderr
`/dev/null`. The UI cannot see `busy`.

### Smuggled assumptions

| Id | Claim | Status |
|---|---|---|
| A1 | GEMINI STATUS means Happy is on Google Gemini. | FALSIFIED by E10. |
| A2 | `--system-prompt-override` alone blocks AGENTS.md. | UNVERIFIED. E9 says AGENTS.md is appended to the system prompt. Isolation via `--cwd` is the documented lever. |
| A3 | Indent `.note.reply` is a thread pane. | FALSIFIED by E4+E6. Prior slice said so. |
| A4 | `#stats` is a dead control by design. | UNVERIFIED intent; E7 shows no handler. Operator now wants a window. |

**Sherlock** on A1: the cheapest falsifier was `cat /home/chuck/AGENTS.md`
title. It matches the paste. Done.

**Linus** strikes “the reply was bad because Grok is dumb”
`[not evidence]`. The machine was given a coding-agent harness, the
operator home AGENTS.md, and `--always-approve`.

**Brian Cox** timeline: PR #33 landed `hush_agent` → hive restarted onto
that binary → operator `@Happy tell me a joke` → child `grok -p` in
the relay cwd → AGENTS.md + tools → stdout posted as the note. Effect
cannot precede the fork.

### Cross-examination

**Data:** E10 plus E1 explain every token in the operator paste
(GEMINI STATUS, kiff/Pop, npub, tool preamble).

**Sherlock:** Data, you claim cwd AGENTS.md, but why does E9 not prove
the relay cwd *is* `/home/chuck`? We did not `pwdx` the live
hush-relay. The file was loaded *somewhere* on the scan path.

**Linus:** Sherlock, stop. Whether cwd is home or `/opt/repo/hush`,
`--cwd` to an empty temp dir kills the scan. That is the cheap
falsifier and the fix. **8/10** on the lever, **6/10** on the exact
cwd (hard cap: we did not pwdx).

**Brian Cox:** No objection on causality. The GEMINI banner cannot
come from `hush_roster` gemini-api: that provider is never exec’d
(`hush_agent_handle_mention` only forks grok-build). **8/10**.

## Phase 2 — Hypotheses

**H1 — Cwd AGENTS.md + open toolbag.** `grok -p` inherits the relay
cwd, loads `/home/chuck/AGENTS.md` (GEMINI desktop), runs tools
because `--always-approve` and no denylist, and the model writes
status + thought + joke into stdout. Explains E1, E2, E9, E10, the
paste. Does not explain missing thread pane or dead `#stats` (those
are missing features, not this mechanism). Falsifier: fork with
`--cwd` empty + `--disallowed-tools` all + `--max-turns 1` and the
joke-only body appears.

**H2 — Happy is actually gemini-api.** Explains the word GEMINI.
Does not explain Pop!_OS / kiff / “Nostr MCP” / npub lookup (E10
does). Falsifier: session agent provider field. Prior raise + E10
already kill it.

**H3 — `--output-format plain` dumps hidden thoughts even with a
clean prompt.** Possible contributor (streaming-json has a separate
`thought` type, E11). Does not explain GEMINI STATUS text, which is
not a hidden thought channel — it is file content. Falsifier: same
prompt in empty cwd; if thoughts remain, switch to JSON `.text` and
`--reasoning-effort none`.

**H4 — Thread pane never existed.** E4+E6. Not a regression. The
indent is working as specified. Operator now wants the pane.

**H5 — `#stats` has no click contract.** E7. Missing feature.

## Phase 3 — Bayes (dump trio H1 / H2 / H3)

Priors from this stack: coding CLIs ingest AGENTS.md by design (E9);
wrong-provider is rare after a just-landed grok-build path; plain
thought leak is common but usually unmarked.

- P(H1) = 0.60. P(E|H1) = 0.95. Unnorm = 0.570
- P(H2) = 0.10. P(E|H2) = 0.15. Unnorm = 0.015
- P(H3) = 0.30. P(E|H3) = 0.40. Unnorm = 0.120
- Sum = 0.705

```
P(H1|E) = 0.570 / 0.705 = 0.809
P(H2|E) = 0.015 / 0.705 = 0.021
P(H3|E) = 0.120 / 0.705 = 0.170
```

H4 and H5 are not competing dump hypotheses. They are independent
missing-feature claims with posterior ~1 given E4–E7.

## Phase 4 — Second debate

**Data:** H1 wins by 0.809 − 0.170 = 0.639. Numbers justify a scoped
hygiene fix. **8/10**.

**Sherlock:** Value of information is `pwdx` on the live relay and
one dry `grok -p` in `/tmp`. Worth 30 seconds, not a day. Do not wait.
**8/10**.

**Linus:** H1’s diff is argv + `--cwd` + a status export. H3’s JSON
parse is extra. Do both cheap flags in one function. Thread pane is
the large UI piece — keep it one drawer, not a NIP-10 server.
**8/10**.

**Brian Cox:** Arrow of time matches H1. After #33 the first live
grok child ran in a human home that already had AGENTS.md. Momentum:
every future mention will dump the same way until cwd is isolated.
**8/10**.

Agreement: H1 + H4 + H5. H3 as belt (JSON `.text` if we keep json;
else `plain` after tool-strip is enough).

## Phase 5 — Scope

**Problem.** The mention runner posts Grok’s whole coding-agent turn,
including the operator desktop AGENTS.md. The UI never shows that a
job is live. “Thread” is an indent, not a dialogue. `#stats` is not
a control.

**Work.** Isolate `grok` (`--cwd` empty, deny tools, max-turns 1,
reasoning none, rules: one short note, address the human by profile
first name). Export in-flight jobs on `/api/status`. Thinking chip on
the parent note. Thread button → `#thread-pane` (human + that robot).
`POST /api/event` accepts `reply_to`. Channel stream shows roots;
replies live in the pane. `#stats` opens `#relay-drawer` with stored /
projects / sockets and an [x].

**Out of scope.** Streaming tokens. Codex/Goose live CLIs. Nested
NIP-10 trees. Tray. Changing `/home/chuck/AGENTS.md`. Google Gemini
API. Shared OAuth work (already landed).

## Architecture lock

### Grok hygiene

`hush_agent_exec_grok` grows to:

```
grok -p <note>
  --system-prompt-override <robot prompt + hygiene>
  --output-format plain
  --always-approve
  --no-plan
  --no-subagents
  --disable-web-search
  --max-turns 1
  --reasoning-effort none
  --cwd <empty dir>
  --disallowed-tools run_terminal_cmd,web_search,web_fetch,read_file,search_replace,list_dir,grep,todo_write,task,Agent
  --rules <one-line: reply as <name> to <human>; one short note; no status; no tools; no npub>
```

Empty dir: `HUSH_CONFIG_DIR/agent-cwd` (tests already set
`HUSH_CONFIG_DIR`) else `$TMPDIR/hush-agent-cwd`. Created once, no
`AGENTS.md`. Hush still does not write `~/.grok`.

Hygiene appended to the override so a robot prompt cannot re-enable
the desktop persona. Address the human as `profile.first_name` when
set, else “you”. Never echo `nostr:npub`.

`HUSH_AGENT_ARGV_MAX` rises from 12 to 28.

Keep `plain`. Fake grok in `check_agent.sh` still echoes one line.
If a future live dump still includes banners, parse JSON `.text` in a
follow-up — do not do it now (Linus).

### Thinking

New: `void hush_agent_status(char *out, size_t outsz);` writes a JSON
array `[{"name":"Happy","parent":"<id>"}]` of busy jobs. `/api/status`
grows `"thinking":<array>`.

UI: on the root note whose `id` matches `thinking[].parent`, a
`.think` chip — 8px pulsing accent dot + “Happy is thinking”.
`aria-live="polite"`. Gone when the job leaves the array.

### Thread pane

- A **root** is a kind-1 with empty `reply_to` that mentioned a robot,
  or any kind-1 that has descendants.
- All thread notes tag `e` = **root id** (not the latest child).
- `POST /api/event` reads optional `reply_to` and stores that `e` tag
  before mentions.
- `hush_agent_consider` / `fill_job`: if the triggering event already
  has an `e` tag, the reply’s `e` is that root; else the trigger id.
- Channel `#stream` lists roots only. A root with replies or a live
  job shows `.thread-btn` (“Thread · N”).
- `#thread-pane` drawer: root + descendants authored by the human or
  the mentioned robot. Composer inside the pane posts `reply_to=root`
  and `mention_0=robot`. Close [x] returns to the channel. Same button
  reopens.
- No new store. Filter `GET /api/events`.

### Relay live

`#stats` is `role="button"` `tabindex="0"`. Click / Enter opens
`#relay-drawer`: version, port, stored count, project names, socket
count, whisper, turn. Header row has `#relay-close` [x] on the right.
Reuse `.drawer` / `.panel`.

## Risks

1. `--system-prompt-override` still appends AGENTS.md → `--cwd` empty
   is the real wall (E9).
2. `--disallowed-tools` name drift → keep the README table; tests use
   a fake grok that ignores flags.
3. Thread filter misses a reply if `e` is the child not the root →
   lock e=root in both HTTP and agent.
4. Thinking chip flicker if `tick` races the job close → chip keys on
   `thinking[]` only, not a local timer.
5. `HUSH_HTTP` status body 384 bytes may overflow with names → grow
   the status buffer; named constant.

## Unanimous gate (plan only)

| Voice | Agree | Score | Why |
|---|---|---|---|
| Data | yes | 8/10 | H1 0.809 + E4/E7 features. Cap 8: live grok not re-run this session. |
| Sherlock | yes | 8/10 | A1 dead. Remaining unknown is exact cwd; `--cwd` covers it. |
| Linus | yes | 8/10 | Argv + status field + one drawer + one drawer. No plugin host. |
| Brian Cox | yes | 8/10 | Timeline holds. Isolate the child before the next mention. |

## Success criteria

1. Fake-grok `check_agent.sh` still gets `reply_to` + joke line.
2. New check: live argv contract grepped in `hush_agent.c`
   (`--cwd`, `--max-turns`, `--disallowed-tools`, `--reasoning-effort`).
3. HTML greps: `thread-pane`, `thread-btn`, `think`, `relay-drawer`,
   `relay-close`.
4. Status JSON includes `thinking`.
5. POST with `reply_to` stores `e`; channel listing hides that child;
   pane would show it.
6. `./configure && make && make test`.
7. PR merge + worktree removed.

## Non-goals

Streaming. Codex/Goose runners. Nested reply trees. Editing the
operator AGENTS.md. Gemini API. Rail/OAuth (already on main).
