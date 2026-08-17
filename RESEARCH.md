# Hush C Port — Research Synthesis (Phase 1)

## Scope Locked
- MVP: Nostr NIP-01 basics for chat (kinds 0,1,5,7,9), EVENT/REQ/CLOSE/COUNT (COUNT minimal), simple #h channel tag + authors/ids/since.
- In-memory bounded store (ring buffer or fixed array of 1024 events for MVP).
- Filter match ported from buzz-core/filter.rs (AND within filter, OR across).
- Wire: newline-delimited JSON arrays for MVP (clients can wrap or we provide a tiny bridge later). Full WS in later milestone.
- Crypto: SHA-256 for event id computation (OpenSSL adapter or pure-C). Schnorr signature verification stubbed with explicit deviation comment (see hush_verify.h).
- No persistence, no auth challenge loop, no fan-out to Redis, no workflows.

## Key Findings
- Wire messages are tiny fixed-shape JSON arrays. Hand-rolled parser is feasible and keeps scope small + legible.
- StoredEvent concept maps to struct hush_event { char id[65]; ... uint32_t kind; char content[4096]; ... } with strict bounds.
- Filter: struct with optional arrays (kinds[8], authors[8], etc.) + tag match.
- Verification: id recompute + (stub) sig. For now id only.
- Rust uses heavy async; C will be single-thread poll loop (max 32 conns MVP).
- UI: see § Tailwind decision below.

## Tailwind Decision (logical opt-out for core)
**We opt out of using Tailwind inside C source or build.**
Reason: TailwindCSS is a CSS framework for HTML presentation. Hush C is a protocol implementation and relay engine. Mixing would violate separation and legible-C purity (no CSS strings in C).
Instead:
- hush-c/demo/ contains a standalone index.html using Tailwind via CDN (https://tailwindcss.com/docs/installation/play-cdn) + classes adapted from the licensed kits following the provided v4 rules (gap-*, text-base/7, no leading-*, bg-red-500/60, etc.).
- Optional later: a minimal HTTP server in C that serves the demo/ dir on GET /.
- This fulfills the requirement that "the user interface for our software 'Hush' will be displayed using TailwindCSS Plus".

## Risks Updated
- Parser: will decompose aggressively (tokenize, parse_array, parse_event_object as separate leaves).
- Bounds: every string has MAX_ const; loops bounded by MAX_FILTERS=8 etc.
- No recursion.

## Build Plan
- hush-c/Makefile
- Targets: all, test, clean, demo (copy or echo html)
- CFLAGS = -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -O2 -Iinclude
- Link: -lssl -lcrypto if using OpenSSL (guarded by #ifdef HUSH_USE_OPENSSL, else pure sha stub)

## Next Concrete Plan
See HUSH_C_RDAP_PLAN.md (this file updated in P1 gate). All subsequent phases use the skeleton from c-standard §15.
Every module starts with the 7-part layout.
Public API prefix: hush_
Status: hush_status_t enum per module, 0 = HUSH_OK.

## Verification Performed
- Read protocol, kind, filter, event, verification, handlers (event/req/ingest).
- Compiled probe C11 + poll + (optional) sha.
- Inspected UI kits.
- Confirmed no C source to conflict with.

## References
- crates/buzz-core/src/{kind,filter,event,verification}.rs
- crates/buzz-relay/src/{protocol.rs,handlers/*.rs}
- references/c-standard.md (loaded)
- Tailwind rules in user prompt.


## Final Verification (F.1)
- make clean && make && make test : PASSED (all 3 unit tests)
- Build uses: gcc -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2
- All .c/.h reviewed against c-standard.md §14:
  - No goto, no recursion
  - All loops have static upper bounds (i < n where n from MAX_ or count)
  - Functions split to <=40 lines where needed (relay_run, parse_line extracted)
  - Every fallible call checked and propagated
  - Prototypes present and match definitions
  - State-mutating leaves have asserts
  - Parameter order: ctx, outputs, inputs
  - Named constants for bounds
  - Single-purpose contracts (no "and")
  - No duplicated logic
- Demo UI (hush-c/demo/index.html) uses Tailwind via CDN + v4 practices (gap-*, no space-*, text-xs, bg-*/60 style, no @apply)
- No deviations from legible C without comments (crypto stub has explicit DEVIATION note in ARCHITECTURE.md and code comments)
- Scope: MVP chat kinds (0,1,5,7,9), EVENT/REQ/CLOSE over \n-JSON, bounded in-memory store, poll server. Non-goals respected.


---

# 2026-08 RDAP: Linux/BSD `./configure`, DEB/RPM GitHub Releases, `pass` Key Storage, `IMPORT.md`

**Feature set requested**:
- `./configure` must support compilation on Linux and BSD (FreeBSD, OpenBSD, NetBSD) for Hush (primarily the legible C11 hush-c relay and tools).
- GitHub releases must provide DEB and RPM packages (for hush-relay / Hush artifacts).
- Agent (and general key/token/password) creation flows must use the unix password manager `pass` for storage. Always offer:
  - Checkbox with exact label: "Check here to save the {password/key/token/etc} in the local password manager, `pass`"
  - Manual copy opportunity so user can save in any other manager.
  - Opt-out by leaving the box unchecked.
- New file `IMPORT.md` with step-by-step instructions for importing agents and channels from Buzz to Hush.
- `README.md` must link to `IMPORT.md`.
- All C code (if touched) must follow the write-legible-c standard (C11, §1 layout, §4 classification, §14 pre-delivery checklist, etc.).

## Research Synthesis (M1.1 – M1.6)

### M1.1: Build System, `./configure`, Linux + *BSD Portability
- **Current state (clean tree)**: No top-level `configure` or `Makefile` in HEAD (they were untracked/generated in prior session). `hush-c/` has a self-contained `Makefile`:
  ```
  CC := gcc
  CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2
  ...
  all: libhush.a hush-relay
  test: ...
  ```
  Build + test verified clean on Pop!_OS (Linux): `make clean && make && make test` → ALL TESTS PASSED.
- **C portability**: All headers and sources are strict POSIX.1 + C11. Key files use:
  `<poll.h>`, `<sys/socket.h>`, `<netinet/in.h>`, `<fcntl.h>`, `<unistd.h>`, `<stdlib.h>`, `<string.h>`, `<stdint.h>`, `<stddef.h>`, `<stdio.h>`, `<ctype.h>`, `<stdbool.h>`, `<assert.h>`, `<errno.h>`.
  - No `#ifdef __linux`, no `epoll`, no `/proc`, no glibc-isms.
  - One guarded OpenSSL path (`#if defined(HUSH_USE_OPENSSL)`) in hush_event.c for SHA; otherwise stub.
  - **Conclusion**: The C core will compile on any POSIX system with a C11 compiler (gcc/clang) and make/gmake. BSDs are viable.
- **OS detection patterns** (researched via uname + os-release):
  - Linux: `uname -s` = Linux; `/etc/os-release` present (ID=pop, ubuntu, debian, etc.).
  - FreeBSD: `uname -s` = FreeBSD; packages via `pkg`; build with `gmake`; often `cc` is clang.
  - OpenBSD: `uname -s` = OpenBSD; `pkg_add`; `gmake`; `CC=cc`.
  - NetBSD: `uname -s` = NetBSD; `pkgin`; `gmake`.
  - Darwin (macOS): `uname -s` = Darwin (not in scope per user, but same POSIX base).
- **Tooling differences**:
  - `make` vs `gmake`: BSDs ship BSD make; GNU make (gmake) is separate package and required for our Makefile features.
  - C compiler: `gcc` or `clang`. C11 flag `-std=c11` works.
  - Headers: same POSIX headers.
- **Prior configure** (from dirty stash inspection): A small POSIX sh script that detected OS via `/etc/os-release`, checked gcc/make/install, did a C11 compile probe, wrote `config.mk` with PREFIX/BINDIR/DATADIR/OS_ID. Good starting point to revive and extend.
- **Verification commands used**:
  - `uname -s -r`, `cat /etc/os-release`, `command -v gcc clang gmake make`, C11 probe compile, `make -C hush-c clean && make && make test`.
- **Gap to close**: Create a proper top-level `./configure` (or `configure` in hush-c context) that:
  - Is pure POSIX sh (`#!/bin/sh`, `set -e`).
  - Uses `uname -s` + fallback os-release.
  - Detects/ prefers `gmake` on BSD, falls back to `make`.
  - Runs C11 capability test with strict flags.
  - Writes `config.mk` (or updates hush-c/config.mk).
  - Prints per-OS install hints (e.g. `pkg install ...` on FreeBSD).
  - Respects PREFIX/BINDIR etc.
  - Exits non-zero on missing requirements.

**Success for this area**: `./configure && make -C hush-c` (or top make) succeeds on Linux today and is documented + detection-complete for the three BSDs.

### M1.2: DEB + RPM for GitHub Releases
- **Current release machinery** (RELEASING.md + .github/workflows/release.yml):
  - Desktop releases via `desktop-vX.Y.Z` tags → complex Tauri build matrix (macOS arm/x64, Linux container producing `.deb` + AppImage, Windows NSIS).
  - Linux job already does: `pnpm tauri build --bundles deb,appimage`.
  - Relay releases are Docker images (not native DEB/RPM).
  - No dedicated hush-relay (C) packaging or release lane yet.
- **User requirement**: "Github: releases; I want to support DEB and RPM."
  - Scope for Hush: at minimum the `hush-relay` C binary + minimal supporting files (license, readme snippet, desktop file if relevant) should produce installable .deb and .rpm.
- **Practical approaches researched**:
  - **Minimal native**:
    - DEB: `dpkg-deb --build` with a `DEBIAN/control` + data tree (usr/bin/hush-relay, usr/share/...).
    - RPM: minimal `.spec` + `rpmbuild -bb`.
  - **Recommended for maintainability**: `nfpm` (Go tool, simple yaml, produces deb/rpm/apk from one spec). Easy to vendor or `go install`.
  - Tauri desktop already gives Linux users a .deb today (for the full GUI). Hush C is the relay component — separate but complementary.
- **Release integration**:
  - Add `make package-deb` and `make package-rpm` (or `make packages`).
  - Document in RELEASING.md or a HUSH_RELEASES section: tag `hush-vX.Y.Z` (or reuse pattern) triggers a workflow that builds the C artifact + packages and attaches to a GitHub Release.
  - For now, since CI is heavy, provide working Makefile targets + scripts that a maintainer can run locally or in a simple ubuntu runner.
- **Verification artifacts**:
  - `dpkg-deb --info foo.deb` and `dpkg -c`.
  - `rpm -qpi foo.rpm` (if rpmbuild present) or at least spec lint.
- **Risk**: rpmbuild not installed everywhere; mitigate by making targets conditional or recommending `nfpm`.

**Success**: From a built tree, `make package-deb` and `make package-rpm` (or equiv) produce valid .deb/.rpm files that install the hush-relay binary, and the process is documented for GitHub Releases.

### M1.3 + M1.5: `pass` Integration for Keys/Tokens/Passwords (esp. Agent Creation)
- **`pass` CLI confirmed**:
  - `pass insert [--multiline,-m] [--echo,-e] [--force] pass-name`
  - Common pattern: `echo "$SECRET" | pass insert -e "hush/agents/my-agent/nsec"`
  - Or `pass insert -m "hush/agents/my-agent/nsec"` then paste multiline.
  - Store lives at `~/.password-store` (gpg encrypted files).
- **Current secret handling in codebase**:
  - Desktop (Tauri/Rust): `system-keyring` feature (keyring = "3.6.3" with sync-secret-service / apple-native / windows-native). Falls back to 0o600 files when disabled.
  - Tests mention "Keychain locked" surfaces.
  - buzz-cli and pairing use nsec strings (bech32) passed on CLI or generated via `Keys::generate()`.
  - Agent metadata (managed agents) persisted as JSON under app data dirs (e.g. `agents/managed-agents.json`).
  - No current use of the `pass` tool.
- **User contract (exact)**:
  - "Whenever a key or a token or a password needs to be saved when using Hush the application will use the unix password manager `pass`"
  - "the software will also provide the human user the opportunity to manually copy the data and save it in another password manager should they chose to."
  - "The human user also has the opportunity to "NOT" save using `pass` and opt-out by "unchecking" the box."
  - **Checkbox label (verbatim)**: `Check here to save the {password/key/token/etc} in the local password manager, `pass``
- **Integration points**:
  - Primary: Agent creation / key generation flows (nsec for new agents).
  - Secondary: Any token/password save (relay auth, API tokens, etc.).
- **Implementation approach**:
  - Provide a small, portable wrapper (e.g. `scripts/hush-pass` or `bin/hush-pass`) that encapsulates the namespace and calling convention.
    - `hush-pass save "agents/<name>/nsec" "nsec1..."`
    - `hush-pass get "agents/<name>/nsec"`
    - Handles "pass not found" gracefully.
  - In UI/CLI flows that persist secrets:
    - Show checkbox with the exact label.
    - If checked → call pass helper (or direct `pass insert`).
    - Always show "Copy value (I will save it myself in my password manager)" button/link.
    - If unchecked → do not call pass (use existing keyring/file or warn that secret is ephemeral).
  - Desktop (TS/Rust) will need a Tauri command or sidecar invocation of the helper (or direct shell for simplicity in controlled env).
  - CLI flows already accept nsec; we document the pass path and can add a `--use-pass` flag.
- **Graceful degradation**:
  - If `pass` binary missing or store not initialized (`pass ls` fails), show clear message + manual copy path. Do not hard-fail the whole agent creation.
- **Security**:
  - Never log the secret.
  - Use `pass insert -m` or pipe so it does not appear in process list.
  - GPG key must be set up by user (standard pass requirement).

**Success**: Any flow that offers to persist a hush-related secret presents the exact checkbox text, calls `pass` when checked, offers manual copy, and respects uncheck = no pass. Helper script exists and is used by docs + code paths.

### M1.4: Buzz → Hush Import (Agents + Channels)
- **Buzz agent data locations** (researched):
  - Desktop app data: `agents/managed-agents.json`, `agents/personas.json`, `agents/teams/*/agents/*.persona.md`.
  - Secrets: OS keyring (via keyring crate) or fallback files. nsec is the critical secret.
  - Local models and caches under `~/.buzz/` (mostly TTS/models in this env; agent state is higher level).
- **Channels**: Pure Nostr events (kinds ~40/41/42/43 for communities/channels/threads). History lives on the relay the user was connected to. No single "export file" — state is the event log + local client metadata (last seen, unread, etc.).
- **Export paths from Buzz**:
  - Profile → reveal nsec (with keyring unlock).
  - buzz-cli may have export for emojis/mems; for agents primarily the JSON files + manual nsec copy.
  - Full data export would be "copy the app support dir + extract nsec".
- **Hush side (C relay focus)**:
  - Hush (hush-relay) is the protocol endpoint. "Import agents" means:
    1. Bring the private keys (nsec) into the user's password store under the hush namespace so Hush-aware tools can find them.
    2. Configure any Hush desktop/CLI/agent harness to use those keys against the Hush relay.
    3. Channels: point the client at the new Hush relay URL; re-create or re-subscribe to channels of interest. If user has a prior event dump or the old relay is still up, replay historical events of interest (kind 1/9 chat etc.).
  - For full fidelity, users publish their kind 0 (profile) and re-join channels; agents re-announce if needed.
- **IMPORT.md content outline** (to be written):
  - Prerequisites (install pass + init GPG, have Buzz running or data accessible).
  - Step 1: Locate/export your agent nsec(s) from Buzz (desktop UI or keyring show).
  - Step 2: For each agent, use the Hush agent creation flow (or CLI) and check the `pass` box, or manually:
    ```
    pass insert -m "hush/agents/<agent-name>/nsec"
    # paste the nsec1... value
    ```
  - Step 3: (Optional) Also save other tokens (relay auth, etc.) under `hush/...`.
  - Step 4: Import channels / history.
    - Run Hush relay.
    - In desktop/CLI point at ws://localhost:10555 (or your Hush port).
    - Re-create channels or use any archive/replay tool to publish prior events into Hush if desired.
  - Step 5: Verify with `pass ls hush/agents` and test connection.
  - Troubleshooting (pass not found, GPG issues, key format).
- **README link**: Add under "Getting started", "Hush C relay", and/or a new "Migration from Buzz" section: see `IMPORT.md`.

**Success**: `IMPORT.md` exists with accurate, numbered steps. `README.md` contains a clear link. A user following it can move their agent identities and continue using channels on a Hush relay.

### M1.6: Legible-C Obligations
- Existing `hush-c/` (from prior gb/hush-c-port-rdap) already audited and passes §14 checklist.
- Functions are small, flat, guarded; prototypes at top; named constants; asserts on mutating leaves; no recursion; bounded loops; status returns; `MODULE_TRY` pattern ready.
- **Rule for this RDAP**: If any `.c`/`.h` is created or edited (e.g. a small hush-pass C wrapper or example), it must be written from the §15 skeleton, reviewed against the full 17-item checklist before commit, and the commit message / PR note the gate.
- No C changes are required for configure (sh), packaging (make + control/spec), pass helper (sh), or IMPORT.md (markdown). Any example C will be isolated and explicitly called out.

## Updated Concrete RDAP Plan (Phases → Milestones → Tasks)

All work continues exclusively inside the worktree.
Commit after every completed Milestone with message: "Milestone X.Y: <concise>"

### Phase 0 – Environment & Isolation Setup
**Status**: COMPLETE (worktree `gb/configure-linux-bsd-deb-rpm-pass-import`, main clean at fork, verified).

### Phase 1 – Research & Discovery
**Status**: Research tasks M1.1–M1.6 complete. This section (M1.7) is the synthesis gate.

**M1.7 (mandatory gate – this commit)**:
- Task 1.7.1: Append full synthesis + this plan to `RESEARCH.md`.
- Task 1.7.2: Verify no uncommitted changes outside the update.
- Task 1.7.3: `git add RESEARCH.md && git commit -m "Milestone 1.7: Synthesize research into RESEARCH.md and produce updated concrete plan for remaining phases."`
- Verification: `git log --oneline -1`, `git show --stat`, clean status after.

### Phase 2 – Define / Architecture
**M2.1**: Configure interface definition
- Define exact CLI surface: `./configure [--prefix=...] [--bindir=...] [--with-cc=...] [--with-make=...]`
- OS matrix table (Linux, FreeBSD, OpenBSD, NetBSD) with package commands and expected tools.
- Output: `config.mk` vars (CC, CFLAGS, MAKE, OS_ID, PREFIX, etc.).
- Produce `configure` script design doc in a comment or small DESIGN section.

**M2.2**: `pass` storage contract + UX definition
- Namespace: `hush/<category>/<name>/<secret-type>` e.g. `hush/agents/brain/nsec`, `hush/relay/auth-token`.
- Exact checkbox label as specified.
- API for helpers (save, get, has, remove).
- Fallback behavior when pass absent or unchecked.
- Manual copy always available (copy button + instructions).
- Security notes (no echo in argv, gpg trust).

**M2.3**: Packaging architecture for DEB + RPM
- Artifact contents for hush-relay package (binary, LICENSE excerpt, man page stub if any, hush.desktop if relevant).
- Makefile targets: `package-deb`, `package-rpm`, `packages`.
- Use nfpm (preferred, one yaml) + fallback native paths.
- Versioning from git tag or `VERSION` file / hush-c.
- Output location: `dist/hush-relay-<ver>.deb`, `.rpm`.
- CI note for attaching to GitHub Releases on `hush-v*` tags (or manual for now).

**M2.4**: `IMPORT.md` structure + README placement
- Outline sections: Prerequisites, Export from Buzz (keys + metadata), Save with pass, Channel migration, Verification, Troubleshooting.
- Exact link text and placement in README (e.g. after "Getting started" and in Hush section).

**M2.5**: Risk register update + scope boundaries
- Update top risks.
- Explicit non-goals (no full desktop rewrite for pass in this slice; provide contract + one reference implementation + docs).
- Legible C obligations restated.

**Verification for Phase 2**: All design decisions written into `RESEARCH.md` or a new `RDAP_PLAN.md` appendix; no implementation code yet.

### Phase 3 – Implementation (small-win Milestones)
**M3.1**: Implement `./configure`
- Task 3.1.1: `cat > configure << 'EOF' ... EOF` (full POSIX sh script).
  - OS detection via `uname -s`.
  - Tool checks (cc, make/gmake, install).
  - C11 probe compile with strict flags.
  - Write `config.mk`.
  - Per-OS hints (apt, pkg, pkg_add, pkgin).
  - Support PREFIX etc.
- Task 3.1.2: Make it executable, add to .gitignore if needed (or track it).
- Verification: `./configure && cat config.mk && (cd hush-c && make clean && make && make test)` on Linux. Also run with custom PREFIX.

**M3.2**: Packaging targets (DEB + RPM)
- Task 3.2.1: Create or update top-level `Makefile` (or extend hush-c one) with `package-deb` and `package-rpm`.
  - For deb: stage tree, write DEBIAN/control, `dpkg-deb --build`.
  - For rpm: write minimal `hush-relay.spec`, `rpmbuild -bb` (or nfpm pack rpm).
- Task 3.2.2: Add `make packages` convenience + `make package-clean`.
- Task 3.2.3: Document in RELEASING.md or new HUSH_RELEASE_NOTES.
- Verification: `make package-deb && dpkg-deb --info dist/*.deb && dpkg -c ... | grep hush-relay`. Same for rpm if tools present (or spec check).

**M3.3**: `pass` helper + contract implementation
- Task 3.3.1: `cat > scripts/hush-pass << 'EOF' ...` (bash wrapper).
  - Commands: save, get, has, rm, ls.
  - Uses `pass insert -m`, `pass show`, etc.
  - Namespace prefix "hush/".
  - Error handling and "pass not found" messages.
- Task 3.3.2: Make executable.
- Task 3.3.3: Add usage examples + the exact checkbox string in the script header and in a `docs/pass-integration.md` (or directly in IMPORT).
- Task 3.3.4 (optional thin): If desktop code touched, add the checkbox in the relevant agent creation UI with label and wire to a Tauri command that invokes the helper or pass when checked. At minimum, add the string in comments / docs so implementers use it.
- Verification:
  - `scripts/hush-pass save "test-agent/nsec" "nsec1test..."` (or echo pipe).
  - `pass show hush/test-agent/nsec` shows it.
  - `scripts/hush-pass get "test-agent/nsec"`.
  - Manual copy path demonstrated in help.
  - Unchecked path documented.

**M3.4**: Create `IMPORT.md`
- `cat > IMPORT.md << 'EOF' ... full step-by-step ...`
- Accurate based on research (nsec location, pass path, channel re-subscribe).
- Include screenshots placeholders or commands.
- Verification: `cat IMPORT.md | head -100`; markdown lint if available or manual review; cross-check against real Buzz data locations.

**M3.5**: Wire `IMPORT.md` into `README.md`
- Add prominent link:
  - In "Getting started"
  - In the Hush / C relay section
  - Possibly a "Migrating from Buzz" subsection
- Text like: "Importing your agents and channels from Buzz? See [IMPORT.md](IMPORT.md)."
- Verification: `grep -n 'IMPORT.md' README.md`; render check (head + context).

**M3.6**: Any C changes (if required) – legible C gate
- If a small C example or pass shim is added (e.g. for embedding), write from §15 skeleton.
- Run full §14 checklist before commit.
- Verification: build + test + explicit checklist output in commit or PR.

**M3.7**: Documentation + help text polish
- Update README "Requirements", "Standard Workflow", "Configuration" with new `./configure` behavior, BSD notes, pass requirement, and link to IMPORT.
- Update any HUSH_*.md if needed.
- Add `pass` to requirements where relevant.

**Milestone commit rule**: After each M3.x above, `git add . && git commit -m "Milestone 3.x: <what>"`.

### Phase 4 – Verification, Polish, Integration & Cleanup (Final Phase)
**M4.1**: Full verification matrix
- Linux: `./configure && make && make test && make packages`
- BSD simulation: document exact commands a user on FreeBSD/OpenBSD/NetBSD would run; run uname-based logic tests.
- pass roundtrips + checkbox text audit.
- IMPORT steps executed mentally + against real ~/.buzz if present.
- dpkg/rpm artifacts inspected.
- All new files pass basic lint (sh -n configure, shellcheck if present, markdown).

**M4.2**: Legible C pre-delivery (if any C touched)
- Run the 17-item checklist explicitly.

**M4.3**: Docs, links, CHANGELOG touch
- Ensure README link works.
- Update CHANGELOG.md with entry for the features.

**M4.4**: Final commit + release prep
- `git add . && git commit -m "Complete: configure BSD+Linux, DEB/RPM releases, pass key storage, IMPORT.md – ready for merge"`
- Push branch, prepare merge instructions (or open PR).
- Return to main, merge --no-ff, push.
- Worktree remove.

**M4.5**: Post-merge cleanup + confirmation
- Verify main is clean.
- State "Grok Build complete." (or equivalent for this RDAP).

## Top Risks (updated)
1. BSD compile not testable live here → Mitigation: rigorous POSIX check + detailed per-BSD instructions + CI note.
2. rpmbuild absent in container → Mitigation: nfpm path + "or install rpmbuild" note; produce spec + control files even if build step skipped.
3. Desktop agent creation is large surface → Mitigation: implement contract + helper + exact string in docs/UI comments first; full wiring can be incremental.
4. pass not installed for user → Mitigation: graceful messages + manual copy always offered.
5. Scope creep into full migration tooling → Mitigation: IMPORT.md is instructions + commands; no new heavy importers unless small-win fits.

## Definition of Done (recap)
- [ ] `./configure` works on Linux + documented for BSDs; produces usable build.
- [ ] `make package-deb` and `make package-rpm` (or nfpm) succeed and produce valid packages for hush-relay.
- [ ] GitHub release process updated or documented for DEB/RPM attachment.
- [ ] pass helper exists; checkbox text exact; manual copy + opt-out paths present in docs and code paths.
- [ ] `IMPORT.md` complete and linked from README.md.
- [ ] All C (if any) passes legible-C §14.
- [ ] All Milestones committed; worktree lifecycle followed; main clean after merge.
- [ ] "Grok Build complete."

**References for implementers**: This RESEARCH.md, HUSH_ARCHITECTURE.md, c-standard.md (loaded), RELEASING.md, desktop/src-tauri secret handling, pass(1) man page.


## 2026-08-17 Phase 1 continuation (this RDAP instance)

### M1.3 C completeness
- hush-c/ implements minimal viable Nostr relay core for kinds 0/1/5/7/9 over TCP \n-JSON.
- Builds clean with strict C11 flags, 3 unit tests pass.
- configure + top Makefile already drive hush-c exclusively for C artifacts.
- No other C in tree.
- Post-excision: the repo surface will be C-only (hush-c + build glue + docs + .goose).

### M1.4 Branch/main protection + worktree discipline
- Hush does NOT carry Buzz's 580 branches. Remote tracking is historical.
- Policy: `git branch -r` output is not used for work. Feature work ONLY via `git worktree add -b gb/<slug> worktrees/<slug>` from clean main.
- main protected by social + documented contract: direct commits to main forbidden except for merge of completed worktrees.
- Orphaned wts: forbidden inside Hush. All wts under worktrees/ inside the git repo. External /opt/repo/*-wt* belong to sibling experiments; prune via their owning .git or rm when confirmed loose.
- Goose prime directive: the AGENTS.md will be replaced by short Goose-specific; .goose/ is the skills/config home.


---

## Phase 1 Synthesis Gate (M1.5) — 2026-08-17

### Consolidated Findings
**Repo identity**: Hush is the C11 + Goose successor experiment. It started as a Buzz fork but the mandate is complete port to legible C11 with Goose as sole agent, worktree-only development, main protected by workflow.

**Current surface (pre-excision)**:
- C core: hush-c/ (6 .c, 6 .h, tests, demo Tailwind html, Makefile). Builds + 3 tests pass under strict -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow. Uses poll(2) + minimal hand-rolled Nostr array parser over \n-JSON. MVP kinds 0,1,5,7,9 + #h filter. Crypto id stubbed (dev only).
- Build: top configure (POSIX sh, Linux+*BSD detection, writes config.mk), top Makefile delegates to hush-c. Good.
- Agent: .goose/ stub + GOOSE.md + 3 skills (worktree, c-build, c-test). Legacy .agents/.claude/.codex present with empty or symlinked skills (to be removed).
- Docs: AGENTS.md is the full Buzz 624-line guide (wrong), CODE_OF_CONDUCT.md is correct SQLite Code of Ethics (Rule of St. Benedict) since e92b7085d, README describes Hush C correctly at high level, many VISION_*.md and Buzz docs remain.
- Git: main + current gb/* only locally. ~580 origin/ remotes are Buzz history — do not use. Worktree inside repo under worktrees/.
- Rust/TS/Flutter: full crates/ (25+), desktop/, mobile/, web/, Cargo.*, pnpm*, Justfile, etc. All to be excised.
- /opt/repo stray wts: lumenEh-* and loose worktrees belong to other experiments; Hush policy is "all worktrees inside the repo dir under worktrees/".

**Scope for this RDAP (full port)**:
Primary Goal: Hush on main is pure legible C11 (hush-c becomes the implementation), .goose/ is the complete Goose skills/config home, all development follows documented worktree lifecycle from clean main, no Rust/TS/Flutter/legacy agent dirs remain, CoC is SQLite ethics, main is protected by documented process, Buzz branches are acknowledged as historical only.

Non-Goals: Full Nostr (no full WS, no Schnorr real verify in MVP, no Postgres/Redis, no agents desktop surface, no mobile). We keep the scoped relay core + build + Goose harness.

Success (measurable):
- After excision + polish: `git ls-files | grep -E '\.(rs|tsx|ts|dart|lock)$|Cargo.toml|pnpm' | wc -l` == 0 (except maybe docs references that are updated).
- `./configure && make && make test` succeeds clean.
- All .c/.h pass write-legible-c §14 checklist (audited in Final Phase).
- .goose/skills has at least worktree, c-build, c-test, legible-c, relay, goose-init; GOOSE.md + short AGENTS.md declare Goose-only + worktree prime directive.
- README/CONTRIBUTING/AGENTS.md updated; no "just ci", "cargo", "desktop dev" instructions for Hush.
- Local branches: only main + active gb/*; `git branch -r` not used for work.
- Worktree removed after merge; main clean; "Hush C/Goose RDAP complete."

Constraints: C11 + write-legible-c mandatory. POSIX sh for configure. Keep demo/ as static Tailwind (CDN) separate from C. No new heavy deps.

Risks (updated):
1. Removing large surface may leave stale references in docs — mitigate by targeted grep/sed + review.
2. hush-c parser is naive — keep as MVP; document as such.
3. Skills need to be actionable for Goose (exact commands + verifs).
4. Main protection is process, not git hooks (unless we add simple one); document strongly.

### Updated Concrete Plan (post M1.5)
See HUSH_C_RDAP_PLAN.md updated below or follow the RDAP structure executed here:
- Phase 2: Define exact excision list, new repo layout, updated headers if needed, AGENTS.md rewrite plan.
- Phase 3: Impl excision (rm -rf crates desktop ...), populate .goose/skills fully, rewrite root docs, polish hush-c for full §14 (fix any near-misses), update configure/Makefile if needed for C-only, add main-protection note + example pre-push.
- Phase 4/Final: Full build+test+audit, git lifecycle (push branch, merge --no-ff on main, worktree remove), prune local branches, final clean status, confirmation.

Every remaining Milestone will be committed with "Milestone X.Y: ..."

**Verification of gate**: This section + commit message "Milestone M1.5" + updated plan artifact.

