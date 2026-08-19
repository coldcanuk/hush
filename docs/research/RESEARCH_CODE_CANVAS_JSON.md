# RESEARCH: Happy stuck on Thinking after a code reply

Worktree: `/opt/repo/hush/worktrees/code-canvas-json`
Branch: `gb/code-canvas-json`
Base: `main` `711c1f788` (PR #45)

Using the four-minds debug protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP / extensions in this session: analyze, apps, buzz_publish, developer,
  extensionmanager, skills, summon, todo. No documentation or observability
  MCP for Grok, JSON RFC 8259, or the live hive.
- Consulted: `PRIME_DIRECTIVE.md`, `UI_SPEC.md` §13, `hush_agent.c`,
  `hush_http.c` `hush_json_escape`, `demo/index.html` `tick` / `localThink`,
  live `hush-relay --open` on `127.0.0.1:10555`.
- GitHub API: `coldcanuk/hush` `isFork:true`, parent `block/buzz`.
- No M# items. Skip further MCP: the failure is a local JSON encode plus a
  UI tick that swallows `JSON.parse`.

**Data:** docs MCP is absent; the live `/api/events` body is the primary
instrument. **8/10**.
**Sherlock:** "What is the docs MCP conspicuously silent on?" RFC 8259
§7 control-character escape. We have the wire instead.
**Linus:** We do not read Wikipedia before fixing a tab that leaked into
JSON. **9/10**.
**Brian Cox:** Causal order is already on disk: jokes land, code note
lands, then the next `tick()` cannot parse events. **9/10**.

## What is broken (quoted)

Operator paste of the 1:1 thread:

```
@Happy tell me a joke
Why did the scarecrow win an award? Because he was outstanding in his field!
@Happy tell me another
Why don’t scientists trust atoms? Because they make up everything!
@Happy and another
Why did the math book look so sad? Because it had too many problems!
@Happy write me a Day of the week announcer in Python and in Go.
Announce the new day at 1am everyday
```

Then: Happy stuck on `Thinking ...`. "Why was Happy unable to produce
the code?"

Product extras (same turn, not the hang):

- Code snippets must always be included in code blocks.
- Large pieces of code are a file attachment (download or canvas).
- Canvas is a right-hand text IDE; save to worktree / download;
  multi-file selector.
- Stop maintaining the Buzz fork relationship in git. Acknowledge origin
  in README near the top.

## Phase 1 — Evidence

**E1** Live `GET /api/status` this session:

```
{"ok":true,"version":"0.0.1",...,"thinking":[]}
```

`ps --ppid 392339` showed only the Brave PWA child. No `grok` process.
Happy was not still thinking on the server.

**E2** Live `GET /api/events` body (5308 bytes) failed strict JSON:

```
json.decoder.JSONDecodeError: Invalid control character at: line 1 column 3873 (char 3872)
```

Control-byte census of that body: `{9: 17, 10: 1}`. The lone `0x0a` is
the trailing response newline. The 17 `0x09` bytes sit inside the Go
fenced block of Happy's weekday-announcer note.

**E3** `hush_json_escape` (`hush-c/src/hush_http.c:417-436`) only
special-cases `"`, `\`, and `\n`. Every other byte, including TAB
(`0x09`), CR, and other C0 controls, is copied raw:

```
} else if (*in == '\n') {
    out[o++] = '\\';
    out[o++] = 'n';
    in++;
} else {
    out[o++] = *in++;
}
```

RFC 8259 §7: unescaped U+0000–U+001F inside a string is illegal.

**E4** `tick()` (`demo/index.html:3063-3079`):

```
fetch(API + "/api/events").then((r) => r.json()),
...
} catch (err) {
  badge(false, "relay unreachable");
}
```

`Response.json()` rejects the illegal tab. The catch never clears
`localThink`. The human sees a dead "Happy is thinking" chip and a
"relay unreachable" badge even though `/api/status` is fine.

**E5** Optimistic chip (`index.html:2394-2397`, `2747-2753`, `3026-3030`)
is set on send and cleared only when `status.thinking` lists that parent
or a later `tick` sees a non-mine child. A failed `tick` never reaches
`render()`, so the chip stays forever.

**E6** Stored weekday note `000000006a8608cf…` (created_at 15:49:35,
9s after the ask at 15:49:26) is 1425 bytes, 17 tabs, 4 fences. It
contains both a Python program and a Go program plus a joke. Happy
*did* produce the code. BASIC later (`000000006a8609c3`, no tabs)
parsed and painted.

**E7** `HUSH_EVENT_MAX_CONTENT = 4096` (`hush_event.h:14`). Job stdout
is the same bound (`hush_agent.c:76`). Two short programs fit. A
larger pair will truncate mid-file with no attachment path.

**E8** `paintNote` (`index.html:2924-2926`) sets
`body.textContent = prettyMentions(...)`. Fences are not rendered as
`<pre><code>`. There is no canvas, no download, no file selector.

**E9** `gh api repos/coldcanuk/hush` →
`{"fork":true,"parent":"block/buzz"}`. README mentions "predecessor
project" and points at `IMPORT.md`. GitHub still shows Sync / parent.

**E10** `HUSH_AGENT_TIMEOUT_S = 90`, `--max-turns 2`. Irrelevant to
this hang: E1+E6 show the child finished in 9s with a body.

### Smuggled assumptions

| Id | Claim | Status |
|---|---|---|
| A1 | Happy never produced the code. | FALSIFIED by E6. |
| A2 | Happy is still thinking / Grok hung. | FALSIFIED by E1. |
| A3 | `--max-turns 2` is why code failed. | FALSIFIED by E6 (both languages present). |
| A4 | The UI string "Thinking ..." is a Grok thought dump. | FALSIFIED by E5 (local chip). |
| A5 | Detaching the GitHub fork is a `git` operation in this clone. | Partial: origin is already only `coldcanuk/hush`. The Sync button is GitHub parent metadata (`E9`). |

**Sherlock** on A1: cheapest falsifier was `curl /api/events` with
`strict=False`. Done.
**Linus** strikes "Grok is too dumb to write two files" `[not evidence]`.
**Brian Cox** timeline: 15:48 jokes (no tabs) → 15:49:26 ask → 15:49:35
tabbed Go note → next `tick` throws → chip frozen. 15:53 BASIC (no tab)
could have unstuck a *new* root; the old pane kept `localThink` on the
first root.

### Cross-examination

**Data:** E3 plus E4 plus E6 explain the hang without remainder. The
server finished; the wire is illegal JSON; the UI cannot drop the chip.
**Sherlock:** Data, you claim the tab is the only poison. Why ignore CR
and other C0? E3 copies those too. One tab is enough for *this*
incident; the fix must cover the class.
**Linus:** Sherlock, stop. Escape every C0. Do not build a markdown
engine to unstick a chip.
**Brian Cox:** No objection on causality. The BASIC note cannot have
caused the earlier hang; it arrived four minutes later. **9/10**.

## Phase 2 — Hypotheses

**H1 — Illegal JSON TAB in `/api/events` freezes `tick`.** Explains E2,
E3, E4, E5, E6. Does not explain missing canvas (that is a missing
feature). Falsifier: POST a tabbed note, `python3 -c json.loads` fails
today and must pass after the escape fix.

**H2 — Grok hung / 90s timeout / max-turns 2.** Explains a *possible*
future hang. Does not explain E1 (`thinking:[]`) or E6 (code present).
Falsifier already ran: no grok child, note stored.

**H3 — `hush_intel` held the ask and never released.** Explains a
missing reply. Does not explain E6. Channel policy is `mention` /
`burst_ms=2000`; single note, one `@`.

**H4 — 4096-byte content clip made the UI think the job was empty.**
Does not explain E6 (1425 bytes). Still a real ceiling for larger
asks.

**H5 — UI has no fenced-code / canvas path, so even a parsed reply
looks like a wall of text.** Independent product gap (operator extras).
Not the hang mechanism.

## Phase 3 — Bayes (hang trio H1 / H2 / H3)

Priors from this stack: hand-rolled JSON that only escapes `\n` is a
known class; live Grok hangs exist but leave `thinking:[{…}]`; intel
holds leave a recap note.

- P(H1)=0.70. P(E|H1)=0.99. Unnorm=0.693
- P(H2)=0.20. P(E|H2)=0.05. Unnorm=0.010
- P(H3)=0.10. P(E|H3)=0.05. Unnorm=0.005
- Sum=0.708

```
P(H1|E)=0.693/0.708=0.979
P(H2|E)=0.010/0.708=0.014
P(H3|E)=0.005/0.708=0.007
```

H4/H5 are independent product claims, not competing hang hypotheses.

## Phase 4 — Second debate

**Data:** H1 wins 0.979 − 0.014 = 0.965. Scoped fix is JSON C0 escape.
**8/10** (cap: live PWA not restarted in this session).
**Sherlock:** Highest-VOI leftover is "does `hush_launch_json_escape`
have the same hole?" Yes (same `\n`-only body at
`hush_launch.c:1564`). Session names rarely contain tabs; do not block
the hang fix on unifying both helpers. **8/10**.
**Linus:** Smallest diff is one leaf that maps C0 → `\n` `\t` `\r` or
`\u00XX`, used by `hush_json_escape`. Canvas is a second milestone, not
the hang. Unfork is a GitHub settings click plus a README sentence.
**8/10**.
**Brian Cox:** Arrow of time matches H1. Momentum: the next Go snippet
will freeze the hive again until C0 is escaped. **9/10**.

Agreement: H1 is the hang. H5 is the asked product follow-through
(code blocks + canvas). H4 is a named risk, not this incident.
Unfork acknowledgement is README + operator GitHub click.

## Phase 5 — Scope

**Problem.** Happy answered. `hush_json_escape` emitted raw TABs.
`tick()`'s `r.json()` threw. The optimistic thinking chip never
cleared. The operator saw "Thinking ..." and concluded the robot
failed.

**Work.**

1. Escape every C0 control in `hush_json_escape` (and the twin
   `hush_launch_json_escape` so session JSON cannot repeat this).
2. Prove it: POST a tabbed note; `GET /api/events` must
   `json.loads` and contain `\t`.
3. Render fenced code as `<pre><code>` in notes. Notes with large or
   multi-file fences grow a download + canvas path: right-hand editor,
   file selector, download, save into a recorded project worktree when
   one exists.
4. README: Hush started as a fork of Buzz; Hush is not Buzz; we do not
   keep an upstream sync relationship.

**Out of scope.** Raising `HUSH_EVENT_MAX_CONTENT`. Streaming tokens.
Changing Grok argv / timeout. Detaching the GitHub parent (operator
must unfork or ignore Sync on github.com). NLP. New C modules for a
full IDE.

## Architecture lock

### JSON

`hush_json_escape` / `hush_launch_json_escape` map:

| byte | emit |
|---|---|
| `"` `\` | `\"` `\\` |
| `\n` | `\n` |
| `\r` | `\r` |
| `\t` | `\t` |
| other `c < 0x20` | `\u00HH` |

Need 6 bytes of room for `\u00HH`. Grow the `o + 2` guard to `o + 6`.
Named constants for the hex width. No new public API.

### Code in notes

`paintNote` walks fences. Text stays a `.body` node. Each ` ```lang `
… ` ``` ` becomes `<pre class="code-block" data-lang><code>`. A note
with one or more fences gets `.note-files` actions: Download (one
blob, or zip-less sequential downloads for 2+), Open in canvas.

### Canvas

`#code-canvas` is a right-hand pane (`position:fixed; right:0; top:0;
height:100%; width:min(36rem,46vw)`). Header: file `<select>` (hidden
when one file), Download, Save to project, [x]. Body: `<textarea
class="code-canvas-edit" spellcheck="false" wrap="on">`. Language
class from fence tag (`python`, `go`, …) is a `data-lang` hint for
color (CSS only; no highlighter library). Persist last size in
`localStorage.hush-canvas`.

Save to project: if `session.projects[]` is nonempty, POST
`/api/canvas` `{path, content}` writes under that project's `path`
only when the resolved path stays inside the project directory.
If there is no project, Download is the only persist path (no silent
write into `~/`).

`/api/canvas` is a tiny POST next to existing launch project paths.
Refuse `..`, absolute paths outside the project, and empty content.

### Fork

README second paragraph after the version line: Hush began as a fork
of [Buzz](https://github.com/block/buzz). It is now a standalone C11
Nostr relay. We do not track or sync Buzz. Keep `IMPORT.md` as a
one-way import guide.

## Risks

1. Escape room (`o + 2`) truncates `\u00HH` → keep `o + 6`.
2. Canvas POST abused as arbitrary write → path must stay under a
   recorded project.
3. Fence parser misfires on indented ` ``` ` inside prose → require
   fence at line start.
4. GitHub Sync button remains until the operator detaches the fork.
   README cannot remove it.

## Unanimous gate (plan)

| Voice | Agree | Score | Why |
|---|---|---|---|
| Data | yes | 8/10 | H1 0.979 + live body. Cap 8: PWA not reloaded yet. |
| Sherlock | yes | 8/10 | A1–A4 dead. Remaining unknown is launch-escape twin; fix both. |
| Linus | yes | 8/10 | Escape first. Canvas is one pane, not Monaco. |
| Brian Cox | yes | 8/10 | Timeline holds. Next Go paste will freeze the hive until C0 is escaped. |

## Success criteria

1. `GET /api/events` with a tabbed note is strict-JSON and contains
   `\\t`.
2. `check_launch.sh` greps canvas ids / fence painter.
3. `./configure && make && make test`.
4. README names Buzz as historical origin and denies ongoing sync.
5. PR merge + worktree removed.

## Non-goals

Monaco/CodeMirror. Syntax WASM. Raising 4096. Grok timeout/turns.
GitHub unfork API. Editing `/home/chuck/AGENTS.md`.

## Updated plan

See [`../plan/PLAN_CODE_CANVAS_JSON.md`](../plan/PLAN_CODE_CANVAS_JSON.md).
