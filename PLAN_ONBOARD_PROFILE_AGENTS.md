# PLAN: Proper Onboarding + Profile + Vibe Members + Payne Agent Creation (RDAP)

Branch: `gb/onboard-profile-agents`
Worktree: `worktrees/onboard-profile-agents`
Base: main `034cfd3d5` (PR #19 splash/onboard)

## 1. Select Planning Methodology

RDAP — Double Diamond + Spiral risk iterations + atomic Milestones with
strict Definition of Done + commit after every Milestone on the worktree
branch. Land on `main` only via Pull Request.

## 2. Scope of Work

### Primary Goal

If the session has no user or no vibe, start onboarding:

1. Identity key (create or import) with unix `pass` save **checked by default**.
2. First vibe (this relay), public or private.
3. Introduce **Sgt Major Payne**.

Splash uses the current feather logo (`/icon-192.png` / `assets/hush_feather.png`
lineage). Nothing fancy.

After ready:

- **Profile:** avatar (Nostr-consistent `picture` URL), theme
  `{dark, light, color-blind, dracula, desert, monochrome, christmas}`,
  first name, last name, email, organization, Logout.
- **Vibe:** public or private; invite / add humans; add agents.
- **Agent creation (Buzz sequence, Hush size):** Payne walks the human
  through it. Payne also has an **agent-creation skill** so Goose/Payne
  can create agents with skills on the fly. Fields: avatar, name,
  system prompt, context files (**plaintext and Markdown only**, MIME
  checked on upload).

### Non-Goals

- Multi-process / multi-tenant vibes (one `hush-relay` = one vibe).
- Blossom / NIP-96 media hosting.
- External LLM runtime for Payne (persona + skill + API only).
- ACP / Tauri / Buzz desktop port.
- Tailwind in C.
- NIP-42 AUTH, NIP-05 hosting, email verification.
- Spawning agent OS processes from C.
- Exit/Close process design (separate worktree `gb/exit-close-design`).

### Success Criteria / Overall DoD

1. Cold session (`logged_in=false` or `has_vibe=false`) shows feather
   splash then a numbered wizard: Identity → Backup/`pass` → Vibe →
   Meet Payne. Completing it yields `ready=true` and a Payne welcome
   note in `#welcome`.
2. Restored session (`pass` + vibe) splash-detects and enters the hive.
3. Profile drawer edits first/last/email/org + theme + avatar; Logout
   is a **server** `POST /api/identity {action:logout}` (not client-only).
4. Kind 0 for the human includes `name`, `display_name`, `about`,
   `picture` when set. Email and organization stay Hush-local
   (privacy) unless the human opts to put org in `about`.
5. Hive has Add human (npub / invite token) and Create agent.
   Private vibe shows join token; public does not require it.
6. Agent create: name, system prompt, avatar, context files. Server
   rejects any file whose MIME/extension is not `text/plain` or
   Markdown. Agent nsec saved via `pass` at
   `hush/agents/<slug>/nsec` (default on). Kind 0 published for the agent.
7. `.goose/skills/agent-create/SKILL.md` exists and documents the API
   Payne/Goose uses to create agents.
8. Seven themes apply via `data-theme` + CSS variables; persist in
   `localStorage` and session `theme`.
9. `./configure && make clean && make && make test` +
   `hush-c/tests/check_launch.sh` pass.
10. Every touched `.c`/`.h` passes write-legible-c §14. Worktree + PR
    lifecycle followed.

### Constraints

- C11, `-Wall -Wextra -Werror -Wconversion -Wshadow`.
- write-legible-c: fn ≤40 lines, depth ≤2, named literals, no
  recursion/goto, checked fallible calls.
- Single binary; embed UI via `./scripts/embed-ui.sh hush-c/demo`
  from the worktree root (never `embed-ui.sh demo` from repo root).
- Recv buffer today is `HUSH_BUF_SZ = 8192`. Avatar data-URLs and
  context files will not fit unless HTTP POST buffering is raised.
- `HUSH_EVENT_MAX_CONTENT = 4096` — kind 0 JSON must stay under this.
- `HUSH_LAUNCH_JSON_MAX = 8192` — session JSON must stay under this
  or the cap must be raised with a named constant.
- Worktree only; PR only to land.

### Assumptions

- Vibe remains **this relay process**. “Create vibe” after the first
  one means rename / re-state visibility / copy invite — not a second
  server.
- Payne is still a seeded identity + persona, not an LLM.
- Existing `/api/session` shape is extended additively (no field
  removals).
- Feather logo already ships as `/icon-192.png` (and apple/512). Splash
  uses that `<img>`, not a new illustration.

### Required Environment

gcc, make, openssl, curl, git, gh, python3 (embed). `pass` optional
(tests use `tests/fake-pass.sh`).

### Top Risks + Mitigations

1. **8 KiB recv buffer** rejects avatar/context POSTs.
   → M2.1 lock a 64 KiB HTTP body cap; raise `HUSH_BUF_SZ` for HTTP
   clients or split a dedicated upload reader. Verify with a 20 KiB POST.
2. **`hush_launch.c` is already 709 lines.**
   → New `hush_roster` module owns agents + human members. Launch keeps
   identity/vibe/session head.
3. **Kind 0 `picture` as a huge data URL** breaks 4096 content.
   → Client downscales to ≤96px JPEG; store file under
   `$XDG_DATA_HOME/hush/avatars/<pubkey>.jpg`; kind 0 `picture` is
   `http://127.0.0.1:<port>/avatar/<pubkey>`. Fallback: omit picture.
4. **State-machine drift** in `index.html`.
   → Single `page` + `applySession`; extend `check_launch.sh`.
5. **Hick overload** (7 themes + profile + agent form).
   → Themes live only in Settings. Onboard stays 4 steps. Agent create
   is a dedicated drawer, not the splash.

## 3. Comprehensive Plan (Phases → Milestones → Tasks)

### Phase 0 — Environment & Isolation (COMPLETE)

- M0.1: Discarded stray `hush-c/demo/sw.js` on main, pulled, created
  `worktrees/onboard-profile-agents` on `gb/onboard-profile-agents`
  from `034cfd3d5`.
  - Verification: `pwd` contains `/hush/worktrees/`; branch is
    `gb/onboard-profile-agents`; `git status` clean.

### Phase 1 — Research & Discovery (GATE at end)

- M1.1: Inventory current UI + C contracts. (this session)
  - Task 1 of M1.1: Read `hush_launch.h/.c`, `hush_http.c` identity/vibe
    dispatch, `demo/index.html` gate/profile, `check_launch.sh`.
  - Task 2 of M1.1: Confirm session JSON fields and missing logout/agent
    APIs.
  - Verification: findings recorded in RESEARCH section below.
- M1.2: Nostr kind 0, Buzz agent-create, MIME, themes, buffer limits.
  - Task 1 of M1.2: Read Buzz `build_profile` + `AgentCreationPreview`
    + `channelAgents.ts` (name, avatar, systemPrompt).
  - Task 2 of M1.2: Confirm NIP-01 fields `name/display_name/about/picture`.
  - Task 3 of M1.2: Measure `HUSH_BUF_SZ`, `HUSH_EVENT_MAX_CONTENT`,
    embed path, feather assets.
  - Verification: constraints listed in this plan’s Risks.
- M1.3 (MANDATORY LAST): Synthesize RESEARCH + this plan + commit.
  - Task 1 of M1.3: Append synthesis to `RESEARCH.md`.
  - Task 2 of M1.3: Write this file.
  - Task 3 of M1.3:
    ```
    git add RESEARCH.md PLAN_ONBOARD_PROFILE_AGENTS.md
    git commit -m "Milestone 1.3: Phase 1 synthesis gate + onboard/profile/agent plan"
    git push -u origin HEAD
    ```
  - Verification: `git log --oneline -1` shows M1.3; files present.

### Phase 2 — Define / Architecture

- M2.1: Lock UI spec + data model + API table.
  - Task 1 of M2.1: Rewrite `UI_SPEC.md` for this slice (wizard 4
    steps, profile fields, 7 themes, agent drawer, Payne skill).
  - Task 2 of M2.1: Freeze API:
    - `POST /api/identity` += `logout`
    - `POST /api/profile` `{first_name,last_name,email,organization,theme,picture?}`
    - `POST /api/agent` `{name,system_prompt,save_pass,picture?,context:[{name,mime,text}]}`
    - `POST /api/member` `{npub,role:human|agent}`
    - `GET /avatar/<pubkey>` image bytes
    - session += `profile`, `theme`, `agents[]`, `members[]`
  - Task 3 of M2.1: Freeze MIME allow-list and size caps (named
    constants).
  - Task 4 of M2.1: Risk register update (buffer, kind 0 size).
  - Verification: spec committed; no implementation yet.
  - Commit: `Milestone 2.1: lock UI spec, session/API, MIME caps`

- M2.2: Module boundaries.
  - Task 1 of M2.2: `hush_roster` owns agents + members (header +
    empty `.c` stubs compiling).
  - Task 2 of M2.2: Document HTTP buffer raise (`HUSH_BUF_SZ` → 65536
    or HTTP-only reader). Prefer raising the existing named constant
    and noting why in a one-line comment.
  - Verification: `make -C hush-c` still passes with stubs unused.
  - Commit: `Milestone 2.2: roster module boundary + buffer decision`

### Phase 3 — Implementation

- M3.1: Feather splash + numbered wizard (UI only if session already
  drives the steps; C only if progress copy needs new fields).
  - Task 1 of M3.1: Splash card: `<img src="/icon-192.png" alt="hush"
    class="mark">` + “Sgt Major Payne reporting for duty.” + detect.
    Auto-advance when `ready`; else Begin → step 1.
  - Task 2 of M3.1: Progress dots 1/4–4/4: Identity, Backup, Vibe
    (public/private), Meet Payne (copy + Stand up / Enter hive).
  - Task 3 of M3.1: `./scripts/embed-ui.sh hush-c/demo && make -C hush-c`
  - Task 4 of M3.1: Extend `check_launch.sh` for splash feather +
    “Meet Payne” / progress.
  - Verification: cold HTML contains icon-192 splash and 4-step copy.
  - Commit: `Milestone 3.1: feather splash + numbered onboard wizard`

- M3.2: Logout + profile fields (C + UI).
  - Task 1 of M3.2: `hush_launch_logout` clears human login flags;
    vibe/agents remain. `POST /api/identity` `logout`.
  - Task 2 of M3.2: Profile struct on launch; `hush_launch_set_profile`;
    `POST /api/profile`; session emits `profile` + `theme`.
  - Task 3 of M3.2: Rewrite human kind 0 with `name`, `display_name`,
    `about` (org may append). No email in kind 0.
  - Task 4 of M3.2: Profile drawer form + confirm Logout.
  - Task 5 of M3.2: Unit tests in `test_launch.c` + check_launch greps.
  - Task 6 of M3.2: §14 checklist on touched C. Embed + `make test`.
  - Commit: `Milestone 3.2: profile fields + server logout`

- M3.3: Themes.
  - Task 1 of M3.3: CSS `:root` + `[data-theme=…]` for the seven
    palettes (color-blind = blue/orange, not green/red).
  - Task 2 of M3.3: Settings theme picker (radio, 7). Persist
    localStorage `hush-theme` and POST theme on change.
  - Task 3 of M3.3: `theme-color` meta follows theme.
  - Task 4 of M3.3: Embed + smoke that each token name appears.
  - Commit: `Milestone 3.3: seven themes in settings`

- M3.4: Avatar upload (Nostr-consistent).
  - Task 1 of M3.4: Raise HTTP read buffer (named constant) so a
    downscaled JPEG POST fits. Verify 20 KiB JSON POST succeeds.
  - Task 2 of M3.4: Client: file input `accept="image/jpeg,image/png,image/webp"`;
    canvas resize ≤96px; POST to `/api/profile` as raw base64 **or**
    write via a small `/api/avatar` JSON `{pubkey, mime, b64}`.
  - Task 3 of M3.4: Server: decode, sniff JPEG/PNG magic, write
    `avatars/<pubkey>.img`, serve `GET /avatar/<pubkey>`, set kind 0
    `picture` to that URL. Reject non-image MIME.
  - Task 4 of M3.4: Same widget reused on agent create.
  - Task 5 of M3.4: Tests: reject `text/plain` as avatar; accept
    tiny PNG. Embed + make test.
  - Commit: `Milestone 3.4: Nostr picture URL avatars`

- M3.5: Vibe members — add humans; public/private already exists.
  - Task 1 of M3.5: `hush_roster` human member (npub, display name).
  - Task 2 of M3.5: `POST /api/member`; session `members[]`.
  - Task 3 of M3.5: Hive “Invite human” drawer: npub field + show
    join token when private. Payne copy: “Name the soldier.”
  - Task 4 of M3.5: Hive “Vibe” control: visibility + copy invite.
    No second-relay create. Button label **Invite / manage vibe**
    (plus onboard “Stand up the hive”). Document why.
  - Task 5 of M3.5: Tests + embed + make test.
  - Commit: `Milestone 3.5: add humans to the vibe`

- M3.6: Agent creation + MIME-checked context files + Payne skill.
  - Task 1 of M3.6: `hush_roster_add_agent`: generate identity, slug,
    system prompt, context slots (max 4 files, 4096 bytes each,
    MIME `text/plain` | `text/markdown` | `text/x-markdown` or
    extension `.txt`/`.md`). Save nsec via pass default-on.
  - Task 2 of M3.6: Kind 0 for agent (`name`, `about`=prompt excerpt,
    `picture` if any). Welcome note in `#agents`.
  - Task 3 of M3.6: `POST /api/agent`. Session `agents[]` includes
    Payne + created agents (npub, name, slug — never nsec after ack).
  - Task 4 of M3.6: Agent drawer UI: avatar, name, system prompt
    textarea, context file input (`accept=".txt,.md,text/plain,text/markdown"`).
    JS rejects other MIME **and** server re-checks. Payne walkthrough
    copy on each field.
  - Task 5 of M3.6: Write `.goose/skills/agent-create/SKILL.md`
    (Payne/Goose: collect fields, POST `/api/agent`, confirm).
  - Task 6 of M3.6: Tests: happy path, reject `application/pdf` and
    `image/png` as context, pass path `hush/agents/<slug>/nsec`.
  - Task 7 of M3.6: §14 checklist. Embed + make test.
  - Commit: `Milestone 3.6: Payne-led agent create + MIME context + skill`

### Phase 4 — Verification, Polish, Integration & Cleanup

- M4.1: Full gate.
  - Task 1 of M4.1: `./configure && make clean && make && make test`
  - Task 2 of M4.1: `hush-c/tests/check_launch.sh`
  - Task 3 of M4.1: curl script covering logout, profile, theme,
    agent, rejected MIME, session shape.
  - Verification: all green.
- M4.2: Docs + Quinn/Parker/Payne audit.
  - Task 1 of M4.2: README mention of profile/themes/agent create.
  - Task 2 of M4.2: Bump SW cache `hush-ui-v1` → `hush-ui-v5`.
  - Task 3 of M4.2: Hick check (header still ≤5; themes in Settings).
  - Commit: `Milestone 4.2: docs, SW cache, audit`
- M4.3: Final commit + push.
  ```
  git add .
  git commit -m "Complete: onboard wizard, profile/themes, vibe members, Payne agent create"
  git push -u origin HEAD
  ```
- M4.4: PR + auto-merge.
  ```
  gh pr create --base main --head gb/onboard-profile-agents \
    --title "Onboard wizard, profile/themes, vibe members, Payne agent create" \
    --body "…"
  gh pr merge --auto --merge
  ```
- M4.5: After GitHub shows MERGED:
  ```
  cd /opt/repo/hush
  git checkout main && git pull --ff-only origin main
  git worktree remove worktrees/onboard-profile-agents
  git branch -d gb/onboard-profile-agents 2>/dev/null || true
  gh api -X DELETE repos/coldcanuk/hush/git/refs/heads/gb/onboard-profile-agents
  git worktree list
  ```

## 4. Audit the Plan (before execution)

- [x] Every Task has CLI/code direction, verification, Milestone ref.
- [x] Phase 1 gate is M1.3 (this commit).
- [x] Worktree lifecycle matches PRIME_DIRECTIVE (PR, not local merge).
- [x] Tasks are atomic; C isolated from HTML where possible.
- [x] write-legible-c called out for every C milestone.
- [x] Buffer / kind-0 / single-vibe constraints explicit.

Plan is frozen for execution after M1.3 commit.

## 5–7. Execute → Audit → Confirm

Follow strictly. Commit after every Milestone. Re-audit at end.
State “Grok Build complete.” only when DoD + PR merged + worktree gone.
