# RESEARCH — Rail help + Configure Providers hub

Methodology: **RDAP** Phase 1.
Worktree: `/opt/repo/hush/worktrees/rail-prov`
Branch: `gb/rail-prov`
Base: `main` `ffd45116a` (PR #50 canvas FIM)

## Mode

**TOOLED.** Files and tests in this worktree. Live hive pid is still
the pre-#49 binary; this slice does not restart it.

Goose-doc-guide does not apply (not a Goose recipe or Goose
provider-config file). Hush providers are `hush_provider` +
`GET/POST /api/provider`.

## Symptom (operator)

Update the tool rail to:

```
[Install][i]
---
[Profile][i] [Settings][i]
[Call][i]    [Invite][i]{Click to invite a human to your vibe}
---
[Add Channel][i] [Configure Providers][i]
---
[New Robot][i] [New Project][i]
---
[Minimize] [Maximize]
[Close]    [Exit]
```

`[Configure Providers]` is the formal central location for all
provider configuration. Providers can be configured **globally**
and **per robot**. The left-nav robot cards already let a human
pick a runtime per robot. The rail button is the hive-wide desk.

## Evidence

**E1** `hush-c/demo/index.html:592-650` — rail v2 from PR #49 is
already on this base. Present: Install+`#rail-info`, Profile,
Settings, Call, Invite+`#invite-info`, Add Channel+`#chan-info`,
New Robot+`#robot-info`, New Project+`#proj-info`, Minimize,
Maximize, Close, Exit. Missing: `#profile-info`, `#settings-info`,
`#call-info`, any Configure Providers control. Profile and Settings
are bare `#iconbtn`s in one `.rail-grid`. Call has no `i`. Min/Max
and Close/Exit share one four-cell `.rail-grid`.

**E2** `UI_SPEC.md:497-534` — locked order has no Configure
Providers row and no `i` on Profile / Settings / Call. Help copy
exists only for install / invite / channel / robot / project.

**E3** `index.html:848-945` + `2061-2125` — `#provider-drawer` is
already the hive-global configure surface. `openProviderDrawer()`
reads `chosenProvider()` from `#agent-providers` radios, then
`GET /api/provider` + `fillProviderDrawer(id)`. Save is
`POST /api/provider`. OAuth is `POST /api/provider/login`. Scan is
`POST /api/provider/scan`. The pencil `#provider-cfg` only appears
on the selected Raise/Edit radio.

**E4** `hush_provider.h` — nine named runtimes. Status is
hive-global (`providers.json` + `pass` `hush/providers/<id>/*` +
home detect). There is no per-robot secret field and no
`hush/agents/<slug>/providers/` path.

**E5** `hush_roster.h:53` + `index.html:1648-1659` — a raised
robot stores one `provider` id. Payne stores a ranked list
(`payne.providers`, max 4). Left-nav cards show the label; Edit
opens `#agent-drawer` and checks that radio. That is the per-robot
surface the operator already has.

**E6** `RESEARCH.md` provider-configure lock (still in force) —
configure state is hive-global (one config per provider id), not
per agent. Raise posts only the id. Secrets never echo on GET.

**E7** `.rail-i` / `.rail-pop` (`index.html:472-490`, `2657-2682`)
— one delegated click handler on every `.rail-i`. New `i` buttons
that set `aria-controls` light up for free. No new JS per popover.

**E8** `check_pwa.sh` greps rail ids (`rail-info`, `invite-info`,
`chan-info`, `robot-info`, `proj-info`) and forbids
`class="create"`. No grep yet for a providers hub.

**E9** Drawer CSS: `.drawer` is `position:fixed; inset:0`. Two
`.show` drawers stack by DOM order. `#provider-drawer` already
sits after `#agent-drawer`, so a later `#providers-hub` must come
*before* `#provider-drawer` if the detail panel is to sit on top,
or the hub must close when the detail opens. Lock: hub stays open;
detail is later in the DOM so it covers the hub; Close on detail
returns to the hub.

## Assumptions

| Id | Claim | Status |
|---|---|---|
| A1 | Operator wants help copy generated for Profile / Settings / Call / Configure Providers | LOCK — "Generate help text" |
| A2 | Invite / channel / robot / project help stay as shipped | LOCK |
| A3 | Per-robot secrets (a second pass tree per slug) are **not** this slice | LOCK — E4/E6; would be a new research lock |
| A4 | Existing `/api/provider` is enough; no new C endpoint | LOCK — E3/E4 |
| A5 | Left-nav Edit + radio + pencil stay; hub does not replace them | LOCK — operator said that surface already exists |
| A6 | Live hive restart is still the operator's job | residual, same as #49/#50 |

## Hypotheses (architecture)

**H1 — Second `#provider-drawer` clone with new HTTP.** Reject.
Duplicates E3 and invents a second secret path.

**H2 — Per-robot overlay in `vibe.json`.** Reject this slice.
Conflicts with E6. Out.

**H3 — Rail-only markup: add the button, click opens the existing
drawer on the first unconfigured runtime.** Reject. Not "an entire
interface" and hides the global-vs-per-robot split.

**H4 — Accept. New `#providers-hub` drawer + rail button.**
Hive-wide roster of the nine runtimes from `GET /api/provider`.
Each row shows family, status line, OAuth `ready`, and which
robots (including Payne) currently point at that id. Row / ✎
opens the **existing** `#provider-drawer` for that id
(`openProviderDrawer(id)` takes an explicit id; Raise pencil
still uses `chosenProvider()`). No new C. No new secret store.
Rail also gains `i` on Profile / Settings / Call / Configure
Providers, and splits Min/Max vs Close/Exit into two grids.

## Architecture lock (H4)

```
#tool-rail
  Install [i]
  ---
  Profile [i]   Settings [i]
  Call [i]      Invite [i]
  ---
  Add Channel [i]   Configure Providers [i]   ← #providers-btn + #prov-info
  ---
  New Robot [i]     New Project [i]
  ---
  Minimize          Maximize
  Close             Exit

#providers-hub  (new drawer)
  title: Configure Providers
  copy:  Credentials are hive-wide. Each robot still picks which
         runtime it uses. Left-nav Edit is that pick.
  list:  one row per hush_provider id (9)
         label · status · used-by slugs · ✎
  click / ✎ → openProviderDrawer(id)   (existing panel)

#provider-drawer  unchanged fields / routes
#agent-drawer     unchanged radios + pencil
```

Help copy (generated, Payne-plain):

- `#profile-help`: “Your name, npub, and logout. This is you, not a robot.”
- `#settings-help`: “Theme, vibe visibility, and the local STUN/TURN server for calls.”
- `#call-help-pop`: “Start a mesh conference on this channel. Agents need Whisper to hear.”
  (id is not `#call-help` — that string already labels the stage.)
- `#invite-help`: keep “Click to invite a human to your vibe.”
- `#chan-help`: keep existing channel sentence.
- `#prov-help`: “Hive-wide credentials and OAuth for every runtime. Robots pick which one they use; secrets live once, here.”
- `#robot-help` / `#proj-help`: keep.

## Scope

**In.** Rail mock-up above. Four new `i` popovers. `#providers-hub`
listing all nine runtimes + used-by. Reuse `#provider-drawer`.
`openProviderDrawer(id)`. Spec + README. Tests.

**Out.** Per-robot secret overrides. New `/api/provider/*` routes.
C changes to `hush_provider`. Live DeepSeek/OpenAI spawn. Removing
the Raise pencil. Live hive restart. Changing Close/Exit verbs.
Raising `HUSH_EVENT_MAX_CONTENT`.

## Success / DoD

- Served HTML has `#profile-info`, `#settings-info`, `#call-info`,
  `#providers-btn`, `#prov-info`, `#providers-hub`.
- Rail order matches the lock. Min/Max and Close/Exit are two
  separate `.rail-grid`s.
- Hub lists every `PROVIDERS` id. A row opens `#provider-drawer`
  for that id without requiring a Raise radio.
- Raise/Edit pencil still opens the same drawer.
- No `curl/curl.h`, no `-lcurl`, no new C file.
- `make -C hush-c test` → ALL TESTS PASSED.
- Landed via PR; worktree removed; main clean.

## Risks

1. Two drawers both `.show` fight for the overlay. Mitigation: E9
   lock (hub earlier in DOM than `#provider-drawer`).
2. Used-by list misses Payne if we only walk `session.agents`.
   Mitigation: walk `robotModels()` (includes Payne).
3. `check_launch.sh` still greps “Raise a robot” / “Invite human”
   on drawers — keep those strings.
4. Rail overflow when every pair has an `i`. Mitigation: reuse
   existing `.rail-info-row` inside `.rail-grid` (Invite already
   does this).

## Phase 0 register

Extensions in play: buzz_publish, skills (worktree, write-legible-c
loaded; no C in this slice so c-standard is unused), todo.
Unused: apps, analyze, summon, goose-doc-guide (not Goose).
