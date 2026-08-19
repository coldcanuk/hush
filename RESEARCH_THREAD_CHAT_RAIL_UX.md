# RESEARCH: thread chat UX + tool-rail free drag (no docks)

Worktree: `/opt/repo/hush/worktrees/thread-chat-rail-ux`
Branch: `gb/thread-chat-rail-ux`
Base: `main` `5aec1f621` (PR #35)

Using the four-minds debug protocol. Mode: **TOOLED**.

## Phase 0 — Context Register

- MCP servers in this session: analyze, apps, buzz_publish, developer,
  extensionmanager, skills, summon, todo. No documentation or
  observability MCP exists for Grok Build or this hive UI.
- Consulted: none. `grok --help` and a live `grok -p` on this host are
  the cheapest falsifiers; they are local binaries, not MCP.
- M# items: none.

Data: no docs MCP can settle `--reasoning-effort` values; the binary
on PATH is the source of truth. **7/10**.
Sherlock: "Data, you claim MCP is silent — why not Context7?" Because
Grok Build 1.0.4 is a local CLI; the help text is on disk.
Linus: no objection. Reading Wikipedia before a null pointer is waste.
Brian Cox: the causal order is prior slice shipped `none` → grok 1.0.4
rejects it → empty stdout → static on-deck note. That arrow is one-way.

## What is broken (quoted)

1. Thread chat: “when I mentioned `@happy` I could see some visual
   feedback and the button for the thread. However, when i tried chatting
   with `@happy` I got some static reply”
   ```
   At ease. Grok Build returned nothing. — Happy
   ```
   repeated for “tell me a joke”, “that's a bad joke. try again”, “What?”
2. Thread UI: “The user interface look haphazardly setup as well. It was
   that friendly to use. I couldn't resize it and the UI wasn't consisted
   with the one that the user is accostomed to once the app launched.
   We want consistency… Fix the thread chat UI. This thread chat UI need
   to be able to support 1:1 and 1:n”
3. Tool rail: “When expanded, it can move around. When collapsed and a
   hamburg, it can move around. When I move it around there's these
   outlined squares I can move to, why? those look weird and I don't
   like it. Remove the idea of docking.”
4. “Double clicking the hamburger will send the icon to the upper left
   next to `hush \\n LOCAL HIVE`”
5. “When I'm in a thread chatting, I don't want to see the tool rail ux
   inside the thread menu. When I'm chatting in a thread just send the
   tool rail UX, in it's hamburg state, to it's anchor point to the left
   of `hush \\n LOCAL HIVE`”

What the operator already tried: chatting in the new thread pane
(verified by the paste). Assumption: Happy is configured as Grok Build
and authenticated (matches prior slices; this host has nonempty
`~/.grok/auth.json`).

## Phase 1 — Evidence

**E1** `hush-c/src/hush_agent.c:597-598`

```
argv[13] = (char *)"--reasoning-effort";
argv[14] = (char *)"none";
```

**E2** `hush-c/src/hush_agent.c:671-705` — `hush_agent_finish_job`:
when `ok && job->out[0] != '\0'` insert the note; else on-deck

```
ok ? "Grok Build returned nothing."
   : "Grok Build did not answer in time."
```

wrapped as `At ease. %s — %s` (`hush_agent_on_deck` lines 516-517).

**E3** Live `grok --help` on this host (`grok 1.0.4 (d846eb93d9) [stable]`):
`--reasoning-effort` exists. Live invocation:

```
--effort/--reasoning-effort: unknown effort level 'none'; use one of: xhigh, high, medium, low
```

stdout bytes = 0, process exit 1. stderr is discarded by
`hush_agent_exec_grok` (`dup2` of `/dev/null` onto STDERR).

**E4** Same argv with `--reasoning-effort low` (this session):

```
exit: 0
stdout bytes: 88
I told my suitcase there would be no more trips — now it's full of emotional baggage.
```

Full hush argv with `low` also returned a one-line joke (94 bytes).

**E5** `hush-c/demo/index.html:353-361` — `#thread-pane` is a `.drawer`
modal: `position:fixed; inset:0; background: rgba(9,9,11,0.72)` with
`.panel { width: min(28rem, 100%); }`. No resize handle. Composer is a
bare `<input>` + Send (`558-570`), not the hive `.composer-box` + pills.

**E6** `openThreadPane` (`2542-2558`) titles `Thread · Happy` and help
`You and Happy. At ease.` `threadMembers` unions robots mentioned on
the root plus robots that authored kids. Composer posts
`mention_0…mention_N` for every bot in that set (`2126-2133`). 1:n is
already in the mention list; the chrome still reads as a 1:1 modal.

**E7** `hush-c/demo/index.html:414-422, 2716-2845` — six `.rail-dock`
dashed 44×44 squares (`#rail-docks`) paint while dragging. Snap radius
48px. `localStorage.hush-rail` stores `{x,y,collapsed,anchor}`.

**E8** Header brand block (`440-450`):

```
<div class="title">hush</div>
<div class="sub" id="vibe-sub">local hive mind</div>
```

Default `#tool-rail` CSS is `top:10px; right:12px`. No code parks the
collapsed hamburger to the left of that brand. No `dblclick` on
`#rail-toggle`. `#tool-rail` z-index 30 sits above `.drawer` z-index 20,
so the rail paints on top of the thread modal.

**E9** `UI_SPEC.md:343-351` currently requires the six docks. Tests
`check_launch.sh:63-64` require `id="rail-docks"` and `railAnchor`.

**E10** `check_agent.sh` fake grok is `echo "Why did the robot laugh? Byte me."`
and only greps that `--reasoning-effort` exists, not the value `none`.
That is why CI stayed green while live Happy posted E2.

### Smuggled assumptions

| Id | Claim | Status | Falsifier |
|---|---|---|---|
| A1 | Happy is Grok Build and authenticated | consistent with E3/E4 + nonempty `~/.grok/auth.json` | session `agents[].provider` |
| A2 | “returned nothing” means grok hung | FALSIFIED by E3: grok exits immediately with a flag error | already run |
| A3 | Thread pane is a hive-consistent chat | FALSIFIED by E5 | already read |
| A4 | Docks are required for free move | FALSIFIED by E7: drag already works; docks are overlays | already read |
| A5 | 1:n needs a new protocol | FALSIFIED by E6: mentions already fan out | already read |

Sherlock on A2: “What would falsify empty-stdout-as-hang?” The cheapest
check is `timeout 45 grok -p … --reasoning-effort none`. Ran. Exit 1
in ~1s. Hang is dead.
Linus strikes any restatement of “Happy is broken” as `[not evidence]`.
The clue is the flag value, not the paste.

### Timeline (Brian Cox)

1. Prior slice `gb/thread-think-hygiene` locked `--reasoning-effort none`
   from an older grok README (`RESEARCH_THREAD_THINK_HYGIENE.md:186`).
2. Host grok is now 1.0.4; allowed efforts are `xhigh, high, medium, low`.
3. Operator `@Happy tell me a joke` → child execs E1 → grok writes the
   error to stderr (devnull) and exits → pipe EOF → `finish_job(ok=1)`
   with empty `out` → E2 static note.
4. Follow-ups in the thread re-dispatch the same argv → same static note.
5. Independently, docks (E7) and the modal drawer (E5) shipped as designed.
   Those are product misses, not regressions of the grok flag.

## Phase 2 — Hypotheses

**H1** `--reasoning-effort none` is rejected by grok 1.0.4, so every
Happy reply is the empty-stdout on-deck line.
Explains E1–E4 and the operator paste. Does not explain docks or
resize. Falsifier: already run (E3/E4). Data **9/10**. Linus **9/10**.

**H2** Happy is not authenticated / binary missing, so start_grok never
runs. Does not explain the exact string “Grok Build returned nothing.”
That string is only in `finish_job` after a started job (E2), not the
generic on-deck “Standing orders are noted.” Falsified by the paste + E2.

**H3** Thread composer does not mention Happy, so follow-ups never
re-dispatch. Falsified by E6 + the repeated Happy replies in the paste.

**H4** The thread pane is a 28rem modal overlay, not a hive chat surface;
it cannot be resized and does not reuse the hive composer. Explains the
UX complaint. Does not explain the static text. Falsifier: E5 (read).

**H5** Dock squares are intentional snap UI (`#rail-docks`) and sit
above the hive while dragging. Explains the outlined squares. Does not
explain Happy. Falsifier: E7 (read).

Sherlock: H1 does not explain the docks. Good — a theory that explains
everything explains nothing. These are two incidents that share a
session, not one mechanism.
Linus: H1’s fix is one string. H4/H5 are UI. Do not merge them into a
rewrite of hush_agent.
Brian Cox: H2 requires time reversal (a start that never happened still
emits `returned nothing`). Discard.

## Phase 3 — Bayesian (top three)

Priors from this codebase: flag-drift after a CLI bump is common (H1);
modal-drawer-as-chat is the prior slice’s explicit design (H4); docks
were a requested feature last slice (H5).

| H | Prior | P(E\|H) | Unnorm | Posterior |
|---|------:|--------:|-------:|----------:|
| H1 grok flag | 0.50 | 0.95 | 0.475 | **0.514** |
| H4 modal UX | 0.30 | 0.90 | 0.270 | **0.292** |
| H5 docks unwanted | 0.20 | 0.90 | 0.180 | **0.195** |

```
P(H_i|E) = P(E|H_i) P(H_i) / sum_j
0.475 / 0.925 = 0.514
0.270 / 0.925 = 0.292
0.180 / 0.925 = 0.195
```

These three are **independent product bugs**, not mutually exclusive
causes of one symptom. The table ranks *which lever first explains the
static reply*. H4 and H5 are separately authorized UX work.

Data: H1 is the static-reply lever. Margin over H4 is 0.222. **8/10**
on scoped flag fix. H4/H5 stay in scope because the operator asked.

## Phase 4 — Second debate

Data: H1 dominates the reply symptom. **8/10**.
Sherlock: the highest-VOI leftover is “does `low` still keep hygiene?”
E4 already answered with a one-line joke. Not worth waiting. **8/10**.
Linus: H1 diff is one token. Ship it. H4/H5 are the larger (authorized)
UI slice; keep them out of hush_agent. **8/10**.
Brian Cox: arrow of time matches H1. Docks and the modal are earlier
design, still live. **8/10**.

Agreement: all four yes on (1) change effort to `low`, (2) replace
docks with free drag + header hamburger home, (3) rebuild the thread
pane as a hive-consistent, resizable 1:1 / 1:n chat surface that parks
the rail.

## Phase 5 — Problem & scope

**Problem.** Happy’s static line is empty Grok stdout caused by an
invalid `--reasoning-effort none` on grok 1.0.4. Independently, the
thread surface is a non-resizable 28rem modal that does not match the
hive composer, and the tool rail paints dashed dock squares the
operator rejected.

**Scope of work.** Change the effort token to `low`. Capture grok
stderr into the empty-stdout path so a future flag miss is visible
instead of “returned nothing.” Rebuild `#thread-pane` as a resizable
hive-chrome chat (composer box, pills, notes) that lists every human
and robot in the thread (1:1 and 1:n). Remove `#rail-docks` and snap.
Keep free drag. Double-click hamburger (and entering a thread) parks
the collapsed rail to the left of the brand (`hush` / vibe name).
Leaving the thread restores the last free position.

**Out of scope.** Codex/Goose live CLIs. Nested NIP-10 trees. New
providers. Changing Close/Exit. Theme work. Tray.

## Unanimous agreement gate

- Data: **yes**. **8/10** — E3/E4 are quoted; UI is specified from E5–E8.
- Sherlock: **yes**. **8/10** — H2 is dead; we are not treating UX as
  the reply bug.
- Linus: **yes**. **8/10** — flag is one constant; UI stays in HTML.
- Brian Cox: **yes**. **8/10** — causal story is flag then empty then
  on-deck; docks are a separate geodesic.

## Architecture lock

### Grok argv

```
grok -p <note>
  --system-prompt-override <prompt+hygiene>
  --output-format plain
  --always-approve --no-plan --no-subagents --disable-web-search
  --max-turns 1
  --reasoning-effort low
  --cwd <empty agent-cwd>
  --disallowed-tools <same denylist>
  --rules <same rules>
```

Named constant `HUSH_AGENT_GROK_EFFORT` = `"low"`. Tests grep that
token, not merely the flag name.

Optional small observability: if stdout is empty after `ok`, take a
trimmed stderr tail (cap 200 bytes) into the on-deck why-line so the
next CLI bump does not look like silence. Keep `/dev/null` off stderr;
pipe both fds or a second pipe. Prefer one extra pipe read in
`hush_agent_read_job` only if it stays inside the fn budget; otherwise
leave stderr discarded and rely on the `low` token + test.

Decision: **do not** add a second pipe this slice. The live bug is the
token. Extra pipes are a Linus veto (larger than the failure).

### Thread pane (1:1 and 1:n)

- `#thread-pane` is a hive-surface panel, not a dimmed 28rem modal.
  Same tokens (`--surface`, `--line`, note cards, composer-box).
- Default size ~min(42rem, 92vw) × min(70vh, 640px). User can drag a
  bottom-right handle to resize; persist `{w,h}` in
  `localStorage.hush-thread`.
- Header: `Thread · Name` (1:1) or `Thread · A, B, C` (1:n). Subline
  lists every participant including the signed-in human:
  `you · Happy` or `you · Happy, Payne`.
- Stream: root + descendants from any participant (human or any robot
  in the member set). Same `.note` chrome as the channel.
- Composer: reuse pill + leftover input. `@` opens the mention box
  scoped to hive roster so a 1:1 thread can become 1:n by adding a
  robot. Submit posts `reply_to=<root>` and `mention_0…N` for every
  robot currently in the member set **plus** any new pills.
- `[x]` closes. Escape closes. Reopen via the same Thread button.

### Tool rail

- Delete `#rail-docks`, `.rail-dock`, `railDocks`, `nearestRailDock`,
  `paintRailDocks`, `showRailDocks`, `applyRailAnchor`, `railAnchor`.
- Drag still works from `#rail-grip` (expanded or collapsed). Position
  is free, clamped on screen, saved as `{x,y,collapsed}`.
- Home point: immediately to the left of `.brand` (the `hush` /
  `LOCAL HIVE` block). Named `placeRailAtBrand()`.
- Double-click `#rail-toggle` (hamburger): collapse if needed and
  `placeRailAtBrand()`.
- When `#thread-pane` opens: force collapsed + `placeRailAtBrand()`.
  Remember the pre-thread `{x,y,collapsed}` and restore on close
  unless the human dragged during the thread (then keep the new free
  pos).
- Tests: `check_launch.sh` drops `rail-docks` / `railAnchor` greps;
  require `placeRailAtBrand`, `dblclick`, and thread-pane resize.

## Risks

1. grok effort enum drifts again → test greps `low`; next miss is a
   one-token fix.
2. Thread resize fights the drawer overlay → drop the full-viewport
   dimmer; pane is a floating hive panel (`z-index` 25, rail 30 only
   when not in-thread; in-thread rail stays 30 but parked at brand,
   outside the pane).
3. 1:n mention fan-out double-starts jobs → existing `hush_agent`
   already starts one job per `p` tag; do not mention the human.

## Success criteria

- Live-shaped argv with `low` is what `hush_agent_exec_grok` writes.
- Thread pane is resizable, hive-consistent, 1:1 and 1:n.
- No dock squares exist in HTML/CSS/JS.
- Double-click hamburger and open-thread both park the collapsed rail
  left of the brand.
- `./configure && make && make test` pass.
- PR merged; worktree removed.
