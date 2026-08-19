# RESEARCH — Thread scroll, compose, canvas, rail

Worktree: `/opt/repo/hush/worktrees/thread-ux`
Branch: `gb/thread-ux`
Base: `main` `53c9779e1` (PR #47)

Using the four-minds debug protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP / extensions in this session: analyze, apps, buzz_publish, developer,
  extensionmanager, skills, summon, todo. Available but unused:
  summarize, tom, code_execution, chatrecall, computercontroller,
  autovisualiser, memory, tutorial.
- No documentation or observability MCP for the PWA, Grok CLI, or the
  live hive. Skip further MCP: the snap-back writer is in
  `hush-c/demo/index.html`.
- Consulted: `index.html` compose / thread / canvas / rail, `tick()`,
  `UI_SPEC.md` §13–§15, `hush_agent.c` job table, `hush_http.c`
  `/api/canvas` and POST dispatch, `check_pwa.sh`.
- No M# items.

**Data:** docs MCP is absent; the 1 Hz `openThreadPane` rewrite is the
instrument. **8/10**.
**Sherlock:** "What is the docs MCP conspicuously silent on?" Whether
the operator scrolled `#thread-stream` or the whole pane. The code
path does not care: both lose their place when `innerHTML` is wiped.
**Linus:** We do not read Wikipedia before deleting a forced
`scrollTop`. **9/10**.
**Brian Cox:** Causal order is already in the file: `setInterval(tick,
1000)` → `render` → `openThreadPane` → `scrollTop = scrollHeight`.
**9/10**.

## What is broken (quoted)

Operator:

> when I write my reply in the input box, it is only 1 line. That
> input box needs to be a 6 line input box, it word wraps. When we go
> over the 6 lines there's a up<->down bar that let's us scroll
>
> When I'm in the thread window and I scroll up, something is
> refreshing and immediately pulls it back down.
>
> Canvas: I noted that it did not color/pretty'ify the Go language.
>
> Feature Request: When editing in the canvas, I can select a section
> and `CNTRL+K` will open a small window and I can have the AI
> assistant fixup text
>
> The tool rail UX needs some fixing. The buttons are way to big. I
> like the position of the install. that one stays as is. The help
> text describing the install button, i want it as a pop-up when you
> click the `i`.

Mock-up of the expanded rail:

```
[         install         ]
[i]{...help text...}
<---dividing bar--->
[Profile] [Settings]
[   Call  ] [  Blank  ]
<---dividing bar--->
[ Minimize][Maximize]
[ Close ] [   Exit    ]
```

## Phase 1 — Evidence

**E1** `#msg` and `#thread-msg` are single-line `<input>` elements.
`hush-c/demo/index.html:580`:

```
<input id="msg" autocomplete="off" maxlength="2000" placeholder="Write to the hive… @ a human or robot">
```

`:648`:

```
<input id="thread-msg" autocomplete="off" maxlength="2000" placeholder="Write in this thread… @ to add someone">
```

CSS `:266-269` sets `flex: 1; min-width: 8rem` and no `rows`, no
`overflow-y`, no `white-space`. An HTML `<input type=text>` cannot
wrap to six lines. [verified]

**E2** `#thread-stream` is a scroll box (`overflow: auto`, `:176-179`).
`openThreadPane` always wipes it and pins the bottom. `:3160-3165`:

```
const box = $("thread-stream");
box.innerHTML = "";
box.appendChild(paintNote(root));
kids.forEach((k) => box.appendChild(paintNote(k)));
paintThreadThink(jobs);
box.scrollTop = box.scrollHeight;
```

[verified]

**E3** `render()` reopens the live thread on every successful events
tick, with no "near bottom" test. `:3246-3247`:

```
if (openThread && $("thread-pane").classList.contains("show"))
  openThreadPane(openThread);
```

`tick()` polls every 1000 ms (`:3573-3574`). Even a stable event list
still calls `openThreadPane`. [verified]

**E4** The hive stream only rewrites when `key !== seen` (`:3211`) and
then also pins `s.scrollTop = s.scrollHeight` (`:3237`). The thread
path has no `seen` gate and no stick-to-bottom flag. [verified]

**E5** Canvas is a wrapping `<textarea id="code-canvas-edit">`
(`:666`). `UI_SPEC.md:394` locks "No highlighter library." CSS only
paints a 3 px left stripe for `python` / `go` / `javascript` /
`js` / `c` on `.code-block` in notes (`:216-219`), not on the canvas
textarea. `fenceName` maps go → `.go` (`:2977`) but `showCanvasFile`
only sets `data-lang` on the textarea (`:3034`). A textarea cannot
color tokens. [verified]

**E6** There is no `keydown` handler for `k` / `K` on
`#code-canvas-edit`. There is no `/api/ask` or `/api/fixup`. POST
dispatch in `hush_http.c:1183-1218` ends at `/api/provider/login`.
`hush_agent.h` exposes `init` / `shutdown` / `consider` / `poll` /
`status` only. A canvas Ctrl+K has no existing endpoint. [verified]

**E7** Tool rail markup (`:532-547`) is a single column: Install,
always-visible `#install-help` paragraph, Profile, Settings, Call
(hidden until `session.ready`, `:1056`), Close, Exit. No `i` button,
no Blank, no Minimize, no Maximize, no dividing bars. [verified]

**E8** Rail Fitts lock. `UI_SPEC.md:15` "Fitts: min 44px tap" and
`:451` "All ≥44px." `.iconbtn` is `min-width: 44px; min-height: 44px;
border-radius: 999px` (`:423-426`). `#rail-grip` is `min-height: 44px`
(`:496`). Operator: buttons "way to big" / "not big and obnoxious
like I'm 95." [verified — conflict between spec and this ask]

**E9** Install position. `#install` is the first control in
`.rail-body` (`:538`). Operator: "I like the position of the install.
that one stays as is." [verified]

**E10** Close and Exit both open `#hive-leave` (`:2518-2519`).
Minimize / Maximize / Blank do not exist. Window chrome is the
Chromium PWA / `--open` browser window, not a Hush process. [verified]

**E11** `hush_agent.c` job table is `HUSH_AGENT_JOBS_MAX = 4`
(`:23`). `hush_agent_finish_job` always inserts a kind-1 note
(`:896-905`). Reusing `consider` for canvas fixup would post a hive
note. [verified]

**E12** `check_pwa.sh` greps the served HTML for manifest, SW,
OAuth copy. It does not assert composer rows, scroll pinning, or
rail button sizes. [verified]

## Smuggled assumptions

| ID | Claim | Tag | Falsifier |
| --- | --- | --- | --- |
| A1 | "Something is refreshing" means a full page reload | assumption | E3 is a 1 s DOM rewrite, not `location.reload`. Falsified. |
| A2 | The hive `#stream` also snaps | unverified | Operator named the thread window. E4 would snap `#stream` only when `key` changes. Out of primary incident unless we see it. |
| A3 | Canvas must pull Highlight.js / Prism from the network | assumption | Operator asked for color. Spec currently forbids a highlighter library. A small in-page token painter avoids a CDN. |
| A4 | Ctrl+K must spawn a new Grok job via `/api/event` | assumption | E11: that posts a kind-1 note into the channel. Wrong surface. |
| A5 | Minimize / Maximize must be OS window controls | assumption | `--open` is a browser window. `document.documentElement.requestFullscreen` plus a collapse-to-header is the reachable pair. |
| A6 | Blank is a fourth product surface | unverified | Operator listed it next to Call. Cheapest reading: a reserved no-op / future slot that stays disabled, not a new app. |
| A7 | 44 px Fitts is still law | conflict | E8. This ask overrides tap size on the rail only. Keep 44 px on `#rail-toggle` and `#install` (install stays). |

**Sherlock on A1:** What would falsify a reload theory? Absence of
`location.reload` and presence of `innerHTML = ""` every tick. That
is E2+E3.
**Linus:** A2 is not this bug. Do not "fix" `#stream` unless the
operator names it.
**Brian Cox:** A4 is acausal for a canvas edit — the selection exists
in the textarea *now*; a kind-1 note is a later hive event.

## Timeline (Brian Cox)

1. Human opens Thread. `openThreadPane` paints and pins bottom. Correct.
2. Human scrolls `#thread-stream` up. `scrollTop` is now `< scrollHeight`.
3. ≤1 s later `tick` → `render` → `openThreadPane` again.
4. `innerHTML = ""` destroys the scroll box contents; `scrollTop` is
   reset; the function then writes `scrollTop = scrollHeight`.
5. Human perceives "something is refreshing and immediately pulls it
   back down."

No time reversal. The writer is later than the scroll. Momentum: the
interval never damps, so the snap repeats forever while the pane is
open.

## Phase 2 — Hypotheses (incident = thread snap)

**H1** `render` unconditionally recalls `openThreadPane`, which wipes
DOM and forces `scrollTop = scrollHeight`. Explains E2, E3, the
operator report. Does not explain the one-line composer (that is a
separate product ask). Falsifier: stop calling `openThreadPane` when
the painted note ids are unchanged *and* only pin when the user is
already near the bottom. **Data 9/10**.

**H2** A CSS `overflow` / flex bug on `#thread-pane` resets scroll
without JS. Does not explain the explicit `scrollTop` assignment in
E2. Falsifier: comment out the assignment; if snap remains, H2 lives.
**Linus: reject first. The assignment is sitting there.** **3/10**.

**H3** Service worker or `tick` full-page remount. Contradicted by
no `location.reload` in the tick path and by `#stream` using a `seen`
key. **Sherlock: a theory that needs a hidden remount when E3 is
visible is theatre.** **2/10**.

**H4** Thinking-chip updates force a rewrite even when notes are
stable, and that rewrite is what the human feels. Partially true
(`thinkKey` is in the hive `seen` key, not the thread path). The
thread path rewrites *even when thinking is empty*. H4 is a subset
of H1, not a rival. **Brian Cox: same geodesic.** **n/a as rival**.

Product asks (not hypotheses):

- P1 Six-line wrapping composer (`<textarea rows=6>`) on hive and thread.
- P2 Canvas token color for a locked popular-language list, including Go.
- P3 Canvas Ctrl+K inline fixup popover.
- P4 Compact two-column rail with `i` popup, dividers, Blank / Min / Max.

## Phase 3 — Bayes (H1 / H2 / H3)

Priors from this stack: chat UIs that re-render on a poll almost
always pin scroll (E2+E3 are the textbook bug); CSS-only snap without
a writer is rare when the writer is quoted; SW remount would also
reset the composer caret, which the operator did not report.

- P(H1) = 0.70. P(E|H1) = 0.95. Unnorm = 0.665
- P(H2) = 0.20. P(E|H2) = 0.15. Unnorm = 0.030
- P(H3) = 0.10. P(E|H3) = 0.05. Unnorm = 0.005
- Sum = 0.700

```
P(H1|E) = 0.665 / 0.700 = 0.950
P(H2|E) = 0.030 / 0.700 = 0.043
P(H3|E) = 0.005 / 0.700 = 0.007
```

H1 is the lever. Margin 0.907.

## Phase 4 — Second debate

**Data:** H1 posterior 0.950. Numbers justify a scoped thread-paint
fix. **7/10** (hard cap: not executed).
**Sherlock:** "Data, you claim H1, but why does E4 conveniently ignore
that `#stream` has the same `scrollTop` line?" Because the operator
named the thread window, and `#stream` is gated on `seen`. Value of
information after the fix is a static grep: `openThreadPane` must
not assign `scrollTop` unless `nearBottom`. Do not wait. **7/10**.
**Linus:** Sherlock, stop philosophizing. The cheapest falsifier *is*
the fix: keep scroll unless the user is at the bottom or the note
set grew. Do not add a virtual list. **7/10**.
**Brian Cox:** Linus, you are not compressing the timeline — the
interval is the clock. After the fix the geodesic is: tick may
refresh thinking, but `scrollTop` stays unless the human was already
pinned. **7/10**.

Agreement: H1. No distillation.

## Phase 5 — Scope

**Problem.** The thread pane cannot be read upward because every
1 s tick rebuilds `#thread-stream` and pins it to the bottom (H1,
P=0.950). The composer is a one-line `<input>` so a long reply
cannot wrap. The canvas is an uncolored textarea and has no inline
AI. The tool rail is a column of 44 px pills with always-on install
help.

**Work.**

1. Incident: paint the thread only when the note/think key changes;
   remember `scrollTop`; pin to bottom only when the user was already
   near the bottom (or the pane just opened).
2. Replace `#msg` and `#thread-msg` with 6-row textareas, wrap, overflow
   auto after 6 lines. Keep mention / Enter-to-send (Shift+Enter newline).
3. Overlay a read-only highlight layer on the canvas for a named
   popular-language list (Go included). No CDN. Textarea stays the
   editor.
4. Ctrl+K (and Ctrl+Shift+K) on the canvas selection opens a small
   popover. POST `/api/fixup` runs a one-shot `grok -p` that returns
   JSON `{ok,text}` and does **not** insert a hive note. Replace the
   selection with `text`.
5. Compact rail: Install stays first and full width. `i` toggles a
   popover of the current install help. Dividers. Two-column
   Profile/Settings, Call/Blank, Minimize/Maximize, Close/Exit.
   Normal button padding (not 44 px pills) on those pairs. Install
   and hamburger keep their current hit size.

**Out of scope.** Live hive restart (operator; pid may still be
pre-#47). GitHub unfork. Raising `HUSH_EVENT_MAX_CONTENT`. Streaming
Grok. Highlight.js / Prism / network fonts. Changing `#stream`
pinning. Real OS minimize of a browser window. Wiring Blank to a
new product. Dropping `--max-turns`. Joke / agent hygiene.

## Architecture lock

### Thread paint

`paintThreadStream(rootId, forcePin)`:

- Compute `threadKey = root.id + kids ids + think names`.
- If `threadKey === threadSeen` and the pane is already shown: do
  not touch `innerHTML` or `scrollTop`. Still refresh
  `#thread-think` text if needed via `paintThreadThink`.
- Else: `const near = box.scrollHeight - box.scrollTop - box.clientHeight < 48`
  (treat a just-opened pane as near). Rebuild. If `near || forcePin`,
  `scrollTop = scrollHeight`; else restore the previous `scrollTop`
  (clamp).
- `openThreadPane` on first open passes `forcePin=true`.
- `render` calls `paintThreadStream(openThread, false)`.

Named constant: `THREAD_PIN_PX = 48`.

### Composer

`#msg` and `#thread-msg` become `<textarea rows="6">`. CSS:

- `resize: none; overflow-y: auto; line-height: 1.4;`
- height = `6 * line-height + padding` (named, not magic).
- `white-space: pre-wrap; word-break: break-word;`

Enter submits (existing mention Enter still applies when the mention
box is open). Shift+Enter inserts a newline.

### Canvas highlight

Keep the textarea. Sit a `pre#code-canvas-hi` behind it, same font,
`pointer-events: none`, synced `scrollTop` / `scrollLeft`. Tokenize
with a small in-page scanner (strings, comments, keywords, numbers).

Language aliases (fence tag → highlighter id):

`latex, tex, r, bash, sh, zsh, powershell, ps1, plaintext, text, txt,
markdown, md, c, cpp, cc, cxx, h, hpp, rust, rs, php, python, py, go,
golang, ruby, rb, html, htm, xml, javascript, js, typescript, ts,
react, jsx, tsx, node, nodejs`.

Unknown → plaintext. Go must color `package` / `func` / `import` and
string literals. No network.

### Canvas Ctrl+K

`POST /api/fixup` body `{instruction, text}` (selection, or whole
buffer if empty). Caps: instruction 500, text
`HUSH_EVENT_MAX_CONTENT`. Reply `{ok:true,text:"…"}` or
`{ok:false,error}`.

Implementation: one extra job flavor on the existing 4-slot table
(`kind` note vs fixup). Fixup jobs do not call
`hush_agent_insert_note`. `hush_agent_poll` writes the trimmed stdout
into a one-slot result the HTTP handler waits on with a bounded
sleep (reuse `HUSH_AGENT_TIMEOUT_S`, wake on `poll`). No tools.
`--max-turns 1` for this path only (named
`HUSH_AGENT_FIXUP_TURNS`). Hygiene: return only the rewritten
selection, no fences, no preamble.

Popover: small `#canvas-k` under the selection (or top of canvas).
Input + Apply / Cancel. Esc closes. Ctrl+K while the popover is
open focuses the instruction box.

### Tool rail

Markup order:

1. `#install` (unchanged position, still first).
2. `#rail-info` (`i` in a circle) + hidden `#install-help` popover
   (same sentence as today).
3. `hr.rail-rule`
4. two-col: Profile, Settings
5. two-col: Call, Blank (`#blank-btn`, disabled, `title="Reserved"`)
6. `hr.rail-rule`
7. two-col: Minimize (`document.exitFullscreen` or collapse rail +
   hide hive chrome? **Minimize** = `window.blur` is useless.
   **Lock:** Minimize collapses the rail to the hamburger (same as
   `#rail-toggle` on). Maximize = `requestFullscreen` on
   `document.documentElement`; if already fullscreen, `exitFullscreen`.
8. two-col: Close, Exit (same `#hive-leave` as today).

`.rail-grid { display:grid; grid-template-columns:1fr 1fr; gap:6px; }`
`.rail-body .iconbtn { min-height: 32px; min-width: 0; border-radius: 8px;
padding: 4px 8px; font-size: 0.78rem; }`
`#install` and `#rail-toggle` keep current sizes.
`#install-help` is `display:none` until `#rail-info` is pressed;
`aria-expanded` on the `i`.

## Four-score on this lock (pre-fix, cap 7)

- Data **7/10** — would rise with a quoted `check_pwa` that greps
  `THREAD_PIN_PX`, `rows="6"`, `/api/fixup`, and `rail-info`.
- Sherlock **7/10** — would rise if a live thread scroll stays put
  across two ticks.
- Linus **7/10** — would rise if C stays inside `hush_http.c` +
  `hush_agent.c` for fixup only, and the highlight scanner is one
  file region, not a framework.
- Brian Cox **7/10** — would rise when the tick geodesic no longer
  writes `scrollTop` on a stable `threadKey`.
