# PLAN_KEYS.md: KEYS epic, phase 1 (plan only)

Base: main 13511bd4 (#323), worktree branch `gb/keys-plan`
Date: 2026-10-17
Order: CoS 09:17 (Chuck-specified). Phase 1 is this document only. No app code.
Status: **APPROVED** with the CoS K0 rulings of 2026-10-17 09:46 and the CoS
ruling of 10:10 (§7.4). K1 is GO once #325 merges.
Gauge FAILed #334 at 59b48543 (10:16 ET). All of Gauge's fixes are adopted in
this revision (Appendix B has the claim trace). Gauge re-review is pending.
Path: the order asked for `docs/KEYS_PLAN.md`. AGENTS.md:83-88 says plans go
under `docs/plan/` as `PLAN_*.md` (93 on main 13511bd4; 94 with this file), so this is `docs/plan/PLAN_KEYS.md`.

Citations:

- `file:line` means main 13511bd4, which includes the #322 gate from PR #323.
- `index.html` means `hush-c/demo/index.html` (9440 lines at the base).
- Everything this plan proposes is marked **NEW**.

---

## 0. Summary

- **Keyboard today.** The UI already has three global key paths:
  - `i` and `c` (index.html:6011-6029)
  - `1`-`4` for the quickbar (index.html:6134-6140)
  - Escape, a dismiss chain (index.html:6012-6022)

  Two duplicate typing guards protect them: `navTyping` (index.html:5963-5968) and
  `quickbarTyping` (index.html:6099-6105). The only stored keyboard preference is
  `localStorage["hush-quickbar"]` (index.html:6067-6077).
- **Proposal.** One key registry and dispatcher, with the defaults below. A
  **Shortcuts & Macros** drawer (`?`) in the field-office ink-stamp family. One
  `hush-keys` v1 JSON format with import, export and reset.
- **Macros.** Macros are `trigger → conditions → actions`. A macro whose actions
  stay in the UI runs in the browser. Anything that reaches outside the hive runs
  **only on the relay**.
- **No approve or decline action.** No macro may approve or decline a turn
  (§4.3, Gauge F1). Auto-approve exists only as the relay `approval_mode`
  `approve_owner_asks`. Its exclusions are hard-coded on the relay, because the
  relay alone knows whether a held turn is a guest's (agent_approve.c:54, :66).
- **Agents.** Agents propose changes through a new system skill
  (`skills/system/keys-macros/SKILL.md`) that emits a fenced `hush-keys`
  proposal. The owner applies it by hand.
- **Token caveat (Gauge F3).** Hush does not *hand* robots the session token,
  but it doesn't keep them from it either:
  - The worker setup does not scrub the environment (agent_process.c:65-74)
    before `execvp` (:413).
  - The token sits in `$HUSH_HOME/session.token`, 0600 and same uid
    (hush_auth.h:18, :22-23).
  - Any unauthenticated loopback request gets it back as a `Set-Cookie`
    (hush_http.c:286-290, :732-738).

  Until #336 is fixed, a shell-capable turn can read or fetch the token and act
  as the owner. The propose-only agent path is therefore UX, not a security
  boundary (§7.0).
- **Security headline.** A browser macro that auto-posts "Yes" would answer a
  **guest** held turn as the owner. It would set `approved`
  (agent_dispatch.c:1386-1391), which sets `person_gate`
  (agent_prompt.c:153), which grants project cwd plus Cline
  `--auto-approve true` (agent_team.c:327-342,
  agent_process.c:368-377). That is exactly the hole #322 closes.
- **Ruled (C-1, C-2, C-3).** Auto-approve is a relay rule: a **new
  `approval_mode` value, `approve_owner_asks`**, that the premade macro card
  toggles. It runs only turns the owner asked for directly. Robot-chained,
  follow and election turns stay held, and guest turns stay held and send an
  alert. These exclusions are hard-coded, not conditions (§4.4, §7).
- **Dependencies.** K5-K9 depend on #336, the P1 fix for C-5: agent sandbox, env
  scrub, per-action nonce and Origin check (§8).

---

## 1. Action audit

### 1.1 Sources

- The turn-1 W1 probe (`/workspace/w1w2-audit/probe/inventory.json`, 180 controls
  over 33 surfaces: 114 actions, 62 fields, 2 tiles, 2 rows). The probe ran on
  the PR #325 head d58185ab.
- Every `addEventListener` in index.html at the base.

The full control-by-control table is Appendix A. Markup and handler lines there
were re-resolved against 13511bd4 (index.html is identical to 900a486d).

### 1.2 Keyboard handlers that exist today

| line | target | keys | behaviour |
|---|---|---|---|
| 4380 | `.fo-dial` | Arrows, PageUp/Down, Home/End | scroll the stream |
| 4425 | `#new-chan` | Enter | save channel |
| 4781 | provider row | Enter, Space | open provider |
| 5350 | `#new-proj` | Enter | click `#add-proj` |
| 5475 | `#msg` | Enter (Shift+Enter newline); mention box Arrow/Enter/Tab/Escape | send / pick mention |
| 5593 | document | Escape | close thread pane (or its mention box) |
| 5821 | document (capture) | Tab | focus trap in top layer |
| 6011-6029 | window | Escape chain; `i` / `c` (no Ctrl/Meta/Alt, not typing) | `toggleRobotInventory` (8877) / `toggleCharacterSheet` (5896) |
| 6134-6140 | window | `1`-`4` (no modifiers, not typing) | `runQuickSlot` |
| 6200 | `#dev-log` | Space | toggle dev log |
| 7262 | `#code-canvas-edit` | Tab accept FIM, Escape clear, Ctrl/Meta+K | `showCanvasK` (7211) |
| 7281 | `#canvas-k-ask` | Escape, Enter | cancel / apply |
| 9219 | `#thread-msg` | Enter + mention keys | send in thread |

**Escape order** (index.html:6012-6022; `dismissJourneyLayer` 5985-6008):

1. skill hold
2. `#hive-leave`
3. journey layer, in this order:
   1. dev-log
   2. avatar
   3. forge
   4. skill
   5. new-chan
   6. manage-chan
   7. agent
   8. inv-expand
   9. profile
   10. settings
   11. qb-editor
   12. kit-menu
4. `closeNavigation` (4254)

### 1.3 Action catalog today

`QUICKBAR_ACTIONS` (index.html:6060-6066) is the only named-action registry:

| id | runs |
|---|---|
| inventory | `quickInventory` |
| character | `quickCharacter` |
| new-channel | `quickNewChannel` (6052-6055) |
| new-robot | `openAgentDrawer(null)` (4918) |
| stop | `quickStop` (6056-6059): clicks the **first** live `.think-stop` in the DOM, not necessarily the visible thread's |
| leave | clicks `#hive-close`, which opens the leave dialog (5928) |

### 1.4 Gaps found

- The two typing guards are copies (5963-5968 vs 6099-6105). Neither checks
  `ev.isComposing` or `ev.repeat`. Holding `i` re-toggles.
- Matching is on `ev.key` (6025, 6135). On AZERTY the unshifted digit row gives
  `& é " '`, so quickbar `1`-`4` need Shift there.
- No `?` help or cheat sheet. No key lists other than the `<kbd>` chips:
  - `#nav-inventory` and `#nav-character`: index.html:2250-2252
  - `#qb-1`..`#qb-4`: index.html:2350-2353
- Single-character shortcuts cannot be turned off or remapped. WCAG 2.2 SC 2.1.4
  (Character Key Shortcuts) requires one of: off, remap, or focus-only. The
  quickbar slots can be re-pointed (index.html:6127-6131), but their keys are
  fixed.

---

## 2. Default shortcut map (proposed)

All rules below are the existing rules (6025-6027) moved into one dispatcher:

- **Global keys** fire only with no Ctrl, Meta or Alt.
- **Typing guard.** They never fire when the target is `input`, `textarea`,
  `select` or `contenteditable`. They also never fire while `ev.isComposing`
  (**NEW**) or on `ev.repeat` (**NEW**). The dispatcher uses a single shared
  guard (**NEW**) that replaces both copies.
- **Off switch.** "Single-key shortcuts" (**NEW**) disables every global letter
  and digit key (SC 2.1.4). It defaults to on, as today.

| key | action | status | wired to (cite) |
|---|---|---|---|
| `i` | Inventory | existing | index.html:6025-6029 → `toggleRobotInventory` 8877 |
| `c` | Character sheet | existing | index.html:6025-6028 → `toggleCharacterSheet` 5896 |
| `1`-`4` | Quickbar slots | existing | index.html:6134-6140 → `runQuickSlot` 6095 |
| Esc | Close top layer / thread | existing | index.html:6012-6022, 5593 |
| `?` | Open Shortcuts & Macros | **NEW** drawer | §3 |
| `/` | Focus composer `#msg` | **NEW** key | `#msg` index.html:2309 |
| `n` | New channel | **NEW** key, existing action | `quickNewChannel` 6052 → handler 4398 |
| `r` | New robot | **NEW** key, existing action | `openAgentDrawer(null)` 6063 / 4918 |
| `x` | Stop a live robot | **NEW** key, existing action | `quickStop` 6056 → `stopJob` 6773 → `POST /api/cancel` 6776 |
| `k` | KIT menu | **NEW** key, existing action | `toggleKitMenu` 5936 (`#rail-toggle` 9190) |
| `b` | Boards drawer | **NEW** key, existing action | `#nav-toggle` 4258 |
| `,` | Settings | **NEW** key, existing action | `openSettings` 5800 (`#settings-btn` 5819) |
| `p` | Profile | **NEW** key, existing action | `#profile-btn` 5901 |
| `h` | Providers hub | **NEW** key, existing action | `openProvidersHub` 4790 (`#providers-btn` 5074) |
| `[` / `]` | Previous / next channel | **NEW** key + **NEW** function | channel buttons built at 3568; `channel = c.slug` 3573 |

**Layer-scoped keys.** These stay as they are and cannot be rebound in v1:

- Enter in fields (4425, 5350, 5475, 9219)
- Tab trap (5821)
- canvas Tab, Esc and Ctrl/Meta+K (7262)
- `.fo-dial` arrows (4380)
- dev-log Space (6200)

**Layer-scoped key added (C-8 ruling):** Ctrl/Cmd+S in the code canvas clicks
`#canvas-save` (7294). This is **NEW**.

**Bindable but unbound by default:**

- `#call-btn` (6172)
- `#invite-human` (5228)
- `#stats` (5717)
- `#skill-forge-open` (9292)
- `#inv-seed` (9289)
- `#thread-desk-archive` (5629)
- canvas download (7290)
- the remaining rows marked "menu-bindable" in Appendix A

**Never bindable**, by key or by macro. These are destructive, identity or
approval controls. The list is a UX guard in the browser, not a security
boundary: any token holder can call the same routes (§7.0).

- `#hive-close` / `#hive-exit` (5928-5929)
- `#leave-close` / `#leave-exit` (5950, 5957): `POST /api/close` and `/api/exit`
- `#agent-delete` (5320)
- `#profile-logout` (6141)
- `#reveal-key` / `#copy-key` / `#ack-key` (3430-3435)
- `#vibe-rotate` (6182)
- `#inv-clear` (9290)
- the approval radios `#approval-auto` / `#approval-every`: markup 2390-2395,
  handler 6159-6169 posts `{approval_mode}`
- the gate flows: `#create-id`, `#use-id`, `#begin`, `#do-vibe`, `#meet-payne`

### 2.1 Conflicts with browser, OS and assistive technology

These come from platform behaviour, not from repo code. The menu refuses or
warns as listed.

| key(s) | owner | handling |
|---|---|---|
| Ctrl/Cmd+T, W, N, Shift+N, Q, Tab | Browser tab/window. Chrome does not deliver Ctrl/Cmd+W, T or N to pages | **Refuse** |
| Ctrl/Cmd+L, R, F, P, D, H, J, 1-9; F5, F11, F12; Ctrl+Shift+I/J/C | Browser chrome / devtools | **Refuse** |
| Ctrl/Cmd+S | Browser Save page | Allowed (C-8 ruling). Default: canvas-scoped Save (`#canvas-save` 7294) while the code canvas is open. Bindable elsewhere |
| Ctrl/Cmd+K | Browser search box on Chrome/Firefox (Win/Linux). Already canvas-scoped at 7262 | Keep canvas-only. Never global |
| `/`, `'` | Firefox Quick Find when focus is outside a field | `/` default calls `preventDefault()`. Warn in the menu. `'` unused |
| Alt+letter | Win/Linux menu-bar accelerators. macOS Option+letter types characters (`ev.key` = `å`, …) | Never a default. Warn |
| Alt+←/→ | Browser back/forward | **Refuse** |
| Space, PageUp/Down, Home/End, arrows | Page scroll; `.fo-dial` (4380) | Never global |
| Escape | Exits fullscreen and stops page load before the page sees it; existing chain | Not rebindable |
| Tab / Shift+Tab | Focus order; trap at 5821 | Not rebindable |
| Cmd+Space, Cmd/Alt+Tab, Super/Win, Win+L, Ctrl+Alt+Del, Ctrl+Alt+T, Ctrl+Alt+arrows | OS (macOS Spotlight, app switcher, GNOME terminal and workspaces) | **Refuse**; the page never sees most of them |
| Single letters (`h b k x c i n r p`) | NVDA/JAWS browse-mode quick keys (h heading, b button, k link, x checkbox, c combo, i list item); VoiceOver quick nav | Off switch + remap (SC 2.1.4). Show this note in the menu |
| Digits / punctuation on non-US layouts | AZERTY digit row, QWERTZ `/` = Shift+7, etc. | **NEW**: store `ev.key` and also accept `ev.code` `Digit1`..`Digit4` for slots. The menu shows the captured key |
| Desktop window (`/api/window`, 5943-5946) | Embedded window shell, if any | Not verified in phase 1. Check in K2 |

---

## 3. Shortcuts & Macros menu (spec)

- **Surface.** One `.drawer` dialog, `#keys-drawer` (**NEW**). It uses the same
  `openLayer` path as the dev log (openLayer used at 7904). Add it to
  `dismissJourneyLayer` (5985-6008) so Escape closes it.
- **Openers:**
  - `?`
  - a `KEYS` stamp in `#kit-menu`
  - a quickbar catalog entry `keys` (**NEW**, added to `QUICKBAR_ACTIONS`
    6060-6066)
- **Look.** Dossier-khaki, tokens only:
  - Paint only through `--bg --surface --surface-2 --line --fg --muted --accent
    --accent-ink --fo-*` (index.html:1157-1180). The token gates
    `check_palette_tokens.py` S1-S4 apply (UI_SPEC.md:1454-1456).
  - Every button is the ink-stamp family (UI_SPEC.md:1435-1444; CSS precedent
    `.loop-stamp` index.html:2145-2160):
    - 22px box, `0 7px` padding
    - 1px `--accent` border plus inset double rule, 2px radius
    - 700 0.62rem mono caps at 0.16em, rotated -1.2deg
    - 2px `--fg` focus ring at offset 3
    - 44px touch hit at `(max-width:640px),(pointer:coarse)`
  - Add the drawer to the `check_palette.sh` C1-C4/B1 surface list
    (UI_SPEC.md:1456-1465).
- **Layout.** A game keybind screen with three folder tabs (`data-folder-tab`,
  the same pattern as `#qb-1`..`#qb-4`, 2350-2353): **KEYS · MACROS · LOG**.
  - **KEYS.** One row per action from the registry, grouped Journeys / Hive /
    Thread / Robots. Each row shows the action label, a primary-key slot and an
    alternate-key slot (`<kbd>`), plus a `CLEAR` stamp.
    - Click a slot and it shows `PRESS A KEY…`. Esc cancels.
    - A refused key (§2.1) stamps `RESERVED` with the reason.
    - A key already in use stamps `IN USE: <action>` with `SWAP` / `CANCEL`.
    - Footer stamps: `SINGLE-KEY SHORTCUTS ON/OFF`, `IMPORT`, `EXPORT`,
      `RESET KEYS`.
  - **MACROS.** One dossier card per macro:
    - name
    - an origin stamp: `PREMADE`, `MINE` or `AGENT DRAFT`
    - trigger, conditions and actions, read as sentences
    - an `ON` / `OFF` switch, which uses the existing field-office switch styling
    - `EDIT`, `DUPLICATE`, `DELETE`

    Relay-run macros carry a `RELAY` stamp. Approval macros carry an
    `--accent` `APPROVES TURNS` stamp. `NEW MACRO` opens a three-step form:
    `WHEN`, then `ONLY IF`, then `DO`.
  - **LOG.** The macro audit trail (§7.3), newest first. Read-only.
- **Accessibility.**
  - The drawer is `role="dialog"` and gets the Tab trap at 5821.
  - Each key slot has `aria-keyshortcuts` and a label: "Inventory, primary
    key i".
  - Global keys are suspended while a capture is in progress.

**Related: GUARD UI epic (Chuck 09:56).** This is a note only: no design
detail and no KEYS milestone.

- **Shared drawer.** A Guardrails table will sit next to Shortcuts & Macros in
  the same drawer. Saved rules have match, scope, action, hits and created
  columns, with inline edit, delete and add. Floor owns the rule engine and data
  model.
- **Drawer room.** The KEYS · MACROS · LOG folder-tab strip must leave room for
  a fourth tab, GUARDRAILS. The drawer width, tab sizing and Escape/Tab handling
  should not assume three tabs.
- **Approval prompt.** The prompt is getting S1 stamp buttons: **Approve |
  Always approve | Ask every time | Reject**.
- **No clash with the premade auto-approve.** The premade (the
  `approve_owner_asks` mode, §4.4) must not conflict with "Always approve"
  rules:
  - Both decide at the same relay point (`approval_needed`,
    agent_approve.c:40-46).
  - An explicit per-rule Reject or Ask every time wins over the mode.
  - The mode never writes Always-approve rules.
  - Neither path may run a guest turn without an owner decision (§7.1, #322).
  - **Ruled (CoS 10:10).** By default, Always-approve (GUARD) rules do **not**
    cover robot-chained turns, which matches C-3. The owner may opt in per rule
    later, after Gauge review.
  - **Bind answers to a hold (Gauge F10).** Today a typed Yes answers the
    *oldest* held turn in the thread (agent_approve.c:102). The prompt's stamp
    buttons should each carry a hold id + nonce (#336), so a click answers
    exactly the turn on screen.
- **Floor co-design adopted (#335, `docs/guard-plan.md` §7).**
  - The pending card sits inline in the source thread, and decisions happen only
    there.
  - The GUARDRAILS tab in this drawer shows a pending badge.
  - Always means an exact match at robot scope. The scope pick widens it, and
    prefix or glob matching is only possible through the table or a custom row.
  - Rules live in a separate `hush/guardrails.json`, not in `hush/macros.json`.
  - The stored action is one of `approve | reject | ask`.

---

## 4. Macro model

```
macro = { id, name, origin: premade|user|agent, enabled, runs: browser|relay,
          trigger, conditions[], actions[], created_by, updated_at }
```

**Where it runs.** `runs` is derived, never chosen. If any trigger, condition or
action is relay-only, the whole macro is `relay`.

### 4.1 Triggers

| trigger | exists today as | where | runs |
|---|---|---|---|
| `key` | keydown dispatcher | index.html:6011-6029, 6134-6140 (to be unified) | browser |
| `note.new` (channel/thread) | `/api/events` poll every 1 s | `tick` 9060-9064, `setInterval(tick,1000)` 9412, `render` 7673 | browser |
| `loop.ask` ("Continue this loop?") | client prefix test | `isLoopAsk` 6537, stamps 7363 | browser (**actions restricted**, §7) |
| `approval.ask` | chaperon line "Approval needed: %s wants to …" | text hush_agent_internal.h:499-503; posted agent_approve.c:67-69; ingress `hush_intel.c:171` | **relay only**. The browser cannot tell guest from owner (§7.1) |
| `approval.held` (with `guest`) | held-turn record | agent_approve.c:48-71, `held->guest` :66 | **NEW** relay hook. Alert actions only. It can never be paired with an approve action, because none exists (§4.3) |
| `robot.mention` | cevent `mention` | agent_dispatch.c:543; ring hush_cevent.h:20, `GET /api/chan-events` hush_http.c:311 | relay (browser would need **NEW** chan-events polling; the UI does not read it today) |
| `job.start` / `job.done` | cevent `job_start` / `job_done` | agent_dispatch.c:1381; :240, :306, :416, :633 | relay, or browser via `st.thinking` (4205, 4224) |
| `robot.stuck` | cevent `stuck` | hush_presence.c:501 | relay |
| `devlog.line` | dev-log buffer | `devLogLines.push` 7886 | browser |
| `timer` / `idle` | none | **NEW** | either |

### 4.2 Conditions

- `channel ==` / `in` (the note channel field; event payloads in render 7673)
- `robot ==` (job name in `st.thinking`; cevent `actor`, hush_cevent.h:31-39)
- `author is owner` (`isMineNote` 6527) or `is robot`
- `text starts with` / `contains`
- `thread is project-bound` (desk project; agent_team.c:331-334)
- `turn is owner-asked` (`hush_agent_owner_asked`, agent_approve.c:140): **relay only**
- `turn is follow / election` (`in.follow` / `in.elect`,
  hush_agent_internal.h:286, :296): **relay only**
- `tab hidden` (`document.hidden`): **NEW**, browser

### 4.3 Actions

| action | exists today as | where | runs |
|---|---|---|---|
| run catalog action | `QUICKBAR_ACTIONS[id].run()` | index.html:6060-6066 | browser |
| open thread | `openThreadPane(root)` | 7628 | browser |
| stop robot on thread | `stopJob` → `POST /api/cancel {root, robot}` | 6773-6776 | browser |
| post note | `POST /api/event {content, kind:1, channel, reply_to}` | 6565-6570 | browser. Sent with a **NEW** `"via":"macro"` tag. The relay never treats a tagged note as an approval, team or loop answer (§7.2, Gauge F6) |
| set theme | `POST /api/profile {theme}` | 6155 | browser |
| browser notification | none (no `Notification` use in index.html) | **NEW** | browser |
| ntfy alert | Credentials only: `hush_vault_read_ntfy` hush_vault.h:117-118, origin `https://ntfy.onerelay.app` :25. **No publisher, no caller outside tests** | **NEW** publisher | relay |
| chaperon line | `hush_agent_chaperon_say` | agent_approve.c:60-62 | relay (audit, §7.3) |

**No approve or decline action (Gauge F1).** Today a held turn is answered only
by the owner's note:

- `hush_agent_approval_answer` (agent_approve.c:90-115) → `approval_run` →
  `hush_agent_begin_approved`, which sets `approved` (agent_dispatch.c:1386-1391).
- That makes `person_gate` true (agent_prompt.c:153) and grants project cwd plus
  Cline `--auto-approve true` (agent_team.c:327-342; agent_process.c:398).

A macro action that reached this path would turn a user-editable condition into
the only guard on #322. So the macro action set has **no** `approve` or
`decline`. `/api/macros` (**NEW**) checks every action against a server-side
allow-list and rejects the whole macro on any action outside it, `approve` and
`decline` included.

### 4.4 Premades (ship disabled)

1. **"On approval request: auto-approve + ntfy alert"**, `runs: relay`.
   Per the C-1 and C-2 rulings, this card is a **toggle for a new
   `approval_mode` value**, not a free-form macro.

   - **Name.** The id is `approve_owner_asks` (**NEW**; renamed per Gauge F4 because
     the earlier name was inverted). It sits
     beside `auto_approve` / `approve_every_action` (hush_roster.h:35-37; enum
     :95-98) and is set through the same `POST /api/profile {approval_mode}`
     path (api_identity.c:131-137 → `hush_launch_set_approval`,
     hush_launch.h:193-196). The card's switch posts it. Turning the card off
     restores the previous mode, which the **relay** stores (vibe field
     `approval_mode_prev`, **NEW**), not the browser (Gauge F8).
   - **Unknown or corrupt values (Gauge F8).** An unknown or corrupt
     `approval_mode` keeps the previous mode, or else falls to the strictest
     (`approve_every_action`). It **never** becomes auto-approve.
     - Today the API path already refuses an unknown id and leaves the mode
       alone (`hush_launch_set_approval` → `HUSH_ERR_PARSE`,
       hush_launch.c:700-710).
     - But restore parses an unknown id as AUTO: `hush_launch_restore_vibe`
       (hush_launch.c:3338-3339) → `hush_roster_approval_parse` sets
       `HUSH_ROSTER_APPROVAL_AUTO` first (hush_roster.c:244-255). That fail-open
       path changes in K7.
   - **Semantics, hard-coded in `approval_needed`** (agent_approve.c:40-46,
     **NEW** branch).
     - The branch tests `is_human(parent) && !in->follow && !in->elect`. It
       does **not** call `hush_agent_owner_asked`, whose robot branch would let
       robot chains through (agent_approve.c:149-153; Gauge F11).
     - No setting, macro, condition or import can widen it.
     - A turn runs without a hold **only** when the parent note is the owner's
       own, as in the first branch of `hush_agent_owner_asked`,
       agent_approve.c:147-148.

     These turns are always held:
     - **robot-chained** turns: a robot's note in an owner-opened thread,
       agent_approve.c:149-153 (C-3)
     - **follow** turns (`in->follow`, hush_agent_internal.h:296) (C-3)
     - **election** turns (`in->elect`, :286) (C-3)
     - **guest** turns (`held->guest`, agent_approve.c:66)

     The owner answers held turns by typing Yes/No, as today.
   - **Person gate unchanged.** An owner-direct turn that is not held has
     `approved == 0` but `owner_asked == 1`, so
     `person_gate = approved || owner_asked` (agent_prompt.c:153) is the same
     value it would be under `auto_approve` today. The mode cannot grant a gate
     the owner did not.
   - **Alert half** (C-6, Gauge F2). When a **guest** turn is held, the relay
     sends an ntfy alert.
     - **Payload.** Metadata only: robot name, channel, time and held count. It
       never includes note text.
     - **Config.** The ntfy server (`https://` only) and topic are
       user-configured.
     - **Credential.** It is read only from the keystore under the pinned
       `hush/ntfy/` prefix (§5.2). The path is never user-set.
     - **Changes.** A change needs the per-action nonce (#336).
   - **Audit.** Every turn run under the mode posts a chaperon line in the
     thread, "Auto-approved (approve_owner_asks): <robot>.", and emits the
     `macro` cevent. Both record the actor class (§7.3, Gauge F5).
   - **Loop prompt.** "Continue this loop?" is never auto-answered (C-4).
2. **"Mention me → notify"**, `runs: browser`. `note.new` + text contains my
   name, with the tab hidden → browser notification (**NEW**).
3. **"Robot stuck → open thread"**, `runs: relay → browser`. `robot.stuck` →
   chaperon line. The browser opens the thread when the tab is visible.
4. **"Esc twice → stop all on this thread"**, `runs: browser`. `key` → `stopJob`
   for each live job in the open thread (6773).

---

## 5. Storage, format, import/export, reset

### 5.1 Where prefs live today

| pref | store | cite |
|---|---|---|
| quickbar slots | `localStorage["hush-quickbar"]`, 4 ids validated against `QUICKBAR_ACTIONS` | index.html:6067-6077 |
| theme | localStorage `hush-theme` (3173) **and** relay profile `theme` via `POST /api/profile` (6155) → vibe.json | hush_roster.h:105; `hush_launch_save_vibe` hush_launch.h:184-185 |
| approval mode | relay only: `POST /api/profile {approval_mode}` (6165) → `hush_launch_set_approval` (hush_launch.h:193-196) → vibe.json | api_identity.c:131-137; hush_roster.h:35-37, :107-108 |
| dev log | relay `dev_log_enabled` (6213) | api_identity.c:73-84; hush_launch.c:695 |
| thread / inventory / skill lifetime / agent pic | localStorage `hush-thread` 7614, `hush-thread-open` 5613, `hush-inv` 7927, `hush-skill-lifetime` 3125, `hush-agent-pic` 3885 | — |
| robot skills / favorites | relay (equipped `skill_0..7`; PE-4 favorites "saved per robot on the relay", index.html:943) | skills/system/forge-skill/SKILL.md "Equip and prune" |

**API auth today (Gauge F3/F7).**

- `/api` routes need the session token. It is accepted from any of four
  carriers (hush_http.c:529-553):
  - Cookie `hush_session`
  - `Authorization: Bearer`
  - `X-Hush-Token` (hush_auth.h:20)
  - `?k=` query (:549-551)
- The guard is at hush_http.c:663-695, with the denial at :691.
- The only other check is Host (:668-673). There is no Origin or `Sec-Fetch`
  check anywhere in hush_http.c.
- An unauthenticated **loopback** peer is handed the token as
  `Set-Cookie: hush_session=…` (hush_http.c:286-290, :732-738).
- The token file is `$HUSH_HOME/session.token`, 0600 (hush_auth.h:18, :22-23).

The fix is tracked in #336.

### 5.2 Proposal

- **Browser prefs.** Keys, single-key switch and browser macros go in
  `localStorage["hush-keys"]` (**NEW**). That matches the quickbar: per browser,
  no relay round trip.
- **Migration.** The first load reads `hush-quickbar` into `quickbar` and leaves
  the old key in place for one release.
- **Relay macros.** Relay-run macros go in a relay file beside vibe.json,
  `hush/macros.json` (**NEW**; vibe path resolver hush_launch.c:250, file name
  :53). It is mode 0600 and written only by the relay. Access is through
  `GET`/`POST /api/macros` (**NEW** route next to `/api/profile`,
  hush_http.c:1013).
- **ntfy config** (C-6, Gauge F2).
  - **Stored in `hush/macros.json`.**
    - The server URL, which must start with `https://`. The relay rejects any
      other scheme.
    - The topic.
    - A credential **name**, matching `[a-z0-9-]{1,32}`.
  - **Keystore path is pinned (NEW).**
    - The relay derives the keystore path `ntfy/<name>`, which is the pass entry
      `hush/ntfy/<name>` under the same `hush/` prefix scheme as
      hush_pass.h:20-23. **No user-set path is ever accepted.**
    - The existing `hush_keystore_path_ok` accepts any relative
      `[A-Za-z0-9/_-]` path (hush_keystore.c:305-325), including
      `identity/nsec` (hush_pass.h:20). So the pin is a new relay check, not
      the existing validator.
  - **Changes.**
    - Any change to server, topic or name needs the per-action UI nonce (#336).
    - Each change is logged with the old→new server.
    - Every alert carries a fresh random nonce in its body, so an alert can't
      be replayed into an old notification.
  - **Reading the secret.**
    - It is read at send time through `hush_keystore_load`
      (hush_keystore.h:59-61). Order: pass, then 1Password, then secret-tool
      (hush_keystore.c:249-253), then the AES-256-GCM `keys.vault` fallback
      (hush_keystore.c:273-281; hush_keyfile.h:1, :13). That is encrypted, not
      plaintext (Gauge F9).
    - It is never written to a hush-home file as plaintext (`hush_keystore_save`,
      hush_keystore.h:32-35).
  - **Defaults.** The hardcoded onerelay values in hush_vault.h:11-27 are
    **not** the default.
  - **Export.** It omits the ntfy server and topic unless the owner ticks
    "include alert config" (a public ntfy topic is bearer-like; §7.0).
- **Why not vibe.json.** Ruled C-7: new `hush/macros.json`. A separate file keeps a bad macro from
  corrupting the vibe.

**Format** `hush-keys` v1. One JSON shape for both stores and for export:

```json
{
  "format": "hush-keys",
  "version": 1,
  "single_key": true,
  "keys": { "inventory": ["i"], "character": ["c"], "slot-1": ["1", "Digit1"] },
  "quickbar": ["inventory", "character", "new-channel", "stop"],
  "macros": [
    { "id": "premade-mention-notify", "name": "Mention me → notify", "origin": "premade",
      "enabled": false, "runs": "browser",
      "trigger": { "type": "note.new" },
      "conditions": [ { "type": "text_contains", "value": "@me" }, { "type": "tab_hidden" } ],
      "actions": [ { "type": "browser_notification" } ] }
  ]
}
```

`approve_owner_asks` is **not** a macro in this file. It is the relay
`approval_mode`, and its premade card only toggles that setting (§4.4).

**Validation.**

- Unknown action ids are dropped, as `loadQuickbar` does (6070-6073).
- Refused keys (§2.1) are dropped.
- Never-bindable actions (§2) are dropped.
- **Relay-side:** `/api/macros` rejects any action type that is not on its
  allow-list, including `approve` and `decline` (Gauge F1). This check holds
  whatever the browser importer did.
- Limits: 64 macros, 8 actions per macro. Payloads over 64 KB are rejected (the
  same bound as `HUSH_SKILL_JSON_MAX`, hush_skill.h:23).

**Export.** `EXPORT` downloads `hush-keys.json` through `downloadBlob`
(index.html:7140). It includes relay macros fetched from `/api/macros`.

**Import.**

- `IMPORT` takes a file.
- **Every imported macro arrives `enabled: false`** and stamped `IMPORTED`.
- Key conflicts with current bindings are listed before apply.
- Nothing applies without the owner pressing `APPLY`.

**Reset.**

- `RESET KEYS` restores the §2 defaults and `QUICKBAR_DEFAULTS` (6037).
- `RESET MACROS` disables every macro and restores the premades, disabled. It
  does not delete user macros unless `DELETE MINE` is also confirmed.
- Both are journaled to LOG.

---

## 6. Agents: the Armory skill "update keyboard shortcuts and macros"

### 6.1 How system skills work today

| step | cite |
|---|---|
| A skill is a `SKILL.md` with frontmatter `name`, `description`, optional `role` (`worker`/`chaperon`/`any`) and `category`. The body is Markdown | skills/system/forge-skill/SKILL.md (File shape); parser hush_skill.c:864-875 |
| Shipped system skills live in `skills/system/<slug>/SKILL.md` (36 today). `make install` copies them to `$(HUSHSHARE)/skills/system` and seeds `~/.hush/skills/system` without overwriting | Makefile:44 (`HUSH_SKILL_PACK_SHARE`), :281-301 |
| At catalog load the relay seeds from `$HUSH_SKILL_PACK`, `../skills/system`, `skills/system`, the share dir and next to the exe | hush_skill.c:205-217; `hush_skill_seed_pack` hush_skill.h:119-120, hush_skill.c:432 |
| The catalog is served at `GET /api/skills` | hush_http.c:319 |
| The Armory groups **System** and **This robot**. Equip is in the robot drawer (`i`) | UI_SPEC.md:368-374 |
| Each equipped skill's full text is read into the robot's prompt | `hush_skill_read_instructions` hush_skill.h:127-130 → agent_prompt.c:322 |
| Ids: `system:slug`, `user:slug`, `robot:robot-slug:skill-slug`. Max 8 equipped | hush_skill.h:127-130, :17 |

### 6.2 Proposal

- **NEW** `skills/system/keys-macros/SKILL.md`:
  - `name: keys-macros`
  - `description: Update keyboard shortcuts and macros (proposal only).`
  - `role: any`
  - `category: craft`
  - Seeding is automatic (§6.1). No C change.
- **Robots are not given the token, but can get it (Gauge F3).** Hush passes no
  token to robots, and `/api` refuses requests without it (hush_http.c:691).
  Until #336 is fixed, though, a shell-capable turn can read `session.token` or
  take the loopback `Set-Cookie` (§5.1). The propose-only path below is
  therefore UX. The relay-side allow-list (§5.2) and the hard-coded mode
  exclusions (§4.4) are what hold regardless. The skill tells the robot to
  answer with exactly
  one fenced block, language `hush-keys`, holding a **partial** v1 document
  (§5.2).
- **The UI already parses fences.** `splitFences` (index.html:7123-7138) builds
  the Download/Canvas `.note-files` bar (7346-7360).
  - **NEW:** when a fence's lang is `hush-keys` and the author is a robot, add a
    `REVIEW KEYS` stamp to that bar.
  - The stamp opens the drawer with a diff: added, changed and removed rows.
  - The owner (`isHiveOwner`, 6532-6534) presses `APPLY`, or nothing changes.
- **Skill guardrails** (stated in the skill body and enforced by the importer):
  - Agent macros arrive `origin: agent`, `enabled: false`, stamped
    `AGENT DRAFT`.
  - Agent proposals may **not** contain `approve`, `decline`, `ntfy`, `post
    note` or never-bindable actions, nor change `single_key`. The importer
    strips them and lists what it stripped.
  - Robots read untrusted notes and can be prompt-injected by a guest, so a
    proposal is never trusted.
- **Users** do the same through the drawer, and can also paste a `hush-keys`
  block into `IMPORT`.

---

## 7. Security

### 7.0 Threat model (Gauge F7)

| # | adversary / asset | today (file:line at 13511bd4) | plan stance |
|---|---|---|---|
| T1 | **Same-uid agent child** (primary adversary): any shell-capable robot turn | Runs as the relay's uid with its environment; no env scrub before `execvp` (agent_process.c:65-74, :413); no sandbox code in src/ | KEYS assumes T1 until #336. K5-K9 depend on #336 |
| T2 | Session token | `$HUSH_HOME/session.token` 0600 (hush_auth.h:18, :22-23); handed to any unauthenticated loopback peer as `Set-Cookie` (hush_http.c:286-290, :732-738) | No KEYS assurance relies on the token alone. Security-relevant writes need the per-action nonce (#336) |
| T3 | `HUSH_KEY_PASS` + `keys.vault` | The passphrase is read from the environment on each use (hush_keyfile.c:280; env name hush_keyfile.h:11). `keys.vault` is in `$HUSH_HOME` (hush_keyfile.h:13). A child inherits the env | Fixed by the #336 env scrub. ntfy creds never use a user-set path (§5.2) |
| T4 | pass / 1Password / secret-tool stores | Reachable by the same uid wherever their agents are unlocked (hush_keystore.h:1, :59-61) | The ntfy credential is pinned to `hush/ntfy/*` so the relay can't be made a deputy for `identity/` or `agents/` (Gauge F2) |
| T5 | `?k=` token carrier | Accepted (hush_http.c:549-551). URLs leak into history, logs and Referer | The KEYS UI never builds `?k=` URLs. Removing the carrier is a #336 item |
| T6 | Cross-site requests | No Origin / `Sec-Fetch` check in hush_http.c; Host only (:668-673) | `/api/macros` and `approval_mode` writes need an Origin check (#336) |
| T7 | Export file | Would carry the ntfy server and topic, which is bearer-like on public ntfy | Omitted by default (§5.2) |
| T8 | `macros.log` contents and integrity | **NEW** file. The same uid can edit 0600 | Metadata only, no note text. Actor class per line. An optional hash chain is tamper-*evident* only after #336 |
| T9 | Other local users | 0600 files (hush_auth.h:22-23) stop other uids. But the loopback `Set-Cookie` (T2) reaches **any** local uid | Stated. Closed by #336 |
| T10 | Guests (other humans) | Guest turns get no person gate (agent_prompt.c:153). Guest ingress is latent today | `approve_owner_asks` never auto-runs a guest turn (§4.4). No approve action exists (§4.3) |

### 7.1 What #322 / #323 gate (exact)

- **#322** (issue, open; PR #323 says "Refs #322" so it did not auto-close):
  - Title: "Cline project turns get `--auto-approve true` (incl. shell) without
    a person gate under default Auto-approve".
  - Expected: the project binding and `--auto-approve true` apply only after a
    person gate, meaning the owner asked or the owner answered Yes.
  - Interim mitigation: set "Approve every action".
- **#323** (merged 2026-10-17 09:28 EDT, 13511bd4) implements it:
  - `job->person_gate = in->approved || hush_agent_owner_asked(in);`
    (agent_prompt.c:153). The field is at
    hush_agent_internal.h:159-164.
  - `hush_agent_apply_project_cwd` returns without binding unless
    `person_gate`. Only then does it bind the desk project's path as cwd and set
    `project_tools = 1` (agent_team.c:327-342).
  - `hush_agent_cline_approve(project_tools)` returns the auto-approve "true"
    argument only when `project_tools` (agent_process.c:368-377); it is
    argv[5] (:398).
  - `in->approved = 1` is set by `hush_agent_begin_approved`
    (agent_dispatch.c:1386-1391). That function is reached only from
    `approval_run` after an owner "Yes" (agent_approve.c:250).
  - `hush_agent_owner_asked` (agent_approve.c:140) is true when the
    parent note is the owner's, or a robot's note in a thread the owner opened.
- **So the gate guards three things:** project cwd, project tools, and Cline
  auto-approve (including shell). They are granted only when the owner asked for
  the turn or the owner said Yes.

### 7.2 Why a browser auto-approve macro is refused

- `hush_agent_approval_answer` checks only that the answering pubkey is the hive
  owner (agent_approve.c:97-99). It then takes the oldest held turn in that
  thread (:102) and runs it on "Yes" (:110-111).
- A browser macro posting "Yes" through `POST /api/event` (the
  `postLoopAnswer` pattern, 6565-6570) is signed as the owner.
- The ask line is identical for owner and guest turns: the same `ASK_FMT` /
  `ELECT_FMT`, hush_agent_internal.h:499-503, agent_approve.c:67-69. The `guest`
  bit lives only in relay memory (agent_approve.c:54, :66).
- So a browser macro would approve guest turns too. Each would get `approved=1`,
  then `person_gate=1`, then shell. **That launders the #322 gate.**
- **Rule (C-1, C-2, C-3; Gauge F1).** There is no `approve` / `decline` action
  in the browser **or** on the relay. Auto-approve is only the relay
  `approval_mode` `approve_owner_asks` (§4.4), with hard-coded exclusions. It
  skips the hold only for owner-direct turns (agent_approve.c:147-148).
  For those turns `person_gate` is already true, so the mode never grants what
  the gate would refuse. Guest, robot-chained, follow and election turns stay
  held.
- **Rule (C-4).** No macro, mode or action may answer "Continue this loop?".
- **Relay-side guard (Gauge F6), NEW.** A browser check alone is heuristic. It
  would miss an ask that is not the last note, and only the relay knows held
  state. Today every stored owner note is routed to the answer hooks:
  - `hush_intel_consider` (api_channels.c:75)
  - then `hush_agent_approval_answer`, `hush_agent_team_answer` and
    `hush_agent_loop_answer` (hush_intel.c:171-176)

  Changes:
  1. Macro-posted notes carry `"via":"macro"`. The relay stores the tag and
     never routes a tagged note to those three hooks.
  2. Answers come only from the inline approval stamps. Each sends the hold id
     plus a per-action nonce (#336) that the session token alone can't mint.
     The relay refuses a Yes/No whose nonce is missing or stale.
  3. Until #336 lands, a token holder can drop the tag. So (1) is defence in
     depth, and (2) is the guard. K5's done-when is a **C test** of (1) and (2),
     not only JS.
- **Browser check (kept, UX).** The browser `post note` action still refuses
  content that `hush_agent_loop_parse_answer` (used at agent_approve.c:105) would
  read as Yes/No in a thread with a pending ask or loop prompt.

### 7.3 Opt-in, visible, auditable

- **Opt-in.**
  - Every premade ships `enabled:false`.
  - The `approval_mode` default stays `auto_approve` (hush_roster.h:95-98).
  - Turning on the auto-approve card needs a typed confirmation in the drawer.
  - Imports and agent drafts can't set `approval_mode`, because §2 lists the
    approval radios as never-bindable.
  - **These are UX, not security boundaries (Gauge F3b).** `/api/profile`
    accepts `approval_mode` from any token holder (api_identity.c:131-137). The
    boundary is the per-action nonce + Origin check (#336), required for
    `approval_mode` writes and for enabling relay macros.
- **Visible.**
  - While `approve_owner_asks` is on, a persistent `AUTO-APPROVE` stamp sits in
    the header stamp row (`header .stamp-row` index.html:658).
  - Settings shows it next to the approval radios (2390-2395).
- **Auditable.**
  - Every relay firing posts a chaperon line in the thread, the same channel as
    the ask (`hush_agent_approval_say`, agent_approve.c:67).
  - It also emits a cevent, type `macro` (**NEW** type beside
    hush_cevent.h:20-29), readable at `GET /api/chan-events` (hush_http.c:311).
    The ring holds 64 events (hush_cevent.h:11), so LOG also appends to
    `hush/macros.log` (**NEW**, 0600).
  - Enable, disable, edit, import, reset and mode changes are logged with the
    time and the **actor class** (Gauge F5):
    - `browser-nonce`: a request that carried a valid per-action nonce (#336)
    - `token-only`: the bare session token, which may be a T1 child
    - `relay`: a relay-internal firing

    Every token write is stamped as the owner today (api_channels.c:102;
    api_identity.c:131-137 records no actor). The actor class is therefore the
    only way to tell the browser from a child. The log is not tamper-evident
    while the same uid can edit it (§7.0 T8).

### 7.4 Conflicts for CoS

#### Rulings (CoS 2026-10-17 09:46)

Plan status: **APPROVED** with these rulings and the 10:10 ruling below. K1 is GO
after #325 merges. Gauge's FAIL at 59b48543 (10:16) is addressed in this
revision, and re-review is pending.

| # | ruling | where folded in |
|---|---|---|
| C-1 | Relay rule. Auto-approve owner-asked turns only. Guest turns are held and alerted | §0, §4.4, §7.2 |
| C-2 | New `approval_mode` value. The premade macro card toggles it | §4.4 (`approve_owner_asks`, renamed per Gauge F4), §7.3, K7 |
| C-3 | Exclude robot-chained, follow and election turns by default | §4.4, §7.2, K7 test |
| C-4 | Never auto-answer "Continue this loop?" | §4.4, §7.2 |
| C-5 | Gauge check ordered. K6 is blocked until it answers. **Answered** YES/P1 and filed as #336. Per CoS 10:16, K5-K9 depend on #336 | §7.0, K5-K9 |
| C-6 | ntfy server and topic are user-configurable. Credentials only via pass, 1Password or keyring, never plaintext. Default payload is metadata only, no note text. Per Gauge F2: https only, keystore pinned to `hush/ntfy/*`, nonce | §4.4, §5.2, K8 |
| C-7 | New `hush/macros.json` | §5.2, K6 |
| C-8 | Off switch + remap. No single-letter keys while typing. Ctrl/Cmd+S is OK | §2, §2.1, K1 |
| C-9 | Floor issue to persist held turns. Not a KEYS blocker. Filed as **#337** (Floor) | K-F (outside KEYS) |
| C-10 | Resolved: rebased onto main 13511bd4 before push | header |

#### Ruling (CoS 2026-10-17 10:10)

| topic | ruling | where folded in |
|---|---|---|
| GUARD Always-approve vs robot chains | By default, Always-approve (GUARD) rules do **not** cover robot-chained turns (matches C-3). The owner may opt in per rule later, after Gauge review | §3 GUARD note |
| C-9 | Filed as #337 (Floor) | K-F |

#### Original conflicts (as raised)

| # | conflict | recommendation |
|---|---|---|
| C-1 | The order's premade "on approval request: auto-approve" cannot be a browser macro without bypassing #322 (§7.2) | Relay rule, owner-asked only. Guests get an alert, never an approve |
| C-2 | The relay rule is effectively a third approval mode: approve owner asks, hold guest asks. Should it be `approval_mode = approve_guests` in `/api/profile` (api_identity.c:131-137) instead of a macro? | Ship as a macro card for visibility, store as a relay setting. CoS picks the surface |
| C-3 | `owner_asked` counts robot notes in an owner-opened thread (agent_approve.c:136-154; #331 P3-4). Auto-approve would cover follow waves, loop turns and the plan pass, so robots could chain unattended | Default conditions `follow == false`, `elect == false`. Revisit with #331 P3-4 before any guest ingress work |
| C-4 | Auto-"Yes" to the loop prompt "Continue this loop?" (#280/#284, `isLoopAsk` 6537) defeats the human-approved loop | Never allowed as a macro action |
| C-5 | A person-gated Cline turn has a shell running as the relay's user. The token file is `$HUSH_HOME/session.token` 0600 (hush_auth.h:18-23). Phase 1 did **not** verify that the Cline cwd stops a robot reading it and calling `/api/macros` or `/api/profile` | Gauge to verify before K5. Consider requiring a fresh owner confirm (browser-side) for enabling approval macros |
| C-6 | ntfy: no publisher exists. Credentials are hardcoded to the onerelay vault and `ntfy.onerelay.app` / user `eggdrop` (hush_vault.h:11-27), which suits the operator's own hive, not a general FOSS default. Alert text could leak note content | User-configured ntfy URL+topic (**NEW**), off by default. Payload = robot + channel only. CoS decides whether hush_vault creds may be reused |
| C-7 | Relay macro storage: vibe.json vs new `hush/macros.json` | New file (§5.2) |
| C-8 | Single-letter defaults clash with screen-reader quick keys; Ctrl/Cmd+S in canvas overrides browser Save | Ship the off switch in K1. Canvas Ctrl/Cmd+S opt-in only |
| C-9 | Held turns are lost on relay restart (agent_approve.c:1-6), so a macro never sees them again | LOG notes "relay restarted" from the session badge. No replay |
| C-10 | Base was 900a486d while the gate code was on 13511bd4 | Resolved: rebased onto 13511bd4 |

---

## 8. Milestones

Each slice is shippable alone. Owners: **JNY** UI/journeys · **Floor**
store/reliability · **Gauge** C review/security · **Ops** visual · **CoS** merge.

| M | slice | owner | review | depends | done when |
|---|---|---|---|---|---|
| K0 | CoS rulings on C-1..C-10 | CoS | — | this plan | **DONE** 2026-10-17 09:46 (§7.4) |
| K1 | One key registry + dispatcher + shared typing guard (+`isComposing`, `repeat`); `i c 1-4 Esc` behaviour-identical; single-key off switch in Settings; `ev.code` digits | JNY | Gauge (JS review), Ops | **#325 merged** (GO after) | existing journeys unchanged; UI_SPEC delta; palette gates green |
| K2 | New default keys (§2) + `aria-keyshortcuts`/titles; `[`/`]` channel cycle; verify desktop-window key delivery | JNY | Ops | K1 | each key wired to the cited action; screen-reader note in UI_SPEC |
| K3 | `#keys-drawer` KEYS + LOG tabs: rebind (remap), refuse/swap, reset keys; canvas Ctrl/Cmd+S; stamp family; added to `check_palette.sh` surfaces | JNY (UI), Ops (visual) | Gauge | K1 | C1-C4/B1 pass at 1440 and 390 |
| K4 | `hush-keys` v1 store: localStorage, migration from `hush-quickbar`, validate, import (disabled), export, reset | Floor | Gauge | K1 | round-trip test; corrupt input falls back to defaults like `loadQuickbar` |
| K5 | Browser macros: MACROS tab, key/note/devlog/timer triggers, UI actions, `"via":"macro"` tag + relay answer guard (§7.2); premades 2 and 4 | JNY + Floor | Gauge (security) | K3, K4, **#336** | **C test**: a tagged note and an un-nonced Yes never answer a held, team or loop ask; JS refusal test kept |
| K6 | Relay macro store `hush/macros.json` + `/api/macros` (server-side action allow-list, no `approve`/`decline`) + cevent `macro` + `macros.log` with actor class | Floor | Gauge (C review) | **BLOCKED on #336** | unit tests; 0600; allow-list rejects `approve`/`decline`; nonce + Origin required for enable |
| K7 | New `approval_mode` `approve_owner_asks`: parse/id (hush_roster.h:35-37), hard-coded `approval_needed` branch on `is_human(parent) && !follow && !elect` (agent_approve.c:40-46; not `owner_asked`), relay-stored previous mode, unknown/corrupt → previous or strictest (never AUTO; fixes hush_launch.c:3338-3339 / hush_roster.c:244-255 fail-open), premade card toggle via `/api/profile` with nonce, chaperon line with actor class, `AUTO-APPROVE` header stamp | Floor (C), JNY (card + stamp) | **Gauge (security)** | K0 (C-1..C-3), K3, **#336** | tests: guest, robot-chained, follow and election turns are always held under the mode and never get `project_tools` without an owner Yes; unknown/corrupt restore never yields AUTO; loop prompt never answered |
| K8 | ntfy publisher + guest-held alert (premade 1, alert half): https-only server + topic in `hush/macros.json`, credential name → pinned keystore path `ntfy/<name>` (`hush/ntfy/*`), per-alert nonce, nonce-gated config changes | Floor | Gauge (security) | K6, K7, **#336** | off by default; payload metadata only (no note text); rejects non-https and any non-`ntfy/` path; no plaintext secret on disk; fake-curl test like tests/test_vault |
| K9 | `skills/system/keys-macros/SKILL.md` + `REVIEW KEYS` stamp + diff/apply; agent-draft stripping | JNY (UI), Floor (importer) | Gauge | K4, K5, **#336** | test: an agent block with `approve` is stripped and lands disabled; `/api/macros` rejects it server-side too |
| K10 | Merge each slice (PR per M, gb/* branch per AGENTS.md) | CoS | — | each | — |
| K-F | (outside KEYS, C-9) **#337**: persist held turns across relay restart (today they are lost: agent_approve.c:1-6) | Floor | Gauge | — | not a KEYS blocker |

---

## Appendix A: control audit (W1 probe × 13511bd4)

The probe is the turn-1 W1 probe (180 controls). Columns:

- **Markup:** the line at 13511bd4 for `#id` controls; `built` means the line
  where JS creates the control.
- **click/handler:** the first `$("id").addEventListener` at 13511bd4 (index.html is unchanged from 900a486d). `-` means
  delegated or built inline.
- **Fields** are never global keys.

| control | kind | markup | click/handler | default binding |
|---|---|---|---|---|
| `#nav-toggle` | action | 2204 | 4258 | `b` (NEW key) |
| `#rail-toggle` | action | 2205 | 9190 | `k` (NEW key) |
| `#begin` | action | 3338 | - | never bound (destructive, identity, or approval) |
| `#qb-1` | action | 2350 | - | `1` (existing) |
| `#qb-2` | action | 2351 | - | `2` (existing) |
| `#qb-3` | action | 2352 | - | `3` (existing) |
| `#qb-4` | action | 2353 | - | `4` (existing) |
| `#qb-config` | action | 2354 | 6127 | menu-bindable, unbound by default |
| `#create-id` | action | 3433 | - | never bound (destructive, identity, or approval) |
| `#use-id` | action | 3434 | - | never bound (destructive, identity, or approval) |
| `#show-help` | action | 3471 | - | never bound (destructive, identity, or approval) |
| `#save-pass` | field | 3485 | - | field (typing guard; never a global key) |
| `#reveal-key` | action | 3395 | - | never bound (destructive, identity, or approval) |
| `#copy-key` | action | 3396 | - | never bound (destructive, identity, or approval) |
| `#ack-key` | action | 3397 | - | never bound (destructive, identity, or approval) |
| `#vibe-name` | field | 3504 | - | field (typing guard; never a global key) |
| `#vibe-about` | field | 3505 | - | field (typing guard; never a global key) |
| `input[name=vibe-vis][type=radio]` | field | dynamic | - | field (typing guard; never a global key) |
| `#do-vibe` | action | 3413 | - | never bound (destructive, identity, or approval) |
| `#meet-payne` | action | 3425 | - | never bound (destructive, identity, or approval) |
| `button.thread-btn` | action | 7748, 7768 (built) | - | menu-bindable, unbound by default |
| `button.think-stop` | action | 6848 (built) | 6851 | `x` (NEW key) |
| `.note-files > button "Download"` | action | dynamic | - | menu-bindable, unbound by default |
| `.note-files > button "Canvas"` | action | dynamic | - | menu-bindable, unbound by default |
| `#milestone-name` | field | 2301 | - | field (typing guard; never a global key) |
| `#milestone-category` | field | 2302 | - | field (typing guard; never a global key) |
| `button.btn` | action | dynamic | - | menu-bindable, unbound by default |
| `#msg` | field | 2309 | 5474 | field (typing guard; never a global key) |
| `#send` | action | 2321 | - | menu-bindable, unbound by default |
| `#nav-inventory` | action | 2249 | 9352 | `i` (existing) |
| `#nav-character` | action | 2251 | 9356 | `c` (existing) |
| `#thread-close` | action | 2431 | 5592 | Esc (existing) |
| `#thread-desk-name` | field | 2435 | - | field (typing guard; never a global key) |
| `#thread-desk-category` | field | 2436 | - | field (typing guard; never a global key) |
| `#thread-desk-project` | field | 2437 | - | field (typing guard; never a global key) |
| `#thread-desk-save` | action | 2438 | - | menu-bindable, unbound by default |
| `#thread-desk-archive` | action | 2439 | 5629 | menu-bindable, unbound by default |
| `#thread-msg` | field | 2447 | 9218 | field (typing guard; never a global key) |
| `#thread-send` | action | 2449 | - | menu-bindable, unbound by default |
| `#canvas-close` | action | 2458 | 7248 | Esc in its layer (existing chain) |
| `#canvas-download` | action | 2462 | 7290 | menu-bindable, unbound by default |
| `#canvas-save` | action | 2463 | 7294 | menu-bindable, unbound by default |
| `#code-canvas-edit` | field | 2468 | 7255 | field (typing guard; never a global key) |
| `#qb-sel-0` | field | 2357 | - | field (typing guard; never a global key) |
| `#qb-sel-1` | field | 2358 | - | field (typing guard; never a global key) |
| `#qb-sel-2` | field | 2359 | - | field (typing guard; never a global key) |
| `#qb-sel-3` | field | 2360 | - | field (typing guard; never a global key) |
| `#install` | action | 2225 | - | never bound (destructive, identity, or approval) |
| `#profile-btn` | action | 2226 | 5901 | `p` (NEW key) |
| `#settings-btn` | action | 2227 | 5819 | `,` (NEW key) |
| `#call-btn` | action | 2228 | 6172 | unbound, bindable |
| `#invite-human` | action | 2229 | 5228 | menu-bindable, unbound by default |
| `#add-chan` | action | 2230 | 4398 | `n` (NEW key) |
| `#providers-btn` | action | 2231 | 5074 | `h` (NEW key) |
| `#raise-agent` | action | 2232 | 5043 | `r` (NEW key) |
| `#add-proj` | action | 2233 | 5336 | menu-bindable, unbound by default |
| `#new-proj` | field | 2234 | 5350 | field (typing guard; never a global key) |
| `#rail-min` | action | 2235 | 5943 | never bound (destructive, identity, or approval) |
| `#rail-max` | action | 2236 | 5946 | never bound (destructive, identity, or approval) |
| `#hive-close` | action | 2237 | 5928 | never bound (destructive, identity, or approval) |
| `#hive-exit` | action | 2239 | 5929 | never bound (destructive, identity, or approval) |
| `span.slider` | field | dynamic | - | field (typing guard; never a global key) |
| `#turn-host` | field | 2376 | 6181 | field (typing guard; never a global key) |
| `#vibe-rotate` | action | 2383 | 6182 | never bound (destructive, identity, or approval) |
| `#approval-auto` | field | 2392 | - | field (typing guard; never a global key) |
| `#approval-every` | field | 2393 | - | field (typing guard; never a global key) |
| `input[name=theme][type=radio]` | field | dynamic | - | field (typing guard; never a global key) |
| `#settings-close` | action | 2410 | 5820 | Esc in its layer (existing chain) |
| `#prof-first` | field | 2520 | - | field (typing guard; never a global key) |
| `#prof-last` | field | 2522 | - | field (typing guard; never a global key) |
| `#prof-email` | field | 2524 | - | field (typing guard; never a global key) |
| `#prof-org` | field | 2526 | - | field (typing guard; never a global key) |
| `#profile-save` | action | 2544 | 5909 | menu-bindable, unbound by default |
| `#profile-logout` | action | 2545 | 6141 | never bound (destructive, identity, or approval) |
| `#profile-copy` | action | 2546 | 5905 | never bound (destructive, identity, or approval) |
| `#profile-close` | action | 2547 | 5904 | Esc in its layer (existing chain) |
| `#new-chan` | field | 2482 | 4425 | field (typing guard; never a global key) |
| `#new-chan-prompt` | field | 2484 | - | field (typing guard; never a global key) |
| `#new-chan-save` | action | 2488 | 4404 | menu-bindable, unbound by default |
| `#new-chan-close` | action | 2489 | 4403 | Esc in its layer (existing chain) |
| `#manage-invite` | field | 2818 | - | field (typing guard; never a global key) |
| `#manage-invite-add` | action | 2819 | 9137 | menu-bindable, unbound by default |
| `input[name=manage-kind][type=radio]` | field | dynamic | - | field (typing guard; never a global key) |
| `input[name=manage-reply][type=radio]` | field | dynamic | - | field (typing guard; never a global key) |
| `#manage-topic` | field | 2841 | - | field (typing guard; never a global key) |
| `#manage-topic-add` | action | 2842 | 9144 | menu-bindable, unbound by default |
| `#manage-prompt` | field | 2846 | - | field (typing guard; never a global key) |
| `#manage-policy-more > summary` | action | 2848 | - | menu-bindable, unbound by default |
| `#manage-save` | action | 2883 | 9151 | menu-bindable, unbound by default |
| `#manage-close` | action | 2884 | 9136 | Esc in its layer (existing chain) |
| `#member-npub` | field | 2798 | - | field (typing guard; never a global key) |
| `#member-name` | field | 2800 | - | field (typing guard; never a global key) |
| `#member-save` | action | 2804 | 5236 | menu-bindable, unbound by default |
| `#member-close` | action | 2805 | 5235 | Esc in its layer (existing chain) |
| `#providers-hub-close` | action | 2714 | 5075 | Esc in its layer (existing chain) |
| `#provider-use-home` | field | 2723 | - | field (typing guard; never a global key) |
| `#provider-key` | field | 2736 | - | field (typing guard; never a global key) |
| `#provider-key-add` | action | 2737 | - | menu-bindable, unbound by default |
| `#provider-username` | field | 2743 | - | field (typing guard; never a global key) |
| `#provider-username-add` | action | 2744 | - | menu-bindable, unbound by default |
| `#provider-password` | field | 2750 | - | field (typing guard; never a global key) |
| `#provider-password-add` | action | 2751 | - | menu-bindable, unbound by default |
| `#provider-token` | field | 2757 | - | field (typing guard; never a global key) |
| `#provider-token-add` | action | 2758 | - | menu-bindable, unbound by default |
| `#provider-passkey` | field | 2764 | - | field (typing guard; never a global key) |
| `#provider-passkey-add` | action | 2765 | - | menu-bindable, unbound by default |
| `#provider-host` | field | 2771 | - | field (typing guard; never a global key) |
| `#provider-host-add` | action | 2772 | - | menu-bindable, unbound by default |
| `#provider-model` | field | 2777 | - | field (typing guard; never a global key) |
| `#provider-model-add` | action | 2778 | - | menu-bindable, unbound by default |
| `#provider-scan` | action | 2783 | 5161 | menu-bindable, unbound by default |
| `#provider-save` | action | 2788 | 5124 | menu-bindable, unbound by default |
| `#provider-close` | action | 2789 | 5076 | Esc in its layer (existing chain) |
| `#relay-close` | action | 2497 | 5721 | Esc in its layer (existing chain) |
| `#leave-exit` | action | 2508 | 5957 | never bound (destructive, identity, or approval) |
| `#leave-close` | action | 2509 | 5950 | never bound (destructive, identity, or approval) |
| `#leave-cancel` | action | 2510 | 5949 | Esc in its layer (existing chain) |
| `#dev-log-close` | action | 2932 | 6218 | Esc in its layer (existing chain) |
| `#dev-log-clear` | action | 2937 | 6220 | menu-bindable, unbound by default |
| `#dev-log-close2` | action | 2938 | 6219 | Esc in its layer (existing chain) |
| `#seed-close` | action | 2957 | 9387 | Esc in its layer (existing chain) |
| `#seed-actions` | field | 2962 | - | field (typing guard; never a global key) |
| `#seed-skills` | field | 2964 | - | field (typing guard; never a global key) |
| `#seed-project` | field | 2966 | - | field (typing guard; never a global key) |
| `#seed-channel-existing` | field | 2969 | - | field (typing guard; never a global key) |
| `#seed-channel-new` | field | 2970 | - | field (typing guard; never a global key) |
| `#seed-channel-slug` | field | 2973 | - | field (typing guard; never a global key) |
| `#seed-channel-name` | field | 2975 | - | field (typing guard; never a global key) |
| `#seed-provider` | field | 2977 | - | field (typing guard; never a global key) |
| `#seed-preview` | field | 2979 | - | field (typing guard; never a global key) |
| `#seed-submit` | action | 2981 | 9389 | menu-bindable, unbound by default |
| `#seed-cancel` | action | 2982 | 9388 | Esc in its layer (existing chain) |
| `#skill-sheet-close` | action | 2696 | 9312 | Esc in its layer (existing chain) |
| `#skill-flip-robot` | action | 2700 | 9313 | menu-bindable, unbound by default |
| `#skill-flip-chaperon` | action | 2701 | 9314 | menu-bindable, unbound by default |
| `#forge-close` | action | 2901 | 9316 | Esc in its layer (existing chain) |
| `#forge-name` | field | 2907 | - | field (typing guard; never a global key) |
| `#forge-summary` | field | 2911 | - | field (typing guard; never a global key) |
| `#forge-body` | field | 2915 | - | field (typing guard; never a global key) |
| `input[name=forge-scope][type=radio]` | field | dynamic | - | field (typing guard; never a global key) |
| `#forge-save` | action | 2923 | 9318 | menu-bindable, unbound by default |
| `#forge-close2` | action | 2924 | 9317 | Esc in its layer (existing chain) |
| `#inv-expand-edit` | action | 2946 | 9363 | menu-bindable, unbound by default |
| `#inv-expand-close` | action | 2947 | 9360 | Esc in its layer (existing chain) |
| `#agent-name` | field | 2560 | - | field (typing guard; never a global key) |
| `#agent-prompt` | field | 2568 | - | field (typing guard; never a global key) |
| `#agent-file-add` | action | 2576 | 5190 | menu-bindable, unbound by default |
| `#agent-file-del` | action | 2577 | 5219 | menu-bindable, unbound by default |
| `.pill > button "▲"` | action | dynamic | - | menu-bindable, unbound by default |
| `.pill > button "▼"` | action | dynamic | - | menu-bindable, unbound by default |
| `.pill > button "−"` | action | dynamic | - | menu-bindable, unbound by default |
| `input.agent-provider-cb[type=checkbox]` | field | dynamic | - | field (typing guard; never a global key) |
| `#agent-pic-enlarge` | action | 2602 | 5048 | menu-bindable, unbound by default |
| `button.agent-pic-sheet` | action | dynamic | - | menu-bindable, unbound by default |
| `button.agent-pic-cell` | picture tile | dynamic | - | menu-bindable, unbound by default |
| `#agent-intro` | field | 2616 | - | field (typing guard; never a global key) |
| `#skill-cycle-prev` | action | 2635 | 9299 | menu-bindable, unbound by default |
| `#skill-cycle-next` | action | 2637 | 9303 | menu-bindable, unbound by default |
| `#skill-forge-open` | action | 2652 | 9292 | menu-bindable, unbound by default |
| `#skill-enlarge` | action | 2653 | 9296 | menu-bindable, unbound by default |
| `#fav-name` | field | 2659 | - | field (typing guard; never a global key) |
| `#fav-save` | action | 2660 | 9297 | menu-bindable, unbound by default |
| `#fav-unload` | action | 2661 | 9298 | menu-bindable, unbound by default |
| `#agent-pass` | field | 2667 | - | field (typing guard; never a global key) |
| `#agent-pass-howto > summary` | action | 2669 | - | menu-bindable, unbound by default |
| `#agent-save` | action | 2673 | 5245 | menu-bindable, unbound by default |
| `#agent-close` | action | 2674 | 5044 | Esc in its layer (existing chain) |
| `#agent-clone` | action | 2675 | 5053 | menu-bindable, unbound by default |
| `#agent-delete` | action | 2676 | 5320 | never bound (destructive, identity, or approval) |
| `#avatar-close` | action | 2684 | 5052 | Esc in its layer (existing chain) |
| `button.avatar-cell` | picture tile | dynamic | - | menu-bindable, unbound by default |
| `#nav-new-channel` | action | 2255 | 4398 | `n` (NEW key) |
| `button.chan` | list row | 3568 (built) | - | `[` / `]` prev/next (NEW key) |
| `button.chan-options` | action | 3599 (built) | - | menu-bindable, unbound by default |
| `#inv-expand` | action | 2264 | 9345 | menu-bindable, unbound by default |
| `#inv-seed` | action | 2267 | 9289 | menu-bindable, unbound by default |
| `#inv-clear` | action | 2268 | 9290 | never bound (destructive, identity, or approval) |
| `#inv-raise` | action | 2269 | 9291 | menu-bindable, unbound by default |
| `button.fo-person` | list row | dynamic | - | menu-bindable, unbound by default |
| `#stats` | action | 2281 | 5717 | menu-bindable, unbound by default |

## Appendix B: Gauge #334 @59b48543 claim trace

Source: Gauge's review of #334 at 59b48543 (FAIL, 10:16 ET), and CoS ORDER
10:16. Line numbers are lines of this file at this revision.

| finding / order item | doc section : line | fix |
|---|---|---|
| F1 P1 relay `approve` macro (order 1) | §0:37; §4.3:379; §5.2 validation:566; §7.2; K6 | Removed `approve`/`decline` actions. The mode is the only auto-approve, with exclusions hard-coded. `/api/macros` allow-list rejects them server-side |
| F2 P1 ntfy key-exfil deputy (order 2) | §4.4 alert half; §5.2:504; §7.0 T4; K8 | https only; credential name → pinned `ntfy/<name>` (`hush/ntfy/*`); path never user-set; nonce-gated changes; per-alert nonce; old→new logged |
| F3 P2 "robots get no token" (order 4) | §0:44; §5.1:475; §6.2:612 | Corrected with cites: agent_process.c:65-74, :413; hush_auth.h:18, :22-23; hush_http.c:286-290, :529-553, :732-738 |
| F3b P2 UX shown as security (order 4) | §2 never-bindable; §7.3:732 | Marked UX. Boundary = nonce + Origin (#336); api_identity.c:131-137 |
| F4 P2 inverted name (order 5) | §0; §3; §4.4:399; §7.2; §7.3; §7.4; K7 | `approve_guest_turns` → `approve_owner_asks` everywhere (doc and PR body) |
| F5 P2 audit "who" (order 6) | §4.4 audit; §7.3:748; K6; K7 | Actor class `browser-nonce` / `token-only` / `relay` on every log line and cevent; not tamper-evident until #336 |
| F6 P2 client-only Yes/No refusal (order 7) | §4.3 post note; §7.2:704; K5 | Relay guard: `"via":"macro"` notes never reach hush_intel.c:171-176 answer hooks; answers need hold id + nonce; C test |
| F7 P2 no threat model (order 8) | §7.0:642 | T1-T10 covering Gauge (a)-(h) plus guests |
| F8 P3 fail-open restore (order 9) | §4.4:407; K7 | Relay-stored previous mode; unknown/corrupt → previous or `approve_every_action`, never AUTO; cites hush_launch.c:700-710, :3338-3339; hush_roster.c:244-255 |
| F9 P3 store list (order 11) | §5.2:526 | Added the keys.vault AES-GCM fallback (hush_keystore.c:249-253, :273-281) |
| F10 P3 oldest-hold Yes (order 11) | §3:314; §7.2 | Stamps bind to hold id + nonce (agent_approve.c:102) |
| F11 P3 K7 branch (order 11) | §4.4:420; K7 | Branch on `is_human(parent) && !follow && !elect`, not `owner_asked` (agent_approve.c:149-153) |
| F12 P3 GUARD open question / 10:10 missing (order 10) | §3:311; §7.4:779 | CoS 10:10 ruling added: Always rules exclude robot chains by default, per-rule opt-in later after Gauge review |
| F13 P3 drift (order 11) | header:11; §1.4:138 | 95 → 93 on main (94 with this file); `ev.key` line 6024 → 6025 |
| Order 3: K5-K9 depend on #336 | §0:67; K5:815 … K9:819 | `#336` added to K5, K6 (blocked), K7, K8, K9 depends |
| Order 10: C-9 = #337 | §7.4:776; K-F | C-9 row and K-F cite #337 (Floor) |
| Order 12: Floor co-design (#335) | §3:318 | Inline pending card, GUARDRAILS tab badge, exact match at robot scope, `hush/guardrails.json`, `approve\|reject\|ask` |
