# 2026-08-18 RDAP: Mention pills, tool rail, voice affordances, exit reaping

## Scope locked

Primary goal: make the hive chrome and mentions feel like the rest of the
pill/`+/−` language, put the header actions on a movable collapsible tool
rail, surface 1:1 and channel voice only when a speech model is present,
and make Exit actually stop every process Hush started so `make install`
cannot attach to a leftover binary.

Non-goals:

- Live LLM replies from a mentioned robot (still a follow-up).
- A full Whisper STT/TTS engine inside the C relay.
- Spawning real agent audio pipelines. Voice this slice is WebRTC
  conference + mute + invite tiles, gated on `whisper`.
- Force-closing an arbitrary browser tab the user opened themselves.
- Changing NIP-27 on the wire (`nostr:npub1…` + optional `p` tags stay).
- Tray icon, remote authenticated quit, Windows.

Success / DoD (measurable):

1. Manage Channel adds a human or robot with `+`. The name becomes a pill.
   `−` on the pill removes it. Checkboxes are gone.
2. Typing `@Happy` and choosing the hit inserts an `@Happy` pill in the
   composer. The input does **not** show `nostr:npub1…`. Send still posts
   `nostr:<npub>` in content and `mention_N` fields.
3. Channel delete `−` and robot-card `+`/`−` are compact (24×24 visual,
   not 44×44).
4. Install, Profile, Settings, Call, Close, Exit live in a tool rail the
   user can drag. The rail collapses to a hamburger. Install has help
   copy that states it installs the PWA/launcher shortcut.
5. When `GET /api/status` (or `/api/turn`) reports `whisper:true`, each
   raised robot card shows a Call icon (1:1 conference with that robot)
   and each channel row shows a Voice icon (channel conference). Hidden
   otherwise.
6. In a channel call the human can mute a robot tile the same way they
   mute a human tile.
7. Exit / `--quit` / SIGTERM stop the relay **and** the browser `--app`
   children and OAuth `xterm` children Hush forked. After Exit, port
   10555 is free and a newly installed binary is what `--open` starts.
8. `./configure && make && make test` pass. HTML greps cover pills, rail,
   hamburger, whisper-gated icons. Exit smoke test covers child reap.
9. PR → merge → worktree removed. Never write to `main`.

## Constraints

- C11 + write-legible-c §16 on existing modules. No function-static
  mutables, fn ≤40, depth ≤2, named literals.
- Worktree `/opt/repo/hush/worktrees/pills-rail-voice-exit` on
  `gb/pills-rail-voice-exit`. Land via PR only.
- Re-embed after every HTML change: `./scripts/embed-ui.sh hush-c/demo`.
- Color-blind theme: never green/red as the sole pair. Ready/voice use
  `--accent`. Delete/mute use `--warn` plus a label.
- Payne voice. No feline words.
- JSON stays string-field only. Caps already locked (8 mentions, 8
  humans, 8 robots, 16 channels).
- Fitts 44px is overridden **only** for the two sidebar glyphs the user
  called out (channel `−`, robot `+`). Tool-rail buttons stay ≥44px.

## Assumptions

- Speech model present = existing `hush_turn_whisper_available()`
  (`HUSH_WHISPER` set and not `0`, or `/usr/bin/whisper` /
  `/usr/local/bin/whisper` executable). No new model scanner this slice.
- Composer pills are a client overlay. Wire format stays NIP-27.
- Empty Manage Channel lists still mean the whole hive.
- Default listen port remains 10555.

## Top risks

1. ContentEditable composer fights `@` scan and Send → keep a hidden
   `#msg` (or a sibling store) that holds the NIP-27 text while the
   visible row is pills + leftover plain text.
2. Killing the wrong Brave process → only reap children we forked, plus
   `/proc` matches of `--class=hush-relay` **and** `--app=http://127.0.0.1:<port>`.
3. `killpg` of our own group suicides the relay before cleanup → never
   `killpg(getpgrp())`. Kill tracked child groups only.
4. Voice icons promised more than WebRTC tiles can do → copy must say
   “join the channel call” / “call this robot”; Whisper is the gate, not
   a new audio engine.
5. Tool rail position lost on reload → `localStorage.hush-rail`.

## Current-code findings

### Mentions (composer)

`applyMention` in `hush-c/demo/index.html` replaces `@Happy` with
`nostr:` + npub + space and pushes the npub onto `pendingMentions`.
`prettyMentions` only rewrites **rendered notes**, not the input. That
is why `@happy` becomes a raw Nostr address in the box.

Raise-robot and provider configure already have the pill language:
input + `+` → `.pill` with pencil/`−`. Manage Channel still uses
checkboxes (`#mh-N`, `#mr-N`).

### Chrome

Header is a flat row: Install, Profile, Settings, Call, Close, Exit,
badge. Install is a PWA `beforeinstallprompt` handler with **no** help
text. Call is shown whenever `session.ready`, not when Whisper is
present.

### Sizes

`.chan-del` and `.toggle` are `width: 44px; height: 44px; font-size:
1.25rem`. That is the space the user called out.

### Voice

`hush_turn_whisper_available()` already feeds `/api/turn` and
`/api/status`. Conference stage exists (`#stage`, WebRTC mesh, Invite
Payne tile). There is no per-robot Call icon, no per-channel Voice
icon, and no mute control on a tile.

### Exit / leftover processes (four-minds, TOOLED)

Using the four-minds debug protocol. Mode: TOOLED.

#### Phase 0 — Context Register

- MCP surveyed: analyze, apps, buzz_publish, developer, extensionmanager,
  skills, summon, todo. None is a process-observability or docs server
  for Unix process groups.
- Consulted: live `ps` / `ss` / pidfile, `hush_relay.c`,
  `hush_provider.c`, `hush_turn.c`, `check_exit.sh`, prior
  `RESEARCH_EXIT_CLOSE.md`.
- Data: no MCP contradicted the local evidence. Sherlock: the docs MCP
  is silent because there is none — silence is not a clue here. Linus:
  skip Wikipedia. Brian Cox: the leftover process predates this
  worktree (started 19:01; worktree created 19:14).

#### Phase 1 — Evidence

- **E1** `ps`: `294430 hush-relay --open` PPID=1, PGID=294429, listening
  `0.0.0.0:10555`.
- **E2** child `294435 bwrap … brave --class=hush-relay --app=http://127.0.0.1:10555/`
  still parented to 294430.
- **E3** orphan `294300 bwrap … brave --class=hush-relay --app=http://127.0.0.1:10555/`
  PPID=1 (previous spawn whose parent already died).
- **E4** `hush_open_app_window`: `pid = fork(); if (pid != 0) return;`
  then `execlp`. Child pid is discarded.
- **E5** `hush_relay_prepare`: `signal(SIGCHLD, SIG_IGN);`
- **E6** `hush_relay_cleanup`: turn, store, `close(ls)`, pidfile. No
  child kill.
- **E7** `hush_relay_quit`: `kill(pid, SIGTERM)` on the pidfile pid
  only.
- **E8** `hush_provider_spawn_login`: fork + return; child is xterm
  `-hold -e` (holds the window after login).
- **E9** installed binary `/home/chuck/.local/bin/hush-relay` mtime
  19:01 — same age as the live process.
- **E10** `check_exit.sh` asserts the relay pid dies; it never forks a
  UI child and never checks leftovers.
- **E11** `hush_relay_bind` on EADDRINUSE + `--open`: print “already
  running” and `hush_open_app_window` — this is how a leftover process
  serves an older UI after `make install`.

Smuggled assumptions:

- “Exit was clicked” — unverified. OS `×` and `#hive-close` are
  designed to leave the relay up. Falsifier: ask, or instrument.
- “make install replaces the running image” — false on Linux; the
  inode of a running binary stays. Falsifier: `ls -l /proc/294430/exe`.
- “ps | kill is the only fix” — leftover is the designed Close path
  plus unreaped children. Falsifier: Exit after the reap patch.

Timeline (Brian Cox): user launches → fork Brave → user “exits”
(window close or in-app Close, or Exit that only SIGTERMs the relay) →
Brave may reparent to PID 1 → later `make install` writes a new
`~/.local/bin/hush-relay` → `--open` hits EADDRINUSE → attaches to the
old listener. Effect (old UI) cannot precede the leftover listener.

#### Phase 2 — Hypotheses

- **H1** Exit/cleanup does not reap forked Brave/xterm children, so
  GUI (and sometimes the relay, if Close was used) survives.
  Explains E2, E3, E4, E6, E8. Does not by itself explain a still-
  listening relay if Exit was truly taken (E1).
- **H2** The user used Close / OS `×`, which is specified to leave the
  relay up. Explains E1, E11, “old version”. Does not explain E3
  orphans after a real Exit.
- **H3** Multiple test ports / stray unit-test relays. Does not match
  the single 10555 listener observed.
- **H4** EADDRINUSE attach after `make install` is the old-version
  mechanism. Explains E9+E11. Requires a leftover listener (H1 or H2).

Data: H3 has never been observed this session. Sherlock: H1+H2+H4
together explain everything a bit too neatly — keep them as a chain,
not one mega-theory. Linus: the cheapest lever is “kill what we
forked, and make Exit mean that.” Brian Cox: H4 is downstream of
H1/H2; fix the leftover listener first.

#### Phase 3 — Bayes (H1 leftover children, H2 Close-not-Exit, H4 attach-to-old)

Priors from this codebase: Close-vs-Exit was just shipped; users still
reach for the window `×`. Child pids are visibly discarded.

- P(H1)=0.35, P(E|H1)=0.80 → 0.280
- P(H2)=0.40, P(E|H2)=0.85 → 0.340
- P(H4)=0.25, P(E|H4)=0.90 → 0.225
- Normalize: H1=0.331, H2=0.402, H4=0.266

H2 is the leftover **relay**. H1 is the leftover **GUI**. H4 is the
user-visible “old binary” once either leftover exists.

#### Phase 4 — Second debate

- Data **7/10**: numbers favor H2 (Close/×) as the still-listening
  relay, with H1 as the orphan Brave. Margin is not huge (0.40 vs
  0.33).
- Sherlock **7/10**: the highest-VoI check is “did they click Exit?”
  We cannot ask mid-build; treat both H1 and H2 as in-scope. What would
  move posteriors most is a log line on `/api/exit`.
- Linus **8/10** on smallest diff: track forked pids, SIGTERM them in
  `hush_relay_cleanup`, and have `--quit` / Exit take that path. Also
  tell `--open` when it attached instead of started. Do not invent a
  supervisor.
- Brian Cox **8/10**: the arrow is leftover listener → attach → old
  UI. Killing children without killing a Close-left listener will not
  fix `make install`. Exit must remain the “every process stops”
  control; Close stays attachable.

Agreement: implement reap-on-Exit **and** keep Close semantics. Do not
auto-kill the relay when the last window closes (that would collapse
Close into Exit).

#### Phase 5 — Problem & scope (plain)

The bug is two stacked leftovers. (1) Close / window `×` leave
`hush-relay` listening by design, so the next `--open` attaches to
whatever binary is already mapped. (2) Even a real Exit does not reap
the Brave/`bwrap` or `xterm -hold` children because their pids are
thrown away and `SIGCHLD` is ignored.

This slice will track those children, SIGTERM them on Exit/`--quit`/
cleanup, and add a short “already running — this is the old process”
line on attach. It will not change Close, will not `killpg` the relay
group, and will not restart-on-install automatically.

#### Unanimous agreement gate (exit reap only)

- Data: yes, **8/10** — evidence is quoted; fix is local to relay
  spawn/cleanup.
- Sherlock: yes, **7/10** — still do not know if the user clicked
  Exit; the reap is correct either way.
- Linus: yes, **8/10** — smallest diff that matches “every process
  stops.”
- Brian Cox: yes, **8/10** — causal arrow matches E11.

## Architecture decisions (locked)

1. **Composer pills.** Visible composer is a `.composer-pills` row of
   `.pill` nodes plus a plain-text remainder in `#msg`. Selecting a
   mention inserts a pill and deletes the `@query`. `pendingMentions`
   stays. On submit, each pill serializes to `nostr:<npub>` in the
   posted content. Backspace at the start of `#msg` removes the last
   pill.
2. **Manage Channel pills.** Pool of unused humans/robots with a `+`.
   Added entries render as pills with `−`. Invite npub `+` commits a
   pill. Save still posts `human_0…` / `robot_0…`.
3. **Compact glyphs.** `.chan-del` and `.robot-card .toggle` become
   24×24, font-size 1rem. Tool-rail and form `+`/`−` stay 44px.
4. **Tool rail.** `#tool-rail` is `position:fixed`, draggable from a
   grip, collapse toggle is a hamburger. Contents: Install (with
   `title` + `#install-help` “Install puts Hush on your app launcher
   as its own window. It does not start a second hive.”), Profile,
   Settings, Call (shown when ready), Close, Exit. Position + collapsed
   flag in `localStorage.hush-rail`. Header keeps brand + badge only.
5. **Whisper gate.** `tick()` already fetches `/api/status`. Cache
   `status.whisper`. Robot Call and channel Voice icons render only
   when that flag is true. Header Call stays as the generic conference
   entry (already exists) but moves into the rail.
6. **1:1 robot call.** Icon on the robot card opens `#stage`, joins,
   and signals `{t:"join", role:"agent", from: robot.slug}`. Copy:
   “One-to-one with <name>. Mute any voice you do not want.”
7. **Channel voice.** Icon on the channel row opens `#stage` for that
   channel and invites every robot on the channel roster (or Payne if
   the roster is empty). Each tile gets a Mute button that disables
   inbound audio for that tile (local). Robots can be muted like
   humans.
8. **Exit reap.**
   - `hush_child_track(pid)` stores up to 8 pids.
   - `hush_open_app_window` and `hush_provider_spawn_login` track the
     child.
   - `hush_relay_reap_children()` SIGTERM + short wait + SIGKILL each
     tracked pid, then scans `/proc` for leftover
     `--class=hush-relay` + this port’s `--app=` URL and SIGTERMs
     those too (Linux; `#ifdef __linux__`).
   - Called from `hush_relay_cleanup` (Exit, SIGTERM, `--quit`).
   - Not called from Close / attach.
   - Attach message adds: “This is the process already listening. Exit
     or hush-relay --quit before a new install can take the port.”
9. **Tests.** HTML greps for `tool-rail`, `composer-pill`,
   `chan-voice`, `robot-call`, `install-help`. `check_exit.sh` starts
   `--open` (or a stub child via a test helper) and asserts no
   `--class=hush-relay` leftover for that port after `/api/exit`.

## NIP / voice notes

- Mentions stay NIP-27. Pills are UI only.
- No new NIP for groups this slice.
- WebRTC signaling remains kind 25000 as today.

## Verification already performed (research)

- Full read of mention/apply, manage-channel, paintRobots, header,
  call stage, whisper helper, relay fork/cleanup/quit, provider
  login spawn, check_exit.sh.
- Live process table and pidfile on port 10555 quoted above.
- Prior Close/Exit research still valid for the Close vs Exit split.

References: `RESEARCH_EXIT_CLOSE.md` (this directory), `UI_SPEC.md` §§4,10,12–14,
`hush-c/demo/index.html`, `hush-c/src/hush_relay.c`,
`hush-c/src/hush_provider.c`, `hush-c/src/hush_turn.c`.
