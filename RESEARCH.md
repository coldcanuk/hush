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


---

# 2026-08-17 RDAP: OpenBSD + FreeBSD package management

**Feature requested**: support OpenBSD and FreeBSD package management in addition to existing DEB, RPM, and Flatpak.

Primary sources (fetched this session):
- https://www.openbsd.org/faq/faq15.html (Package Management)
- https://www.openbsd.org/faq/ports/ports.html (Working with Ports)
- https://man.openbsd.org/pkg_create.1
- https://www.freebsdsoftware.org/blog/freebsd-pkg-reference.html
- https://man.freebsd.org/cgi/man.cgi?query=pkg-create&sektion=8&manpath=FreeBSD+14.3-RELEASE+and+Ports
- Existing in-tree packaging: `debian/`, `hush-relay.spec`, `io.github.coldcanuk.hush.yml`, top-level `Makefile` targets `deb` / `rpm` / `flatpak`

## Scope locked

### Primary Goal
Ship first-class OpenBSD and FreeBSD packaging for `hush-relay` that a BSD user can:
1. drop into the official ports tree layout and `make package`, **or**
2. build a local binary package and install it with the native tool (`pkg_add` on OpenBSD, `pkg add` / `pkg install` on FreeBSD).

### Non-Goals
- Do not rewrite DEB / RPM / Flatpak unless a shared hook is broken (DESTDIR forwarding).
- Do not submit to official OpenBSD ports CVS or FreeBSD ports git in this slice.
- Do not add rc.d / rcctl / service units (relay is a foreground binary today).
- Do not produce a Linux-built binary labeled as an OpenBSD/FreeBSD package (wrong ABI).
- No NetBSD pkgsrc in this slice (configure already hints at it).
- No C core changes.

### Success Criteria / Definition of Done
- [ ] In-tree OpenBSD port skeleton: `openbsd/net/hush-relay/{Makefile,pkg/DESCR,pkg/PLIST}`
- [ ] In-tree FreeBSD port skeleton: `freebsd/net/hush-relay/{Makefile,pkg-descr,pkg-plist}`
- [ ] `make openbsd` and `make freebsd` (and `make bsd`) exist and are documented
- [ ] Scripts stage a destroot + packing metadata on any POSIX host
- [ ] On the native OS, scripts invoke `pkg_create` (OpenBSD) or `pkg create` (FreeBSD)
- [ ] README documents install via `pkg_add` and `pkg add` / `pkg install`, matching the DEB/RPM/Flatpak sections
- [ ] Top-level `make install` forwards `DESTDIR` (required by every packaging path)
- [ ] No `.c`/`.h` edits; worktree lifecycle + PR to `main`

### Constraints
- C11 + write-legible-c if any C is touched (none expected).
- POSIX `sh` for scripts (`set -eu`).
- Worktree: `/opt/repo/hush/worktrees/bsd-pkg` on `gb/bsd-pkg` only.
- Version source of truth: top-level `VERSION` (`0.0.1`).
- License: GPLv3+ (same as DEB/RPM).
- BSD third-party prefix is `/usr/local` (not `/usr`).

### Assumptions
- GitHub source tags match `VERSION` (`0.0.1`) so `GH_TAGNAME` / `SITES` work when a release exists; local `make dist` is the fallback tarball.
- `gmake` is required on both BSDs (already detected by `./configure`).
- No runtime shared-library deps beyond libc (`WANTLIB = c`).
- This host is Linux: native `pkg_create` / `pkg create` will not run here; verification is metadata + staging + `sh -n`.

### Required environment / tools
- Always: POSIX sh, make/gmake, C11 compiler, `install(1)`, `sha256`/`sha256sum`/`openssl`.
- OpenBSD native finish: `pkg_create(1)`, `pkg_add(1)`, `pkg_info(1)`, `pkg_delete(1)`.
- FreeBSD native finish: `pkg create`, `pkg add`, `pkg info`, `pkg delete`.
- Ports-tree finish: OpenBSD `/usr/ports` + `bsd.port.mk`; FreeBSD `/usr/ports` + `bsd.port.mk`.

### Top risks
1. **Cannot run `pkg_create` / `pkg create` on this Linux host.** Mitigation: complete ports skeletons + staging scripts; native invocation is a guarded branch; document the exact on-BSD commands.
2. **OpenBSD `pkg_create` is picky (COMMENT, FULLPKGPATH, packing-list annotations).** Mitigation: follow `pkg_create(1)` mandatory `-D COMMENT=` / `-D FULLPKGPATH=` / `-d` / `-f` / `-p`; use `@bin` for the executable.
3. **FreeBSD `pkg create` manifest vs plist vs rootdir mismatch.** Mitigation: generate `+MANIFEST` with `files` hashes from the destroot; also keep a ports `pkg-plist`.
4. **Top Makefile drops `DESTDIR`.** Mitigation: forward it in `install`/`uninstall` (shared fix, not a DEB/RPM rewrite).
5. **`.gitignore` has `*.plist`.** Mitigation: OpenBSD file is `pkg/PLIST` (no suffix); FreeBSD is `pkg-plist`.

## Research synthesis

### Existing packaging surface (this tree)
| Format | Path | Consumer | Prefix | Build hook |
|--------|------|----------|--------|------------|
| DEB | `debian/` (`control`, `rules`, `changelog`, `copyright`, `hush-relay.install`) | `dpkg-buildpackage` via `make deb` | `/usr` | `override_dh_auto_*` |
| RPM | `hush-relay.spec` | `rpmbuild -bb` via `make rpm` (needs `make dist`) | `/usr` | `%configure` + `%make_install` |
| Flatpak | `io.github.coldcanuk.hush.yml` | `flatpak-builder` via `make flatpak` | `/app` | simple buildsystem |
| Source | `make dist` → `git archive` | all of the above | n/a | `VERSION` |

Package identity today: **`hush-relay`**, summary “Lightweight, legible C11 Nostr relay core”, maintainer `coldcanuk@users.noreply.github.com`, license GPLv3+.

Install payload (from `hush-c/Makefile`):
- `$(BINDIR)/hush-relay` (0755)
- `$(DATADIR)/applications/hush-relay.desktop` (0644)
- `$(DATADIR)/icons/hicolor/{48,128,256}x{48,128,256}/apps/hush-relay.png` (0644)

`configure` already prints BSD hints (`pkg install -y gmake gcc pkgconf` / `pkg_add gmake`) and prefers `gmake` when `uname` is FreeBSD/OpenBSD/NetBSD. C sources are POSIX (`poll`, BSD sockets) — no Linux-only APIs. **The gap is binary/port packaging, not compilation.**

Bug found: top-level `Makefile` `install` does not pass `DESTDIR` into `hush-c`. `debian/rules` and the RPM spec pass `DESTDIR` at the top; it is currently ignored. Must fix as part of this work.

### OpenBSD (FAQ 15 + ports + pkg_create)

**User-facing tools** (FAQ 15):
| Action | Command |
|--------|---------|
| Install from mirror | `pkg_add hush-relay` |
| Install local file | `pkg_add ./hush-relay-0.0.1.tgz` |
| Search | `pkg_info -aQ hush` |
| Info | `pkg_info hush-relay` |
| Update all | `pkg_add -u` |
| Remove | `pkg_delete hush-relay` |
| Remove leftover deps | `pkg_delete -a` |

Packages are `.tgz` **plus** packing metadata (not a raw tarball). Database: `/var/db/pkg`. Mirror via `/etc/installurl` or `PKG_PATH`.

**Creation** (`pkg_create(1)`):
```
pkg_create [-A arches] [-B pkg-destdir] -d desc \
  -D COMMENT=value -D FULLPKGPATH=value -D PORTSDIR=value \
  -f packinglist -p prefix pkg-name
```
- `COMMENT` and `FULLPKGPATH` are mandatory for updates.
- `-B` is the destroot prepended when reading files.
- `-p` is the install prefix (record + `@cwd` base). Default localbase `/usr/local`.
- `-A '*'` = arch-independent; we will **not** use that for the compiled binary. Omit `-A` so the package is native-arch, or pass the build arch.
- Packing-list: filenames relative to `@cwd`; `@bin` for OpenBSD executables; trailing `/` or `@dir` for directories.

**Ports tree** (ports FAQ):
```
/usr/ports/<category>/<port>/
  Makefile
  distinfo          # SHA256 + SIZE; generated by `make makesum`
  pkg/PLIST
  pkg/DESCR
  patches/          # optional
  files/            # optional
```
`make` in the port dir walks depends, fake-installs, then `pkg_create`. Official advice: prefer packages over building ports; we still ship a port so `make package` works.

In-tree we keep a **drop-in copy** at `openbsd/net/hush-relay/` (category `net`, matching DEB `Section: net`).

Port Makefile variables we will set:
- `COMMENT`, `DISTNAME`/`PKGNAME`, `CATEGORIES=net`
- `HOMEPAGE`, `MAINTAINER`
- `# GPLv3+` then `PERMIT_PACKAGE = Yes`
- `GH_ACCOUNT` / `GH_PROJECT` / `GH_TAGNAME` (or `SITES` + `DISTFILES`)
- `WANTLIB = c`
- `USE_GMAKE = Yes`
- `CONFIGURE_STYLE = simple`, `CONFIGURE_ARGS = --prefix=${PREFIX}`
- `TEST_TARGET = test`

### FreeBSD (pkg reference + pkg-create(8) + Porter's Handbook)

**User-facing tools**:
| Action | Command |
|--------|---------|
| Bootstrap | `pkg bootstrap` (first use) |
| Install from repo | `pkg install hush-relay` |
| Install local file | `pkg add ./hush-relay-0.0.1.pkg` |
| Search | `pkg search hush` |
| Info / files | `pkg info hush-relay` / `pkg info -l hush-relay` |
| Update catalog + upgrade | `pkg update && pkg upgrade` |
| Remove | `pkg delete hush-relay` |
| Orphans | `pkg autoremove` |
| Which package owns a file | `pkg which /usr/local/bin/hush-relay` |
| Create from installed | `pkg create hush-relay` |
| Create from destroot | `pkg create -M +MANIFEST -r rootdir -o outdir` |

Packages are `.pkg`. Database: `/var/db/pkg`. Repos: `/etc/pkg/FreeBSD.conf` + `/usr/local/etc/pkg/repos/`. Poudriere is the official bulk builder; we do not require it.

**`pkg create` metadata (`+MANIFEST`)** — UCL or JSON:
- Required-ish: `name`, `version`, `origin` (`category/port`), `comment` (one line), `desc`, `maintainer`, `www`, `prefix`
- Optional: `licenses`, `categories`, `abi`/`arch`, `deps`, `files` (path → sha256 or `{uname,gname,perm}`)
- `-r rootdir` makes archive paths relative to that destroot
- Legacy `-p plist` (`@dir`, `@mode`, `@owner`, `@group`) also works

**Ports tree**:
```
/usr/ports/<category>/<port>/
  Makefile
  distinfo          # TIMESTAMP + SHA256 + SIZE; `make makesum`
  pkg-descr         # long desc; last line `WWW: url`
  pkg-plist         # or PLIST_FILES in Makefile for tiny ports
```

In-tree drop-in: `freebsd/net/hush-relay/`.

Port Makefile variables we will set:
- `PORTNAME=hush-relay`, `DISTVERSION` from `VERSION`
- `CATEGORIES=net`, `MAINTAINER`, `COMMENT`, `WWW`
- `LICENSE=GPLv3+`, `LICENSE_FILE=${WRKSRC}/LICENSE`
- `USE_GITHUB=yes`, `GH_ACCOUNT=coldcanuk`, `GH_PROJECT=hush`
- `USES=gmake`, `HAS_CONFIGURE=yes` (our script is not GNU autoconf)
- `CONFIGURE_ARGS=--prefix=${PREFIX}`
- `PLIST_FILES` listing the five installed paths

### Shared architecture (Phase 2 decisions)

```
                    VERSION + hush-c install payload
                                |
        +-----------------------+-----------------------+
        |                       |                       |
   debian/ + spec + yml    openbsd/net/hush-relay   freebsd/net/hush-relay
        |                       |                       |
   make deb|rpm|flatpak    make openbsd             make freebsd
                                |                       |
                    scripts/package-openbsd.sh   scripts/package-freebsd.sh
                                |                       |
                    destroot + PLIST + DESCR     destroot + +MANIFEST
                                |                       |
                    [OpenBSD] pkg_create         [FreeBSD] pkg create
                                |                       |
                    hush-relay-VER.tgz           hush-relay-VER.pkg
                                |                       |
                    pkg_add ./file.tgz           pkg add ./file.pkg
```

Rules:
1. **Ports skeletons are the source of truth** for names, comment, origin, plist.
2. **Scripts never emit a `.tgz`/`.pkg` on the wrong OS.** Staging + metadata always; native tool only when `uname` matches.
3. **Prefix is `/usr/local`** on both BSDs.
4. **Origin/FULLPKGPATH is `net/hush-relay`.**
5. **DESTDIR is forwarded** from the top Makefile.
6. **No libc-other deps.** Empty `RUN_DEPENDS` / no `deps` key.

### Updated concrete plan (remaining phases)

**Phase 0** — COMPLETE: worktree `/opt/repo/hush/worktrees/bsd-pkg`, branch `gb/bsd-pkg`.

**Phase 1** — this section is the synthesis gate (M1.7).

**Phase 2** — architecture frozen in this section (M2.1). No extra doc file required.

**Phase 3 — Implementation**
- M3.1 OpenBSD: `openbsd/net/hush-relay/{Makefile,pkg/DESCR,pkg/PLIST}`, `openbsd/README.md`, `scripts/package-openbsd.sh`
- M3.2 FreeBSD: `freebsd/net/hush-relay/{Makefile,pkg-descr,pkg-plist}`, `freebsd/README.md`, `scripts/package-freebsd.sh`
- M3.3 Wiring: top `Makefile` targets `openbsd` `freebsd` `bsd`; forward `DESTDIR`; README install sections; `.gitignore` `*.pkg`

**Phase 4 — Verify + land**
- `sh -n` scripts; run both package scripts on Linux; assert destroot + metadata
- `make -n openbsd freebsd bsd`
- Confirm no C edits
- Commit, push `gb/bsd-pkg`, PR → auto-merge, remove worktree

### References
- OpenBSD FAQ 15: https://www.openbsd.org/faq/faq15.html
- OpenBSD ports: https://www.openbsd.org/faq/ports/ports.html
- OpenBSD pkg_create(1): https://man.openbsd.org/pkg_create.1
- FreeBSD pkg reference: https://www.freebsdsoftware.org/blog/freebsd-pkg-reference.html
- FreeBSD pkg-create(8): https://man.freebsd.org/cgi/man.cgi?query=pkg-create&sektion=8

---

# 2026-08-17 RDAP: Hush chat UI as a Progressive Web App

## Scope locked

- **Primary goal:** The HTML UI already served by `hush-relay` (`GET /`) is a real, installable PWA: add-to-home-screen / install as app on Chromium (desktop + Android) and Safari iOS Add to Home Screen.
- **Non-goals:** Push notifications, Web Push keys, background sync, a separate Node/Vite frontend, changing the Nostr wire protocol, HTTPS termination inside hush-relay, offline posting, account auth.
- **Success / DoD:**
  1. `GET /` HTML contains `<link rel="manifest">`, theme-color, apple-touch-icon, and `navigator.serviceWorker.register("/sw.js")`.
  2. `GET /manifest.webmanifest` is JSON with `name`/`short_name`, `start_url`, `display: standalone`, icons 192 and 512 PNG.
  3. `GET /sw.js` is JavaScript with `install`/`activate`/`fetch` handlers. `/api/*` is not intercepted.
  4. `GET /icon-192.png` and `GET /icon-512.png` return PNG (`\x89PNG`).
  5. `make && make test` still pass under `-Werror`.
  6. `curl` against a running `hush-relay` returns 200 for all PWA routes.
- **Constraints:** C11 + write-legible-c; single binary; no Tailwind-in-C; no new runtime deps; worktree `gb/pwa`; land via PR. Secure context = localhost / 127.0.0.1 (existing bind) or operator HTTPS reverse proxy.
- **Assumptions:** Users install from the relay origin they already open (`http://127.0.0.1:<port>/`). Existing 256×256 launcher PNG is a valid source for 180/192/512. Service worker is still the reliable Chromium install path even if Lighthouse no longer lists it.
- **Environment:** gcc, gmake, POSIX, Python3+PIL (dev only, to rasterize icons), ffmpeg available as fallback. No Node required.
- **Risks:**
  1. SW caches stale UI after binary upgrade → cache name `hush-ui-v1` + network-first static + skipWaiting/clients.claim.
  2. `(void)write` class of `-Werror` bugs on new routes → reuse `hush_http_write_all`.
  3. Huge generated headers if we embed uncompressed assets → PNG from 256px source stays small.
  4. Preview host is not localhost → install prompt may be blocked; functionality still works.
  5. `hush_http_serve` grows past 40 lines → extract static asset dispatch.

## Findings

### Installability (MDN + Chrome Lighthouse, 2025–2026)

Chromium installable manifest requires:

- `name` or `short_name`
- `icons` including **192×192** and **512×512**
- `start_url`
- `display` in `{standalone, fullscreen, minimal-ui}`
- `prefer_related_applications` not `true`

Served over **HTTPS**, or **localhost / 127.0.0.1** (with or without port).

A service worker is **not** listed on the current Lighthouse installable-manifest page, but a controlling SW with a `fetch` handler remains the conservative path for “Add to Home Screen” and offline shell. Safari iOS uses Share → Add to Home Screen and ignores `beforeinstallprompt`.

Manifest MIME: `application/manifest+json` (fallback `application/json` also parsed). Link: `<link rel="manifest" href="/manifest.webmanifest">`.

Apple: `<meta name="apple-mobile-web-app-capable" content="yes">`, `<link rel="apple-touch-icon" href="/apple-touch-icon.png">` (180×180).

### Current Hush UI (code)

- Single file `hush-c/demo/index.html` (~239 lines), inline CSS/JS, no manifest, no SW.
- Embedded at build by `scripts/embed-ui.sh` → `src/hush_ui_html.h` → `HUSH_UI_HTML[]`.
- `hush_http_serve` only routes `/`, `/index.html`, `/api/status`, `/api/events`, POST `/api/event`.
- Icons exist at `assets/icons/{48,128,256}/hush-relay.png` only — **no 192/512**.
- Relay listens `INADDR_ANY`, default port **10555**, prints `http://127.0.0.1:<port>/`.
- UI uses `fetch("/api/...")` when not `file:`.

### Architecture decision (locked)

Keep the **single-binary, embed-at-build** model. Do **not** read the filesystem at runtime (breaks install prefixes and the desktop launcher).

| Route | Type | Source |
|---|---|---|
| `/`, `/index.html` | text/html | `demo/index.html` |
| `/manifest.webmanifest` | application/manifest+json | `demo/manifest.webmanifest` |
| `/sw.js` | application/javascript | `demo/sw.js` |
| `/icon-192.png` | image/png | `demo/icons/icon-192.png` |
| `/icon-512.png` | image/png | `demo/icons/icon-512.png` |
| `/apple-touch-icon.png` | image/png | `demo/icons/apple-touch-icon.png` |
| `/api/*` | unchanged | not cached by SW |

`scripts/embed-ui.sh` becomes a multi-asset generator writing `src/hush_ui_html.h` (text constants + `unsigned char` blobs + lengths).

SW policy: precache shell; **do not** `respondWith` for `/api/*` (live relay data); network-first for documents so upgrades win; fallback to cache when offline.

### Tailwind

Existing RESEARCH opted Tailwind out of C. Current `index.html` is hand-written CSS (the Tailwind-CDN note is stale). PWA work does not introduce Tailwind.

## Remaining plan

See `PLAN_PWA.md`.

---

# 2026-08-17 RDAP: First-launch UX (Nostr login, community/vibe, project, channel, Sgt Major Payne)

## Scope locked

- **Primary goal:** When the Hush PWA first launches it detects login state, walks the human through the same *login sequence Buzz uses* (identity key → backup → community), then lands them in a useful hive: create a vibe (this relay), create channels, create a project (basic git), and meet **Sgt Major Payne** as the first agent.
- **Non-goals:** Full NIP-42 AUTH challenge loop; NIP-07 browser-extension signing as the only path; hosted multi-tenant community SaaS; running an LLM for Payne; NIP-90 Data Vending Machines (upstream **unrecommended**); Tailwind; a Node/Vite frontend; persisting the in-memory event ring; Schnorr-verify of every EVENT (existing stub stays).
- **Success / DoD:**
  1. Cold PWA load with no identity shows a first-launch gate, not the chat composer.
  2. Human can **create** a Nostr identity (secp256k1) or **import** an `nsec1…` key. Public form is `npub1…` (NIP-19).
  3. Fresh-key path shows the nsec once, masked by default, with Copy + the exact `pass` checkbox: `Check here to save the nsec in the local password manager, \`pass\``.
  4. After identity, human must have (or create) a **vibe** — the primary endpoint / this relay — before the hive UI.
  5. First vibe seeds `#general`, `#welcome`, `#agents` and **Sgt Major Payne**.
  6. From the hive, human can create another channel, create a project (NIP-34 kind 30617 + optional `git init`), and see Payne on deck.
  7. `GET /api/session` reports `{logged_in, npub, vibe, agents, channels, projects}`.
  8. `make && make test` pass under `-Werror`. Bech32 vectors from NIP-19 pass.
- **Constraints:** C11 + write-legible-c; single binary + embedded PWA; OpenSSL for secp256k1; `pass` for optional secret persist; worktree `gb/first-launch-ux`; land via PR. Hush is **not Buzz** — we copy the *sequence and security contract*, not Tauri/React.
- **Assumptions:** Local-first: this `hush-relay` process *is* the vibe. Community = host (NOSTR.md). Browser never keeps nsec after the backup step; relay holds the in-process identity; `pass` is the durable store.
- **Environment:** gcc, OpenSSL 3 (`libcrypto`, secp256k1), `pass` optional, `git` optional for projects.
- **Risks:**
  1. Shipping fake keys (hash-as-pubkey) would break Nostr interop → real secp256k1 via OpenSSL.
  2. Showing nsec in the DOM by default → mask + reveal + copy, same as Buzz `NsecMaskedDisplay`.
  3. `hush_http.c` growing past 40-line functions → new `hush_launch` / `hush_identity` / `hush_bech32` modules; HTTP only dispatches.
  4. Relay restart loses identity → optional `pass show hush/identity/nsec` restore.
  5. Scope creep into full NIP-29/34/42 → seed + announce only; enforcement later.

## Findings

### Nostr keys (https://nostr.com/nostr-keys + NIP-19)

- Every account is a **secp256k1** keypair. Public key = username (`npub1…`). Private key = password (`nsec1…`). There is no “forgot password”.
- Apps **sign** notes with the private key. Anyone with the nsec *is* the account.
- NIP-19: bech32 (not bech32m) prefixes `npub` / `nsec` / `note`. Hex stays on the wire. Shareable forms (`nprofile`, `nevent`, `naddr`) are TLV and out of this slice.
- Verified vectors (NIP-19):
  - pubkey hex `7e7e9c42a91bfef19fa929e5fda1b72e0ebc1a4c1141673e2794234d86addf4e` → `npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg`
  - secret hex `67dea2ed018072d675f5415ecfaed7d2597555e202d85b3d65ea4e58d2d92ffa` → `nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5`
- NIP-07 (`window.nostr`) is the *browser extension* signer. Useful later; first-launch must work without an extension (Buzz generates or imports).
- NIP-49 `ncryptsec` and NIP-46 remote signing: later.

### Relays (https://nostr.com/relays)

- A relay is a post office, not an identity provider. Hush *is* a relay. The first-launch “community” is this process’s URL (`http://127.0.0.1:<port>/`).
- Several public relays exist (damus, nos.lol, …). This slice does not auto-join the public network.

### NIPs / kinds that map to the product words

| Product word | Protocol | This slice |
|---|---|---|
| Login / identity | secp256k1 + NIP-19 + kind 0 metadata | generate / import / backup / session |
| Community | Hush: host-authoritative (NOSTR.md). NIP-72 unrecommended; NIP-29 groups | **vibe** = this relay named |
| Channel | NIP-29 `#h` + kind 9007 create / kind 9 chat; Buzz starter `#general` + `#welcome-everyone` | seed `general`, `welcome`, `agents` |
| Project | NIP-34 kind 30617 repo announcement + `git clone` URL | name + `git init` under `~/hush/projects/<slug>` |
| Vibe | “main server / primary endpoint” | named local relay (NIP-11-shaped name/about) |
| First agent | Buzz Welcome Team = Fizz / Honey / Bumble | **Sgt Major Payne** (organizer; find or create robots) |
| Auth to relay | NIP-42 kind 22242 | **not** in this slice (SECURITY.md: planned) |
| DVM | NIP-90 **unrecommended** upstream | out of scope |

Hush `NOSTR.md` already documents the Buzz-era NIP-29/42/43 surface as *intent*. The C MVP store is still kinds 0/1/5/7/9 over HTTP + newline JSON. First-launch writes those kinds (and 30617 as a stored event) without claiming full NIP-29 enforcement.

### What Buzz actually does on first launch (code, `/opt/repo/buzz`)

Boot gates in `desktop/src/app/App.tsx`:

1. **Machine onboarding** (`useMachineOnboardingState` / `MachineOnboardingFlow`)
   - Pages: `identity` → (`key-import` **or** create) → `backup` → `setup` (runtimes) → `config`.
   - Create: `getIdentity()` / `persistCurrentIdentity()` — secp256k1 via `nostr::Keys::generate()`, nsec in OS keychain + optional `identity.key`.
   - Import: `NostrKeyImportForm` — paste or drop `nsec1…`, live `npub` preview, masked input.
   - Backup: `BackupStep` + `NsecMaskedDisplay` — nsec not in the DOM until Reveal; Copy; “never share”.
   - Help: “What’s an identity key?” — you own it, no password reset, create new or use existing Nostr key.
   - Lost keyring → land on import. Locked → `KeyringLockedScreen`.
2. **Community** (`useCommunityInit` / `WelcomeSetup`)
   - No community → “Join or create a community” / “I already have a community”.
   - Community *is* a relay URL. Auto-connect only for internal default-relay builds.
   - Membership: NIP-43 snapshot (`getMyRelayMembershipLookup`); denied / unreachable / ok.
3. **Profile + welcome seed** (`useFirstRunOnboardingGate`, `welcome.ts`, `welcomeGuide.ts`)
   - Skip onboarding if a real kind:0 exists for this pubkey (or localStorage `buzz-onboarding-complete.v1:<pubkey>`).
   - Ensure starter channels `#general` (open) + `#welcome-everyone` (open) + private `Welcome`.
   - Seed Welcome Team agents (Fizz lead, Honey, Bumble).

Hush will **not** port Tauri, keychain, ACP runtimes, or the three-bee team. We keep the **order and the safety copy**.

### Current Hush UI (code)

- `hush-c/demo/index.html` is an installable PWA chat shell. Hard-coded channels. Posts as pubkey `000…001`. **No login gate.**
- HTTP: `GET /`, PWA assets, `GET /api/status`, `GET /api/events`, `POST /api/event`.
- Secrets contract already in `IMPORT.md` / `SECURITY.md`: `pass` namespace `hush/agents/<name>/nsec`; checkbox label is mandatory when offering to save a secret. Helper `scripts/hush-pass` is documented but **missing** — add it.
- SHA-256 / Schnorr still stubbed (`hush_event.c`). Identity generation is a *new* crypto path and must be real.

### Architecture decision (locked)

Keep the single-binary embed model. First-launch is **client state machine in the PWA** + **authoritative session in the relay**.

| Route | Role |
|---|---|
| `GET /api/session` | login + vibe + agents + channels + projects snapshot |
| `POST /api/identity` | `create` / `import` / `ack_backup` / `logout` |
| `GET`/`POST /api/vibe` | name this relay (community) |
| `POST /api/channel` | add a hive channel (`#h`) |
| `POST /api/project` | NIP-34-shaped announce + optional `git init` |
| existing `/api/event(s)` | chat; posts use the session pubkey when logged in |

Modules (new):

- `hush_bech32` — NIP-19 encode/decode
- `hush_identity` — secp256k1 generate/import, nsec/npub, optional `pass`
- `hush_launch` — session, vibe, Payne, channel/project catalogs
- PWA gate screens in `demo/index.html` (hand CSS, no Tailwind)

Sgt Major Payne is a **seeded agent record** (name, about, pubkey of a generated agent key stored under `hush/agents/sgt-major-payne/nsec` only if the human checked `pass`). He is not an LLM in this slice; the UI presents him as the organizer who will find or create the right robot.

### Tailwind

Still opted out of C. First-launch UI continues hand-written CSS to match the existing PWA.

## Remaining plan

See `PLAN_FIRST_LAUNCH.md`.

---

# 2026-08-17 RDAP: Default-on Unix `pass` for identity keys

## Scope locked

- **Primary goal:** Whenever Hush generates or first-shows an nsec (or any other secret), a modal presents the value to copy, with a **checked-by-default** checkbox that saves it via the unix password manager `pass`. The human must physically uncheck to opt out. Retrieve CLI is shown in the modal.
- **Non-goals:** Replacing `pass` with another manager; GPG key generation; OS keyring; NIP-49 `ncryptsec`; persisting the in-memory event ring; rewriting first-launch beyond the backup/import secret modal; requiring `pass` to create an identity.
- **Success / DoD:**
  1. Fresh-key backup modal shows the nsec (masked by default) + Copy.
  2. Checkbox is **checked by default**. Label includes retrieve CLI: `pass show hush/identity/nsec` (or `hush-pass get identity/nsec`).
  3. Leaving it checked on “I saved it” writes `hush/identity/nsec` via `scripts/hush-pass` / `pass insert`.
  4. Unchecking skips `pass` entirely.
  5. Import and Payne-agent secrets use the same contract (default-on, opt-out by uncheck).
  6. Missing / uninitialized `pass` never blocks identity creation; UI reports the failure and still offers copy.
  7. `make && make test` pass under `-Werror`. New unit + route tests cover save / skip / missing-pass.
- **Constraints:** C11 + write-legible-c; worktree `gb/pass-identity-keys`; land via PR. Never write `main`.
- **Assumptions:** `pass` is the durable store. Browser never keeps nsec after ack. Relay process holds in-memory identity. Tests must not require a live GPG agent (injectable backend).
- **Environment:** `pass` v1.7.4 and `gpg` are on PATH. This machine’s `~/.password-store` has no `.gpg-id` (`pass insert` currently fails until `pass init`). Tests therefore use a stub backend.
- **Risks:**
  1. Calling real `pass` from unit tests hangs on GPG pinentry → injectable command + stub script.
  2. Putting nsec on `argv` of `pass insert` leaks via `ps` → pipe stdin, never argv.
  3. Hard-failing create when `pass` is missing → save is best-effort; identity still succeeds.
  4. `hush_http.c` / `hush_launch.c` already near complexity budget → new `hush_pass` module; HTTP only parses `save_pass`.

## Findings (code, this tree)

### What exists

- `scripts/hush-pass` wraps `pass` under namespace `hush/<path>`. Commands: `save|get|has|ls`.
- Bug: `save` with a third argv value uses `"${3+x}"` then pipes `$3`, but **never passes `-f`**, so a second save of the same path prompts and can hang.
- Docs (`IMPORT.md`, `SECURITY.md`, `RESEARCH.md` first-launch) already name `pass` as the store and prescribe the checkbox.
- PWA backup card in `hush-c/demo/index.html` already has the checkbox, **unchecked**, and **never reads it**. `ack_backup` posts `{action:"ack_backup"}` only.
- `POST /api/identity` (`hush_http_serve_identity`) handles `create` / `import` / `ack_backup`. No `save_pass` field. Import auto-acks backup and never offers the modal.
- `hush_identity` generates/imports secp256k1 keys. It does not talk to `pass`.
- `hush_launch_create_vibe` generates Payne’s key in-process and never stores it.
- Relay start (`hush_relay_run`) does not restore identity from `pass`.
- `hush-pass` is not installed by `make install` / DEB / RPM.
- No `docs/pass-integration.md` (IMPORT.md links it).
- `check_launch.sh` only greps the checkbox string in HTML source. It never posts `save_pass` and never inspects a store.

### Contract (locked)

| Secret | `pass` path | Retrieve CLI |
|---|---|---|
| Human identity nsec | `hush/identity/nsec` | `pass show hush/identity/nsec` |
| Agent nsec | `hush/agents/<slug>/nsec` | `pass show hush/agents/<slug>/nsec` |
| Other token | `hush/<category>/<name>` | `pass show hush/<category>/<name>` |

Helper equivalents:

```
hush-pass save identity/nsec
hush-pass get  identity/nsec
hush-pass has  identity/nsec
```

**Checkbox (default checked):**

`Checked to save password to Unix Password Manager. Retrieve with: pass show hush/identity/nsec`

(For agents, the retrieve path is `pass show hush/agents/<slug>/nsec`.)

**Modal:** the existing first-launch backup card *is* the modal. Import also gets the same backup card (do not auto-ack). Any later secret-generation UI reuses the same pattern.

**Default:** checked. Uncheck = do not call `pass`. Copy is always available.

**Failure:** if `pass` is missing or the store is uninitialized, identity still succeeds; session JSON reports `pass_saved:false` and a short `pass_error` string. Human still has Copy.

### Architecture (locked)

New module `hush_pass` (C11, write-legible-c §15):

- `hush_pass_save(path, secret)` — pipes secret to `hush-pass save <path>` (or `pass insert -e -m -f hush/<path>`).
- `hush_pass_get(path, out, outsz)` / `hush_pass_has(path)`.
- `hush_pass_set_helper(cmd)` — test seam (default: `hush-pass` on PATH, else `scripts/hush-pass` relative to cwd).
- Never put the secret on argv. Never log it.
- Missing helper / non-zero child → `HUSH_ERR_IO`, no abort of identity.

Launch / HTTP:

- `hush_launch_ack_backup(launch, save_pass)` — if `save_pass`, persist human nsec under `identity/nsec`.
- Import no longer auto-acks; import lands on the same backup modal (checkbox default on).
- `POST /api/identity` `{action, nsec?, save_pass?}`. Absent `save_pass` on ack = **true** (server-side default matches UI).
- Session JSON grows `pass_saved` (bool) and `pass_error` (string, empty on success).
- On vibe seed, persist Payne under `agents/sgt-major-payne/nsec` using the same default-on flag carried from the backup step (`launch->save_pass`).
- Optional restore: if process starts with no identity and `hush_pass_has("identity/nsec")`, import it and mark backup already acked (no modal). Soft-fail if `pass` absent.

PWA:

- Checkbox `checked` by default. Label includes retrieve CLI.
- `ack_backup` and import-ack send `save_pass: <checkbox.checked>`.
- Import goes to backup page (do not skip).
- Surface `pass_error` if save failed.

Tests:

- `tests/test_pass.c` with a stub helper that writes under `$TMPDIR`.
- `check_launch.sh` asserts `checked` attribute + retrieve CLI string.
- Route test: ack with `save_pass:true` against stub (or skip real pass).
- `scripts/hush-pass` gains `-f` on insert so re-save does not prompt.

## Remaining plan

See `PLAN_PASS_IDENTITY.md`.

---

# 2026-08-17 RDAP: STUN/TURN (coturn), vibe visibility, conference calling

## Scope locked

- **Primary goal:** Let a Hush operator enable a local STUN/TURN server from Settings so voice/video conference calls (human↔human, many humans, human↔agent, agent↔agent, mixed) can traverse NAT. A vibe is public (discoverable + joinable) or private (not).
- **Non-goals:** Vendoring the coturn source tree; embedding Whisper.cpp; building an SFU; TURN TLS/DTLS certificates in this slice; NIP-42 AUTH; replacing the first-launch identity flow; Tailwind.
- **Success / DoD:**
  1. `./configure` has `--enable-stun-turn` (default) and `--disable-stun-turn`.
  2. Settings has an **Enable STUN/TURN** control that writes a coturn config and starts `turnserver` (child of `hush-relay`).
  3. **Daemon mode** installs/starts a systemd unit. `sudo make install PREFIX=/usr` (or equivalent) is required for the system unit; documented.
  4. `GET /api/ice` returns WebRTC `iceServers` (STUN + long-term TURN credentials).
  5. A vibe has `visibility: public|private`. Public is discoverable/joinable; private is not listed and needs the join token.
  6. PWA can start a mesh conference on a channel (offer/answer/ICE via kind 25000). Participant roles: `human` | `agent`.
  7. AI voice is gated: `/api/status` reports `whisper`. Without a speech model (Whisper or equivalent) agents join as text/signaling-only.
  8. `make && make test` pass under `-Werror` with STUN/TURN compiled in *and* with `--disable-stun-turn`.
- **Constraints:** C11 + write-legible-c (fn ≤40 lines, depth ≤2, named literals, no recursion/goto). Single binary + embedded PWA. coturn is a **runtime** dependency (`turnserver` on PATH), not a link-time library. Worktree `gb/stun-turn`. Land via PR only.
- **Assumptions:** One vibe per relay process (already true). Conference is a **mesh** (each peer talks to each other peer). Home deployments can live with a small relay port range (49152–49251). Open TURN is abuse-prone → always generate credentials.
- **Environment:** gcc, OpenSSL (`-lcrypto` already required for identity), optional `turnserver` (coturn), optional `systemctl`, no apt in some sandboxes.
- **Risks:**
  1. Open/unauthenticated TURN becomes a DDoS relay → long-term `user=` creds, never empty.
  2. Binding :3478 needs root or `CAP_NET_BIND_SERVICE` → child mode falls back to 13478.
  3. `hush_http.c` / `index.html` already large → new `hush_turn` module; HTTP only dispatches; UI overlays.
  4. Missing `turnserver` must not fail the build or crash the relay → compile-time flag + runtime `have_binary`.
  5. Conference without ICE still works on LAN; TURN is for NAT.

## Findings

### coturn (https://github.com/coturn/coturn)

- coturn is a TURN **and** STUN server (RFC 8489 STUN, RFC 8656 TURN).
- Not a library we should vendor. Run the `turnserver` binary. Config file or CLI.
- Daemon: `turnserver --daemon` / `-o`. systemd: Debian ships `coturn.service`; Hush ships its own `hush-turn.service` so we own the config path (`/etc/hush/turnserver.conf`) and do not fight a distro coturn instance.
- WebRTC requires `--lt-cred-mech` (long-term credentials). REST API (`--use-auth-secret` + HMAC-SHA1 timestamp usernames) is nicer for multi-tenant; a **single-operator self-host** is simpler with one generated `user=hush:<secret>` written into the conf and returned from `/api/ice`.
- Useful knobs: `listening-port` (3478), `min-port`/`max-port` (relay UDP), `realm`, `fingerprint`, `external-ip` when behind NAT, `no-cli`, `no-tls`/`no-dtls` until certs exist, `no-multicast-peers`, `pidfile`, `log-file`.
- Runtime ports: 3478/tcp+udp (signaling), plus the relay range. Document firewall.

### Why Hush does not embed coturn

- coturn depends on libevent2, optional OpenSSL, DB backends. Linking it would explode the C11 single-binary contract and the no-system-package sandbox.
- Process isolation is a feature: a TURN crash must not take down the relay.
- License: coturn is BSD-like; we generate config and exec. No source copy.

### Vibe visibility (maps onto existing first-launch “vibe”)

RESEARCH §2026-08-17 first-launch already locked: **vibe = this relay / primary endpoint**. Do **not** invent a second vibe object.

SECURITY.md already states the intended ACL: private channels are invisible to non-members. This slice applies the same idea to the **vibe itself**:

| visibility | discoverable | joinable |
|---|---|---|
| `public` | yes (`session.vibe.discoverable=true`) | anyone who can reach the relay |
| `private` | no | only with `vibe.join_token` |

Enforcement is honest-MVP: we hide listings and require the token on session/join APIs. Full NIP-42 AUTH is still later.

### Conference topologies

WebRTC mesh on a channel, signaling over existing store (kind **25000**, `#h` = channel):

| Mix | How |
|---|---|
| Human ↔ human 1:1 | 2 peer connections |
| Many humans | full mesh, N(N−1)/2 |
| Agent ↔ agent 1:1 | same; both `role=agent` |
| Human ↔ agent 1:1 | same |
| Human + many agents | mesh; agents need speech I/O |
| Many humans + many agents | mesh; cap peers (8) |

Signaling JSON in event content: `{t:offer|answer|ice|join|leave, from, to, role, sdp?, candidate?}`.

SFU is a later milestone if N grows.

### Whisper / AI voice

- WebRTC carries **media**. An AI agent that *speaks* needs STT (Whisper-class) + a conversational model + TTS.
- This slice **does not** vendor Whisper. `GET /api/status` includes `"whisper":false` unless `HUSH_WHISPER=1` or a `whisper` binary is on PATH.
- Agents can still **join** a call (signaling + optional playback). Without a speech model they do not claim to hear.
- Browser `SpeechRecognition` is an optional demo fallback and is labeled as such — not a substitute for Whisper.

### Configure / install

```
./configure                  # ENABLE_STUN_TURN=1 (default)
./configure --disable-stun-turn
./configure --enable-stun-turn
```

`config.mk` writes `ENABLE_STUN_TURN`, `TURNSERVER` (path or empty), and `-DHUSH_STUN_TURN=0|1`.

`sudo make install PREFIX=/usr` installs:

- `hush-relay` as today
- `$(DATADIR)/hush/turnserver.conf.in`
- `$(DATADIR)/hush/systemd/hush-turn.service`
- `/lib/systemd/system/hush-turn.service` (system unit; does **not** enable itself)

`make install` to `~/.local` installs the templates only and prints that daemon mode needs a system install.

Debian `Recommends: coturn`.

### Architecture (locked)

| Piece | Role |
|---|---|
| `hush_turn` | find binary, write conf, spawn/kill child, systemd start/stop, ICE JSON |
| `hush_launch` | vibe `visibility` + `join_token` |
| `hush_http` | `GET/POST /api/turn`, `GET /api/ice`, `POST /api/signal`; extend session + status |
| `contrib/systemd/hush-turn.service` | Type=simple, `turnserver -c /etc/hush/turnserver.conf --no-cli` |
| PWA Settings | Enable STUN/TURN, Daemon mode, public host, vibe visibility |
| PWA Call | mesh conference on the current channel |

Do **not** add Tailwind. Hand CSS matching the existing PWA.

## Remaining plan

See `PLAN_STUN_TURN.md`.


---

# 2026-08-18 RDAP: Splash, Onboarding Wizard, Profile/Settings, Messaging UI for Human+Agent Teams (Sgt Major Payne)

## Scope Locked (Primary Goal)
Deliver a researched, disciplined UI:
- **Splash / intro**: Brief loading phase that detects existing identity + vibe + config. Shows Hush branding + "Sgt Major Payne" presence.
- **No-config path**: Clear linear onboarding wizard (Identity → Backup/pass (default on) → Vibe/Team name → Channels/Agents seed → Enter hive).
- **Config path**: Auto-login/resume to messaging UI (or minimal gate).
- **Always-reachable controls**: Profile (view npub, logout, manage identity), Settings (STUN etc + new UI prefs), Login/Import from header or splash.
- **Core UI**: Traditional messaging system for collaboration — channels (teams/squads), message stream with author roles (you / Payne / human / agent), agent roster with Sgt Major Payne prominent, composer, presence indicators.
- Messaging as the primary collaboration surface for mixed human + agent teams.

Non-goals (Parker filter):
- Full NIP-42 auth challenge loop (later).
- Rich threading / reactions MVP.
- External LLM wiring for Payne (he is seeded persona + future DVM).
- Feline branding or Alfred; use disciplined "Sgt Major Payne" voice.
- Tailwind in C; keep hand CSS + existing PWA shell.

Success / DoD (measurable):
1. Launch shows splash (detect state) <1s perceived.
2. Fresh run: wizard steps complete → ready hive with Payne seeded.
3. Restored run (pass + vibe): direct or 1-click to hive; no dead-end "no buttons".
4. Header always exposes: Profile, Settings, (Login if needed), Install PWA.
5. Messaging UI: sidebar (Channels + Agents/Teams), main stream (notes with meta), composer, Payne card + invite.
6. Low cog load (Quinn): ≤5 primary actions visible; consistent nav; recognition over recall.
7. `./configure && make && make test` + check_launch.sh + curl smoke pass.
8. All per write-legible-c for C (if touched); worktree commits + PR gate.

## Research Synthesis (Quinn + Parker + Payne essence from catfu + Hush)

### From catfu specialists (adapted, non-feline)
- **ui-quinn** (Nielsen, UCD, Atomic, Hick/Fitts, Gestalt):
  - Cognitive Load Index target ≤3/10. Gestalt Clarity ≥85.
  - Few choices (Hick): primary rail = Channels | Agents | Projects (not 4+).
  - Large targets, proximity for related actions, consistent language.
  - Recognition: icons + short labels + keyboard hints.
  - Error prevention: confirm before destructive (logout).
- **product-parker** (JTBD, Lean, RICE, Hook):
  - JTBD: "Collaborate with my team (humans + agents) using a familiar messaging interface to plan, assign, and ship work."
  - MVP: channels as squads, messages as work notes, Payne as first agent who "finds or raises the robot".
  - RICE high on clarity + speed to first message; low on bloat.
  - No addiction hooks; utility only.
- **caretaker-alfred → Payne**:
  - Human male mentor. Precise, warm discipline. "At ease.", "Mission first.", one directive/tip per major screen.
  - Seeds #welcome with Payne note. Always present in Agents roster.
  - Every major flow ends with a Payne-flavored line (not "Tip:" literally; "Carry on." or "Report when ready.").
- **catfu-brand**:
  - Enforce: disciplined, no fluff, military-adjacent but not parody. Lowercase "hush" in UI where product name. Payne as mascot/agent.
  - No "Brave", no vendor leakage. "hush" not "Hush" in prose unless title.

### Hush current state (code + run)
- PWA served from C via embed (hush_http_serve_asset + scripts/embed-ui.sh). index.html has gate state machine (landing → create/import → backup (secret modal + pass checkbox default true) → vibe → hive).
- Session /api/session drives everything: logged_in, backup_acked, has_vibe, ready, channels, payne, vibe.
- On relay start: hush_launch_restore_identity (pass) → if ready, hive shows.
- Problem reported: after launcher click, user sees PWA but "no onboarding window or button for login/profile/settings". Likely hit restored path or header hidden until ready. Gate exists but not surfaced as distinct splash.
- Payne already in launch (HUSH_LAUNCH_PAYNE_NAME, seeded profile + welcome note).
- Messaging: flat #channels + stream + basic call/settings drawers. No persistent agent roster in main nav. Profile implicit (npub in session).

### Nostr / messaging patterns for collab
- Kinds 0 (profiles for humans/agents), 1 (chat notes tagged #h=channel).
- Agents as first-class: separate roster, special "who" rendering ("Payne"), invite to calls (role=agent).
- Traditional: sidebar channels (like Slack/Discord teams), message list with author badges, composer at bottom, thread later.
- Vibe = the relay instance (named community).

### PWA / splash best practices (from MDN + existing)
- Splash can be the initial gate render or a brief "connecting…" overlay while first /api/session + /api/events tick.
- Detect config: session.logged_in && backup_acked && has_vibe → ready.
- Onboarding as wizard: numbered steps or progressive cards to reduce overwhelm (Quinn).
- Always header controls: use CSS to show Profile/Settings even on gate (or persistent top bar).

## Architecture Decisions (locked for this slice)
- UI remains single-file demo/index.html + CSS + vanilla JS (embedded). No new frameworks.
- State: drive from /api/session (authoritative). Client paints splash → wizard → hive.
- New surfaces:
  - Splash: loading + brand + "Sgt Major Payne reporting" + auto-advance or "Begin".
  - Wizard: 4 clear steps (use progress dots or numbered). Re-uses existing cards but linear.
  - Profile drawer/modal: npub display, copy, logout, re-import, about human.
  - Agents roster (sidebar section): Payne always top; "Invite agent" later.
  - Polish messaging: author pills ("you", "Payne", "agent", short pk), timestamps relative, empty states with Payne directive.
- Header: always visible brand + Install + Profile + Settings + (Call if ready). Gate can have slim header.
- C changes: minimal. If any (e.g. new session fields), apply legible-c + checklist. Prefer HTML/JS for flows.
- Embed after edits: scripts/embed-ui.sh demo ; rebuild.
- Persona copy: Payne voice in empty states, welcome, buttons where natural.

## Risks + Mitigations
1. State machine complexity → single source of truth in paint()/applySession(); test via check_launch + manual curl.
2. Embed drift → always run embed + make after HTML changes; add to check.
3. User sees "nothing" on restore → ensure splash/hive has profile/settings buttons unconditionally.
4. Cognitive bloat → strict Hick cuts: no more than 5 nav items; fold advanced in Settings.
5. Legible C violation → do not edit .c/.h unless necessary; if do, load legible-c and run checklist before commit.

## Updated Plan (RDAP)
See new or updated plan file + milestones below. Commit gate ends Phase 1.

## Verification Performed (this research)
- Built + ran cold + post-create flows (session JSON, HTML gate presence).
- Read catfu recipes + SKILLs (essence extracted).
- Inspected launch.c, http.c, relay.c, demo JS state machine, check_launch.sh.
- Confirmed Payne seeding, pass default-on, vibe, channels.
- Confirmed no splash overlay currently (gate is the on-ramp).

References: catfu .goose/recipes + .agents/skills (fetched), RESEARCH prior sections, PLAN_*.md, NOSTR.md, hush_launch.h, demo/index.html full, Quinn/Parker/Alfred patterns.

