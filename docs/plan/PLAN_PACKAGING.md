# PLAN_PACKAGING: Omarchy (OPR + AUR) and LCOS

Status: DRAFT plan. Docs only. Nothing was packaged, pushed to the AUR, or run in a VM. The only build was one staged `make install DESTDIR=` on the box, for P7.
Goal (Chuck, 2026-10-17 13:50 ET): make Hush easy to install with a package manager on Omarchy and on LCOS.
Ruling (13:52 ET): ship both Omarchy channels as primary targets: the Omarchy Package Repository (OPR) and the AUR. LCOS is the third target.
Ruling (14:19 ET): the package names are `hush-relay` and `hush-relay-git`, on the AUR and the OPR (**USER_SPECIFIED**, Chuck).
Revision 2 (fix-forward for Gauge FAIL @a84a5eda): adds P7 (TURN credential and service user, #353) and P8 (hooks signal no one), BSD VM runs, a home for the keys, the re-fetched O14, and the SPDX finding. See §8.
Repo pin: `coldcanuk/hush` main @ `fa56420a8199c9c676b803faf48f34b0ea994675`. `file:line` cites refer to that commit.
Upstream pins: `basecamp/omarchy` (now `omacom/omarchy`) @ `5fcdaca6`, `omacom-io/omarchy-pkgs` (now `omacom/omarchy-pkgs`) @ `c8a524ab`, `BryanLunduke/LCOS` @ `eded2c8a`.

Labels:
- **OBSERVED**: read in a file, page, or API response at the cited URL or pin.
- **INFERRED**: reasoned from what was observed. Not executed.
- **UNKNOWN**: not established.
- **USER_SPECIFIED**: decided by Chuck; not a research finding.
- **AMBIGUOUS**: the sources disagree.

## 0. Summary

| Target | Channel | User command | Who publishes | Blockers |
|---|---|---|---|---|
| Omarchy | OPR `[omarchy]` repo (binary, signed by Omarchy) | `omarchy pkg add hush-relay`, or *Install > Package* | PR to `omacom/omarchy-pkgs`; their CI builds, signs and publishes | signed release tag; build-approval label; inclusion criteria UNKNOWN; P7 (#353) |
| Omarchy and any Arch | AUR `hush-relay` (release) and `hush-relay-git` | `yay -S hush-relay`, or *Install > AUR* | Chuck's AUR account (SSH push) | the names `hush` and `hush-git` are taken; signed release tag; P7 (#353) |
| LCOS | `.deb` from a GitHub Release, later a Hush apt repo | `sudo apt install ./hush-relay_<v>_amd64.deb` | Chuck (Release asset or signed apt repo) | `debian/control` deps (#352); P7 (#353); no documented third-party submission path |

An official Arch `[extra]` package is not realistic now. See §2.7.

## 1. Omarchy channel (OPR): findings and steps

### 1.1 Findings

| # | Fact | Class | Source |
|---|---|---|---|
| O1 | Omarchy is Arch-based. Its `pacman.conf` uses Arch `[core]`/`[extra]`/`[multilib]` via `stable-mirror.omarchy.org/$repo/os/$arch`, and the manual calls it the "Omarchy Arch Mirror". | OBSERVED | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/default/pacman/pacman-stable.conf , https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/default/pacman/mirrorlist-stable , https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/manual/30-updates.md |
| O2 | An Omarchy-run pacman repo exists. `[omarchy]` (`Server = https://pkgs.omarchy.org/stable/$arch`) is listed before `[core]`. It has edge, rc and stable channels for x86_64 and aarch64. | OBSERVED | same `pacman-stable.conf`; https://github.com/omacom/omarchy-pkgs/blob/c8a524abb172b5f92e89da839907d133cced4baf/README.md |
| O3 | *Install > Package* runs `omarchy-pkg-install`, which lists `pacman -Slq` (every sync repo, `[omarchy]` included) in fzf and then runs `sudo pacman -S`. | OBSERVED | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/bin/omarchy-pkg-install , `default/omarchy/omarchy-menu.jsonc` (`install.package`) |
| O4 | So an OPR package appears in *Install > Package* and in `omarchy pkg add <name>` with no menu change. | INFERRED | O3, `bin/omarchy-pkg-add` |
| O5 | The OPR takes outside PRs. The flow is: fork, `bin/add-package <pkg> --local --scaffold` (or `--source aur` to import once), `pkgbuilds/<pkg>/PKGBUILD` plus `.omarchy/package.json`, then one package per PR against `master`. "No secrets or access are required." | OBSERVED | omarchy-pkgs `README.md` §"Contribute a package" |
| O6 | OPR rules: every git source must be pinned (`#commit=` or `#tag=`); fill sums with `updpkgsums`; list every arch in `arch=()` (CI builds each); declare an upstream watch (`github` Releases or `git_tags`). | OBSERVED | omarchy-pkgs `README.md`, `docs/upstream-sources.md` |
| O7 | PR builds run only for collaborators, authors in `.github/VOUCHED.td`, or a PR a maintainer labels `build-approved`. A green package-only PR auto-merges. It publishes to `edge` on merge and reaches `rc`/`stable` on promotion. OPR signs the published packages with its own key (`GPG_PRIVATE_KEY`). | OBSERVED | omarchy-pkgs `README.md` §"What happens to your PR", §"Who can build and merge"; https://github.com/omacom/omarchy-pkgs/blob/c8a524abb172b5f92e89da839907d133cced4baf/.github/VOUCHED.td |
| O8 | The fast ring (straight to stable) is for vendor binaries, static tools, scripts and fonts. "A package that needs rebuilding when a dependency moves stays off it." Hush links libcrypto, libcurl and libX11, so it would stay off: edge first, stable at the next promotion. | OBSERVED (rule) / INFERRED (Hush) | omarchy-pkgs `README.md` §"What can be on the fast ring" |
| O9 | The OPR already carries third-party apps (e.g. `obsidian`, `typora`, `zed`, `grok-bot`, `localsend`). "Add <pkg>" PRs from non-collaborators have merged (e.g. #1205 by `netmojo`, #568 by `jdvmi00`, both CONTRIBUTOR). | OBSERVED | https://github.com/omacom/omarchy-pkgs/tree/c8a524abb172b5f92e89da839907d133cced4baf/pkgbuilds , https://api.github.com/search/issues?q=repo:omacom/omarchy-pkgs+is:pr+is:merged+add |
| O10 | No written acceptance criteria for new third-party packages: whether a relay/chat app like Hush is accepted is up to the maintainers. | UNKNOWN | omarchy-pkgs `README.md` (mechanics only) |
| O11 | Curated menu rows (e.g. *Install > AI > Grok Bot*) live in `default/omarchy/omarchy-menu.jsonc` in `omacom/omarchy`. The precedent is `"install.ai.grok-bot": {... "action":"omarchy-install-and-launch 'Grok Bot' grok-bot grok-bot"}`, which installs a repo package and launches its desktop id. | OBSERVED | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/default/omarchy/omarchy-menu.jsonc , `bin/omarchy-install-and-launch` |
| O12 | Criteria for adding a row to Omarchy's own menu are not documented. Issue templates send suggestions to GitHub Discussions; issues are for verified bugs only. | UNKNOWN (criteria) / OBSERVED (routing) | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/.github/ISSUE_TEMPLATE/config.yml |
| O13 | A user can add their own menu row with no upstream PR: `~/.config/omarchy/extensions/omarchy-menu.jsonc` overlays the shipped menu (dotted ids, `action`, `disabled` guard). | OBSERVED | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/docs/menu.md , `config/omarchy/extensions/omarchy-menu.jsonc` |
| O14 | Updates: users run *Update > Omarchy* / `omarchy update`. The manual says this updates OPR, mirror and AUR packages (`manual/30-updates.md:5`), and that a direct `pacman -Syu`/`yay -Syu` is stopped by a guard (`:29`). The code agrees: `bin/omarchy-update:127` runs `omarchy-update-system-pkgs` and `:152` runs `omarchy-update-aur-pkgs`, which runs `yay -Sua --noconfirm` when `pacman -Qem` lists foreign packages (`bin/omarchy-update-aur-pkgs:12-15`). Re-fetched 2026-10-17 2:2x PM ET @5fcdaca6. | OBSERVED (docs + code; not run) | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/manual/30-updates.md , https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/bin/omarchy-update , https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/bin/omarchy-update-aur-pkgs |
| O15 | Omarchy preinstalls `chromium`, `base-devel`, `git` and `yay`. | OBSERVED | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/install/omarchy-base.packages |
| O16 | The repos moved: `basecamp/omarchy` now redirects to `omacom/omarchy` (default branch `quattro`), and `omacom-io/omarchy-pkgs` to `omacom/omarchy-pkgs`. | OBSERVED | https://api.github.com/repos/basecamp/omarchy , https://api.github.com/repos/omacom-io/omarchy-pkgs |

### 1.2 Steps (OPR)
1. Prerequisites in this repo: §2.4 fixes P1–P4, plus a GPG-signed release tag `v0.0.2` or later (§2.5). The only tag today is `v0.0.1`, unsigned, on 2026-08-18 code (see §2.5).
2. Fork `omacom/omarchy-pkgs`. Run `bin/add-package hush-relay --local --scaffold`, then paste the §2.2 PKGBUILD (release flavour, pinned `#tag=v$pkgver`).
3. Write `pkgbuilds/hush-relay/.omarchy/package.json`. Hush has tags but no GitHub Releases (`list_releases` returned `[]`, OBSERVED), so use the `git_tags` watch:
   ```json
   { "source": "local",
     "upstream": { "watch": { "git_tags": "https://github.com/coldcanuk/hush.git",
                              "pattern": "v(?P<version>[0-9]+(?:\\.[0-9]+)*)" } } }
   ```
4. Check locally: `tests/pinned-sources.sh`, `python helpers/upstream-watch.py check pkgbuilds/hush-relay`, then `bin/build --package hush-relay` (Docker/Podman). Run these only in a PLAN_VM_TEST VM (§4), never on kiff.
5. Open one PR to `omacom/omarchy-pkgs` `master`: what Hush is and how it was tested. Expect *Awaiting build approval* until a maintainer labels it (O7).
6. Optional, after the OPR merge: a PR (or a Discussions suggestion first, O12) to `omacom/omarchy` adding `"install.ai.hush": {"label":"Hush","disabled":"omarchy-pkg-present hush-relay","action":"omarchy-install-and-launch Hush hush-relay hush-relay"}`. Until then, document the user-level overlay (O13) in Hush's README with the same entry.
7. Users install with `omarchy pkg add hush-relay` or *Install > Package*, and update with `omarchy update`.

## 2. AUR: `hush-relay` and `hush-relay-git`

### 2.1 Findings

| # | Fact | Class | Source |
|---|---|---|---|
| A1 | **The AUR names `hush` and `hush-git` are taken** by unrelated projects: `hush` 1.03-1 (d99kris/hush, maintainer Dominiquini) and `hush-git` (sirrryasir/hush). `hush-relay` and `hush-relay-git` are free. | OBSERVED | https://aur.archlinux.org/rpc/v5/info?arg[]=hush&arg[]=hush-git&arg[]=hush-relay&arg[]=hush-relay-git |
| A2 | pkgnames `hush-relay` and `hush-relay-git`, on the AUR and the OPR. This matches the deb (`debian/control` `Package: hush-relay`) and the RPM (`hush-relay.spec` `Name:`). | USER_SPECIFIED (Chuck 14:19) | A1 (OBSERVED, names free) |
| A3 | Omarchy's AUR helper is `yay`: preinstalled (O15), used by *Install > AUR* (`yay -Slqa`) and by `omarchy-pkg-aur-add` (`yay -S --noconfirm --needed`). `paru` is not in the base list. | OBSERVED (yay) / INFERRED (paru absent) | https://github.com/omacom/omarchy/blob/5fcdaca6495cac8931e0624c88414c95e0247585/bin/omarchy-pkg-aur-install , `bin/omarchy-pkg-aur-add`, `install/omarchy-base.packages` |
| A4 | Write access to the AUR needs an AUR account with an SSH public key in *My Account*. The guidelines say to create a new key pair (`ssh-keygen -f ~/.ssh/aur`) and set a `Host aur.archlinux.org` entry. Push to `ssh://aur@aur.archlinux.org/<pkgbase>.git`. Commit at least `PKGBUILD` and `.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`), plus a package-source license; 0BSD is encouraged. | OBSERVED | https://wiki.archlinux.org/title/AUR_submission_guidelines |
| A5 | A VCS package needs the `-git` suffix. | OBSERVED | same page |
| A6 | `validpgpkeys` takes full uppercase fingerprints. Signature files in `source` are verified automatically. A git source can require a signed tag or commit with `url#tag=...?signed` (git only). | OBSERVED | https://wiki.archlinux.org/title/PKGBUILD#validpgpkeys , https://man.archlinux.org/man/PKGBUILD.5 |
| A7 | Arch path rules: binaries in `/usr/bin`, app data in `/usr/share/<pkg>`, config in `/etc/<pkg>`, never `/usr/local`; the directory table lists `/usr/lib`, not `/lib`. Config files to keep go in `backup=()` as relative paths. | OBSERVED | https://wiki.archlinux.org/title/Arch_package_guidelines , https://wiki.archlinux.org/title/PKGBUILD#backup |
| A8 | `base-devel` (gcc, make, binutils, pkgconf, …) is assumed and must not be listed. | OBSERVED | https://wiki.archlinux.org/title/PKGBUILD#checkdepends , https://archlinux.org/packages/core/any/base-devel/ |

### 2.2 Dependencies derived from the repo

| Need | Repo evidence (OBSERVED) | Arch package |
|---|---|---|
| C11 compiler, make, `ld -r -b binary`, `ar` | `configure:118-126`, `hush-c/Makefile` (`ld -r -b binary`, `ar rcs`) | base-devel |
| python3 (configure refuses without it) | `configure:160` `check_cmd python3`; `:372-375` exits 1 on MISSING | `python` (makedepends) |
| libcurl headers + pkg-config | `configure:250`; `hush-c/Makefile:19-20` `pkg-config --cflags/--libs libcurl` | `curl` (depends), pkgconf (base-devel) |
| OpenSSL libcrypto | `configure:230-246`; `LDFLAGS := -lcrypto` | `openssl` (depends) |
| X11 (optional; window min/max) | `configure:257-263` | `libx11` (depends, so the build is deterministic) |
| git (git source; build stamp) | `configure:304`, `hush-c/Makefile:58` | `git` (makedepends) |
| test tools | `hush-c/tests/check_deps.sh:15` (`sh python3 curl node git cc make ps awk`), Node ≥ 22 (`:30`), Chrome/Chromium (`:36-48`) | checkdepends `nodejs chromium` (plus python, curl, git above) |
| runtime extras | README: `pass` for secrets, coturn `turnserver` for calls, Chromium-family browser for the UI | optdepends `pass`, `coturn`, `chromium` |

Arch package pages exist for all of these (HTTP 200): https://archlinux.org/packages/core/x86_64/openssl/ , `/core/x86_64/curl/`, `/extra/x86_64/libx11/`, `/core/x86_64/python/`, `/extra/x86_64/nodejs/`, `/extra/x86_64/chromium/`, `/extra/x86_64/coturn/`, `/extra/any/pass/` (OBSERVED).
License (OBSERVED on main @fa56420a): `LICENSE:1-2` reads `GNU GENERAL PUBLIC LICENSE` / `Version 3, 29 June 2007`, so the license is the GPL-3.0 family. The text alone does not say "only" or "or later", and the repo metadata disagrees:
- `-or-later`: `debian/copyright:7-13` (`License: GPL-3+`, "either version 3 of the License, or (at your option) any later version"), `hush-relay.spec:5` `GPLv3+`, `freebsd/net/hush-relay/Makefile:16` `GPLv3+`, `openbsd/net/hush-relay/Makefile:20` `# GPLv3+`.
- `-only`: `CITATION.cff:7` `license: GPL-3.0-only`.
- unqualified: `README.md:5` badge `GPL-3.0`, `llms.txt:3`.

**SPDX: AMBIGUOUS** between `GPL-3.0-or-later` and `GPL-3.0-only`. The draft PKGBUILD uses `GPL-3.0-or-later` because every shipped package (deb, RPM, BSD ports) already says that. It stays a placeholder until Chuck rules (Q3), and whichever way he rules, `CITATION.cff` or the package metadata must change to match.

### 2.3 Install layout (`make install DESTDIR="$pkgdir" PREFIX=/usr`)
All OBSERVED in `hush-c/Makefile:254-330`, except where marked.
- `/usr/bin/hush-relay`, `/usr/bin/hush-pass`
- `/usr/share/applications/hush-relay.desktop`; icons at `/usr/share/icons/hicolor/{48,128,256}x…/apps/hush-relay.png`
- `/usr/share/hush/codex/skills/write-legible-c/…`, `/usr/share/hush/skills/system/<slug>/SKILL.md`
- with STUN/TURN enabled (the default): `/usr/share/hush/systemd/hush-turn.service`, `/usr/share/hush/turnserver.conf.in`, **`/lib/systemd/system/hush-turn.service`** (`:315`), and `/etc/hush/turnserver.conf` at mode 0640 (`:318-322`). That file is the template, with the public `user=hush:replace-me` (`contrib/turnserver.conf.in:10`), and the unit has no `User=`. See P7.
- With DESTDIR set, no relay stop runs (`Makefile:76-81`) and no `~/.hush` is created (`hush-c/Makefile:290`). OBSERVED, and safe for packaging (INFERRED).
- The PKGBUILD adds `/usr/share/hush/hush-relay-stop`, as the deb (`debian/hush-relay.install`) and RPM (`%install`) do.

### 2.4 Repo-side fixes the packages need (separate PRs; not in this plan PR)
- **P1 `/lib` → `/usr/lib`** (#346). On Arch, `/lib` is a symlink owned by `filesystem`, so a package shipping `lib/…` should conflict at install (INFERRED; not run). Until #353 (P7) the draft PKGBUILD drops `lib/`; after it, the PKGBUILD installs the fixed unit under `/usr/lib`. Better: an upstream `SYSTEMD_UNITDIR ?= /lib/systemd/system` that Arch sets to `/usr/lib/systemd/system`.
- **P2 Port guard in `make`** (#347). `all: guard` (`Makefile:136`) refuses to build while a relay owns the guard port. A `yay -Syu` upgrade on a desktop that is running Hush would then fail its build (INFERRED). The draft builds with `make -C hush-c all`, which skips the guard: `hush-c/Makefile:17` already includes `../config.mk` (INFERRED to be equivalent; verify in VM).
- **P3 Build stamp in a non-git tarball** (#348). `configure:304` and `hush-c/Makefile:58` run `git describe`/`git rev-parse` from cwd. Inside an AUR clone (itself a git repo) a tarball build would stamp the AUR repo's SHA (INFERRED). The draft uses a git source, so the stamp is Hush's own. A tarball flavour must set `GIT_CEILING_DIRECTORIES="$srcdir"`.
- **P4 makepkg flags ignored** (#349). `config.mk` hard-sets `CFLAGS :=`/`LDFLAGS :=` (`configure:341-342`) and the top Makefile passes them down (`Makefile:137`), so Arch's `CFLAGS`/`LDFLAGS` (and `lto`) are not applied (INFERRED). Mixing them naively may also break `-Werror` (e.g. a redefined `_FORTIFY_SOURCE`, since configure adds `-D_FORTIFY_SOURCE=2`, `configure:223`) (INFERRED). Open question Q4.
- **P5 `$srcdir` baked into the binary** (#350). `HUSH_CODEX_SKILL_SOURCE` is `$(abspath ../.agents/…)` (`hush-c/Makefile:46-50`), so the package binary embeds a build-dir path. makepkg may warn "reference to $srcdir" (INFERRED). Open question Q5.
- **P6 Newer GCC with `-Werror`** (#351). CI builds on ubuntu-24.04 (`.github/workflows/ci.yml`). Arch's current GCC may raise new warnings that fail `-Werror` (INFERRED; UNKNOWN until built).
- **P7 No shipped TURN credential; coturn unprivileged** (#353, P1 security, fixed after #336/#328). Today (OBSERVED, main @fa56420a; staged install on the box 2:21 PM ET):
  - `make install` with DESTDIR ships `/etc/hush/turnserver.conf` from `contrib/turnserver.conf.in`, which has `lt-cred-mech` and `user=hush:replace-me` (`:8-10`; `hush-c/Makefile:318-321`);
  - `contrib/systemd/hush-turn.service:7-23` has no `User=`, so coturn runs as root;
  - the relay writes a random password only when it can write `/etc/hush` (`hush-c/src/hush_turn.c:97-99`, `:146-155`), which a desktop user cannot.
  So `sudo systemctl enable --now hush-turn` (`hush-c/src/hush_relay_main.c:146`) would start a root TURN server that accepts a public credential (INFERRED; not run). Required before any OPR or AUR release:
  - ship no credential: the template only, under `/usr/share/hush`, with no `user=`; no `/etc/hush/turnserver.conf` in the package and no `backup=` entry for it;
  - generate a random secret at first enable (`static-auth-secret` with `use-auth-secret`, or a random `user=`), mode 0600, owned by root or the coturn user; the unit refuses to start without it (`ConditionPathExists=` plus a check that rejects `replace-me`);
  - coturn runs as an unprivileged `User=` (the distro coturn user; its name on Arch and Devuan is UNKNOWN, Q12) or `DynamicUser=yes`;
  - test: a fresh install has no usable default credential, and nothing runs as root (V1, V2, V6).
  Until #353 lands, the draft PKGBUILD ships neither the unit nor `/etc/hush/turnserver.conf`. Calls then use CHILD mode, where the relay runs coturn as the desktop user with a random password in its own state dir (`hush_turn.c:122-137`; conf in the state dir, `:324-325`; OBSERVED; behaviour INFERRED).
- **P8 Package hooks signal no other user's relay** (Gauge S3). `scripts/hush-relay-stop` matches every `comm == "hush-relay"` (`:55`, `ps -axo pid=,comm=`) and SIGTERMs them all (`:76`). As a root hook (deb prerm, RPM `%pre`/`%preun`) that stops every user's relay on a multi-user machine (OBSERVED code; INFERRED effect). New rule for the OPR/AUR hooks: send no signals. List the running relays and tell the user to restart Hush (or that it restarts at the next login). A running relay keeps its old binary until then, because pacman replaces the file rather than writing into it (INFERRED). Bringing deb/RPM in line is a follow-up; no issue has been filed for it yet.
- Note (Gauge S4): #336 (Landlock or bubblewrap) and GUARD (`hush-guard-hook`) may add depends and files. The package file lists and depends will follow them when they merge.

### 2.5 Source and package signing
- Today: one tag, `v0.0.1`. It is an annotated tag object `e9b7f5e6` on commit `35e4b50a` (2026-08-18T00:40Z, i.e. 2026-08-17 20:40 ET), with verification `unsigned` (OBSERVED: https://api.github.com/repos/coldcanuk/hush/git/tags/e9b7f5e6e8a8b8eba28784216fa20ec0d3eb89ff). It predates all packaging work (#184, #200, #221, #259), so it cannot be packaged as-is (INFERRED).
- Main merge commits are signed by GitHub's web-flow key (`committer: GitHub`, verified) (OBSERVED: https://api.github.com/repos/coldcanuk/hush/commits/fa56420a8199c9c676b803faf48f34b0ea994675). That key does not identify Chuck, so it is not a `validpgpkeys` anchor (INFERRED).
- Plan: Chuck creates a personal GPG signing key, publishes the fingerprint (README/SECURITY.md plus a keyserver), and cuts `git tag -s v0.0.2` after a VERSION bump PR, following `docs/release-hygiene.md` (tag format `v<VERSION>`, never move a tag; OBSERVED). The PKGBUILD then uses `git+https://github.com/coldcanuk/hush.git#tag=v$pkgver?signed` with `validpgpkeys=('<CHUCK_FPR>')`. Ship `keys/pgp/<FPR>.asc` in the AUR repo (PKGBUILD wiki tip: `export-pkgbuild-keys`).
- `-git`: `?signed` on a branch would require every main commit to carry Chuck's signature, which web-flow merges do not (INFERRED). So leave `hush-relay-git` unsigned; that is normal for VCS packages.
- Binary signing: the AUR ships no binaries. The OPR signs with its own key (O7). LCOS/apt needs Chuck's key if Hush runs a repo (§3).

### 2.6 Draft PKGBUILD (`hush-relay`, release)
```bash
# Maintainer: Chuck Pitre <TBD-AUR-EMAIL>
pkgname=hush-relay
pkgver=0.0.2            # needs a signed tag v0.0.2 (does not exist yet)
pkgrel=1
pkgdesc="Hush: legible C11 Nostr relay and hive chat (hush-relay)"
arch=('x86_64' 'aarch64')
url="https://github.com/coldcanuk/hush"
license=('GPL-3.0-or-later')   # AMBIGUOUS (§2.2): placeholder until Chuck rules on Q3
depends=('glibc' 'openssl' 'curl' 'libx11')
makedepends=('git' 'python')
checkdepends=('nodejs' 'chromium')
optdepends=('pass: hush-pass secret storage (identity/provider keys)'
            'coturn: STUN/TURN server for conference calls'
            'chromium: app window (any Chromium-family browser)')
# no backup=(): P7 ships no /etc/hush/turnserver.conf
install=hush-relay.install
source=("hush::git+https://github.com/coldcanuk/hush.git#tag=v${pkgver}?signed")
sha256sums=('SKIP')     # run updpkgsums; the signed tag is the trust anchor
validpgpkeys=('<CHUCK_GPG_FINGERPRINT_UPPERCASE_NO_SPACES>')

build() {
  cd hush
  ./configure --prefix=/usr
  # P2: skip the dev-only live-relay port guard of the top-level `all`.
  make -C hush-c all
}

check() {
  cd hush
  # Same hygiene as debian/rules override_dh_auto_test (#223/#259):
  # never enter the relay-stop signal phases inside a package build.
  env -u CI -u GITHUB_ACTIONS -u HUSH_TEST_STOP_RELAYS make -C hush-c test
}

package() {
  cd hush
  make install DESTDIR="$pkgdir" PREFIX=/usr
  # P7 (#353): until upstream ships no credential and an unprivileged
  # User=, ship neither the root TURN unit nor the replace-me config.
  # Templates stay in /usr/share/hush; calls use CHILD mode.
  rm -f "$pkgdir/etc/hush/turnserver.conf"
  rmdir "$pkgdir/etc/hush" "$pkgdir/etc" 2>/dev/null || true
  # P1 (#346): Arch has no real /lib. After #353, install the fixed unit to
  # /usr/lib/systemd/system instead of dropping it.
  rm -rf "$pkgdir/lib"
  install -Dm755 scripts/hush-relay-stop "$pkgdir/usr/share/hush/hush-relay-stop"
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
```
`hush-relay.install` sends no signals (P8). This differs on purpose from the deb prerm and RPM `%pre`/`%preun`, which SIGTERM every user's relay (OBSERVED: `debian/hush-relay.prerm:29-31`, `hush-relay.spec:37-38`, `:74-75`):
```bash
_hush_running() { ps -axo user=,pid=,comm= 2>/dev/null | awk '$3 == "hush-relay" {print "  " $1 " pid " $2}'; }
_hush_notice() {
  r=$(_hush_running); [ -n "$r" ] || return 0
  echo "hush-relay is running (left alone; nothing was signalled):"; echo "$r"
  echo "$1"
}
post_install() { echo "Start Hush from the launcher, or run: hush-relay --open"; }
post_upgrade() { _hush_notice "Quit and reopen Hush to use the new version (or it restarts at next login)."; }
pre_remove()   { _hush_notice "Quit Hush; the running copy keeps the removed binary until it exits."; }
```
`hush-relay-git` differs only in these fields:
```bash
pkgname=hush-relay-git
pkgver=0.0.1.r0.g0000000   # set by pkgver()
provides=('hush-relay'); conflicts=('hush-relay')
source=("hush::git+https://github.com/coldcanuk/hush.git")   # unsigned, see 2.5
sha256sums=('SKIP')        # no validpgpkeys
pkgver() { cd hush; git describe --long --tags --abbrev=7 | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'; }
```
The AUR repo gets `PKGBUILD`, `.SRCINFO`, `hush-relay.install`, `keys/pgp/<FPR>.asc` (release only) and a `LICENSE` (0BSD) for the packaging files (A4).

### 2.7 Official Arch repo?
**No, not now.** AUR packages move to `[extra]` only when they get "enough community interest and the support of a Package Maintainer". Votes are one consideration. Package sources need 0BSD licensing to be eligible (OBSERVED: https://wiki.archlinux.org/title/Arch_User_Repository , AUR submission guidelines note on 0BSD). Hush has no AUR presence or votes yet, and no Package Maintainer sponsor is known (UNKNOWN), so it is not realistic in the short term (INFERRED).

### 2.8 Steps (AUR)
1. Land P1–P3 upstream (or keep the PKGBUILD workarounds), bump VERSION, and have Chuck push signed tag `v0.0.2`.
2. In a PLAN_VM_TEST Arch/Omarchy VM: `makepkg -si` (release), `makepkg -si` (git), `namcap PKGBUILD *.pkg.tar.zst`, `makepkg --printsrcinfo > .SRCINFO`.
3. Chuck (his AUR account and SSH key): `git -c init.defaultBranch=master clone ssh://aur@aur.archlinux.org/hush-relay.git`, add files, commit, push. Repeat for `hush-relay-git`.
4. Users: `yay -S hush-relay` (or *Install > AUR*). Updates come through `omarchy update`, which runs `yay -Sua` (O14, re-fetched). That update builds as the user while their relay may be running, so P2 matters.

## 3. LCOS

### 3.1 Findings

| # | Fact | Class | Source |
|---|---|---|---|
| L1 | lunduke.com/lcos.html has only the slogan and one link: the ISO `https://github.com/BryanLunduke/LCOS/releases/download/0.9.4/lcos-094.iso`. | OBSERVED | https://lunduke.com/lcos.html |
| L2 | Base: "LCOS is built on top of Devuan (a high quality fork of Debian, which removes Systemd)". | OBSERVED | https://github.com/BryanLunduke/LCOS/blob/eded2c8a75e0ccf31aefe718c723245da147879a/README.md |
| L3 | 0.9 ships Debian 13-era packages (`+deb13u…`) and Devuan `excalibur` (`…devuan1+excalibur2`; 0.3 `os-release` has `VERSION_CODENAME=excalibur`, `ID_LIKE="devuan debian"`). | OBSERVED | https://github.com/BryanLunduke/LCOS/blob/eded2c8a75e0ccf31aefe718c723245da147879a/LCOS-09-PackageSourceList.md , `lcos-packages/lcos-base/etc/os-release` |
| L4 | Package manager: `apt` 3.0.3devuan1, plus `synaptic` and an LCOS `lcos-updates` app. Init is `sysvinit-core` + `elogind`, no systemd. | OBSERVED | `LCOS-09-PackageSourceList.md` |
| L5 | Runtime libs Hush needs are present: `libssl3t64`, `libcurl4t64`, `libx11-6`, `python3`. The X server is XLibre. The default browser is Brave Origin. | OBSERVED | `LCOS-09-PackageSourceList.md`, README |
| L6 | LCOS runs a signed apt overlay, `deb [signed-by=/usr/share/keyrings/lcos-archive-keyring.gpg] https://lcos.lunduke.com/apt excalibur main` (key fpr `5A01 D4BD CDD1 E153 1D45 6A75 60D6 E7F6 CBD6 D572`). It holds only LCOS/Lunduke packages plus some Debian firmware/mesa rebuilds; there are no third-party apps. | OBSERVED | `lcos-packages/lcos-archive-keyring/…/lcos-overlay.list`, `README.lcos-keyring`; https://lcos.lunduke.com/apt/dists/excalibur/Release , `…/main/binary-amd64/Packages` |
| L7 | Third-party submission path: none documented. "LCOS is not a community project. The King has total control." Questions and suggestions go to forum.lunduke.com (subscribers only). Issues are enabled on GitHub. | OBSERVED (statements) / UNKNOWN (any path) | README; https://api.github.com/repos/BryanLunduke/LCOS |
| L8 | So getting Hush into the LCOS overlay depends on Lunduke's decision alone. | INFERRED | L7 |
| L9 | No `flatpak` package is in the 0.9 list. `lcos-appimage-thumbnailer` is (AppImage-friendly). | OBSERVED (list) / INFERRED (no Flatpak by default) | `LCOS-09-PackageSourceList.md` |
| L10 | Whether Hush's browser launch finds `brave-origin` is UNKNOWN (no `brave-origin` string in the repo: code search returned 0). | UNKNOWN | cursor-github `search_code repo:coldcanuk/hush brave-origin` |
| L11 | `hush-turn.service` is inert on LCOS (no systemd). TURN daemon mode needs a sysvinit script or CHILD mode only (UNKNOWN which mode Settings→Daemon uses without systemctl). The `.deb` still ships the `replace-me` `/etc/hush/turnserver.conf` (P7, #353). | INFERRED | L4, `hush-c/Makefile:308-330` |

### 3.2 Existing Debian packaging (reuse) and gaps
OBSERVED: `debian/rules` is a real makefile (`#!/usr/bin/make -f`). Its overrides: clean `make -C hush-c clean` only, build `./configure --prefix=/usr && $(MAKE)`, install `DESTDIR=$(CURDIR)/debian/hush-relay PREFIX=/usr`, and test with `CI`/`GITHUB_ACTIONS`/`HUSH_TEST_STOP_RELAYS` unset (#223, merged via #259). `debian/hush-relay.prerm` delegates to `/usr/share/hush/hush-relay-stop`.
Gaps to fix before an LCOS `.deb` is usable:
- **D1** (#352) `debian/control` `Depends: ${misc:Depends}` has no `${shlibs:Depends}`, so the .deb would not declare libssl/libcurl/libX11 (OBSERVED file; INFERRED effect). Fix: `Depends: ${shlibs:Depends}, ${misc:Depends}`.
- **D2** (#352) `Build-Depends: debhelper-compat (= 13)` only. Add `pkg-config, libssl-dev, libcurl4-openssl-dev, libx11-dev, python3`, the CI list (`.github/workflows/ci.yml`). The test deps (`nodejs` ≥ 22, `chromium`) go in `<!nocheck>`. Whether Debian 13 ships Node ≥ 22 is UNKNOWN.
- **D3** `override_dh_auto_build` runs the top-level `make`, which includes the port guard (P2).
- **D4** `make deb` has never completed in CI or on kiff (PR #259 body: `dpkg-checkbuilddeps` unmet on kiff; CI runs only make/make test) (OBSERVED).
- **D5** `/lib/systemd/system/hush-turn.service` ships on a non-systemd OS (harmless, INFERRED). Consider an `/etc/init.d` script later (Q8).

### 3.3 Steps (LCOS)
1. Fix D1–D3 (#352, #347) and P7 (#353). Then drop SIGTERM from the prerm (P8). Build the `.deb` in an LCOS VM (§4) from a signed tag.
2. Phase 1: attach `hush-relay_<v>_amd64.deb` plus a `SHA256SUMS.asc` to a GitHub Release. Users run `sudo apt install ./hush-relay_<v>_amd64.deb`; apt resolves the D1 deps.
3. Phase 2 (optional): a Hush apt repo (e.g. GitHub Pages, as LCOS itself does) signed with Chuck's key. Users install a `hush-archive-keyring` and a `sources.list.d` entry, then `apt install hush-relay`. That gives updates through LCOS Updates/synaptic (INFERRED; UNKNOWN whether `lcos-updates` honours third-party sources).
4. Phase 3: ask Lunduke (forum or a GitHub issue on BryanLunduke/LCOS) whether Hush can be carried in the overlay. The outcome is his call (L8). Note that LCOS's "No Built-in AI Features" stance concerns the default install; the README says users may install AI tools (OBSERVED).

## 4. Testing in ephemeral VMs (per PLAN_VM_TEST, forthcoming)
Hosts: kiff, Caesar, Athena. Rules from Chuck's brief:
- one fresh VM per run;
- a read-only base image plus a copy-on-write overlay (e.g. `qemu-img create -f qcow2 -F qcow2 -b <base> run.qcow2`), destroyed after the run;
- no sudo by bots on any host;
- no Hush keys, `pass` store, `HUSH_KEY_PASS`, `keys.vault` or Vault creds in any VM;
- the release GPG key and the AUR SSH key live in vault.onerelay.app, which is the source of truth (USER_SPECIFIED). A job gets a per-job `pass` copy on the host only, removed after the job; never plaintext on disk and never inside a VM. Signing and the AUR push happen on the host, after the VM run;
- the LCOS ISO in kiff `~/Downloads` stays read-only: attach it as `-cdrom` or as a backing file, never write it.
This plan did not touch kiff.

| Run | Base | Steps | Pass criteria |
|---|---|---|---|
| V1 OPR | Omarchy 4.0.4 ISO (verify the SHA-256 and signature published on omarchy.org) → base image | clone a fork of omarchy-pkgs, `bin/build --package hush-relay`, `pacman -U build-output/edge/x86_64/hush-relay-*.pkg.tar.zst` | installs; `hush-relay --help` stamp = tag; desktop entry + icons; no `/lib` conflict; **P7:** `grep -r replace-me` over `pacman -Ql hush-relay` files finds nothing usable, there is no `/etc/hush/turnserver.conf`, and after enabling calls `ps -o user= -C turnserver,hush-relay` shows no root (after #353, also `systemctl show -p User hush-turn` is not root) |
| V2 AUR release | same Omarchy base | `makepkg -si`, `namcap` | as V1, including the P7 checks; `check()` green or recorded failures; `pacman -Qkk hush-relay` clean |
| V3 AUR git | same | `makepkg -si` for `hush-relay-git`, then switch back to `hush-relay` | provides/conflicts swap works |
| V4 upgrade | V2 result with relays running as two users (`hush-relay --no-open` on separate ports) | rebuild with `pkgrel+1`, `pacman -U` (and once via `omarchy update`) | **P8:** neither relay gets a signal (same pids before and after); `post_upgrade` lists both; a relaunch runs the new stamp; P2 does not block the build |
| V5 remove | V4 result | `pacman -Rns hush-relay` | no relay signalled (P8) and the notice is printed; no `/etc/hush` left behind; `~/.hush` untouched |
| V6 LCOS | LCOS 0.9.4 ISO (read-only) → installed base | `apt install ./hush-relay_*.deb` (built in a Devuan/LCOS VM) | deps resolve via `${shlibs:Depends}`; app opens in Brave Origin (L10); P7 checks as V1 (`dpkg -L`); remove/purge signal only per the P8 rule once the prerm is fixed |
| V7 aarch64 | OPR CI only (no local aarch64 VM planned) | the OPR PR's aarch64 build | green `result` check |
| V8 OpenBSD | fresh OpenBSD VM on Caesar/Athena (or kiff), CoW overlay; no vmactions | `gmake openbsd` (`Makefile:191-195`, `scripts/package-openbsd.sh`, `openbsd/net/hush-relay`), `pkg_add -D unsigned` the result | installs and runs; `hush-relay --help` stamp = tag; P7 checks as V1; uninstall leaves no files |
| V9 FreeBSD | fresh FreeBSD VM on Caesar/Athena (or kiff), CoW overlay; no vmactions | `gmake freebsd` (`Makefile:197-201`, `scripts/package-freebsd.sh`, `freebsd/net/hush-relay`), `pkg add` the result | as V8 |

The matrix covers Omarchy (V1–V5, V7), LCOS (V6) and BSD (V8, V9). Which BSD versions to use and which host runs which run belong to PLAN_VM_TEST (UNKNOWN today). No vmactions or CI workflow exists for BSD on main (`git grep vmactions`: no hits, OBSERVED).

Who runs guest-side root steps (`pacman -U`, `apt install`) inside a disposable guest is an open question (Q9). Until Chuck rules, Chuck runs them.

## 5. Chores for Chuck
1. Done (14:19): the names are `hush-relay` and `hush-relay-git` (USER_SPECIFIED, A2).
2. Rule on the SPDX id (Q3; AMBIGUOUS in §2.2). Then align `CITATION.cff:7` or the package metadata.
3. Create a GPG signing key and store it in vault.onerelay.app (the source of truth; jobs get a per-job `pass` copy only, never plaintext). Publish the fingerprint, and from then on push only signed release tags (`git tag -s v0.0.2`) after the VERSION bump PR.
4. Create an AUR account at aur.archlinux.org, create a dedicated SSH key (`ssh-keygen -f ~/.ssh/aur`) and keep the private key in vault.onerelay.app (a per-job `pass` copy only). Add the public key under *My Account*, and add the `~/.ssh/config` `Host aur.archlinux.org` entry. Do the first push of both pkgbases.
5. Open the OPR PR to `omacom/omarchy-pkgs` from his GitHub account (or approve a bot doing it), and answer the maintainers. Optionally open a Discussions suggestion in `omacom/omarchy` for an *Install* menu row.
6. LCOS: attach the `.deb` and signed checksums to GitHub Releases. Decide on a Hush apt repo (key, hosting, domain). Optionally contact Lunduke.
7. Provide or approve the VM hosts and base images per PLAN_VM_TEST, including the OpenBSD and FreeBSD bases (V8, V9), and rule on Q9.
8. Merge order: #336 and #328, then the #353 fix (P7). No OPR or AUR release happens before #353.

## 6. Open questions
- Q1: Will the OPR maintainers accept Hush (O10)? Does a package need prior AUR traction?
- Q2: OPR and AUR in parallel, or AUR first to shake out the PKGBUILD? (The ruling says both; ordering is open.)
- Q3: `GPL-3.0-or-later` or `GPL-3.0-only`?
- Q4: Honour makepkg `CFLAGS`/`LDFLAGS` (P4)? If yes, how do they compose with configure's hardening flags and `-Werror`?
- Q5: Fix `HUSH_CODEX_SKILL_SOURCE` baking `$srcdir` (P5). Should installed builds read `/usr/share/hush/codex/…` only?
- Q6: Make `make check()` hermetic enough for makepkg and the OPR Docker builder (Chromium headless, fixed ports 18783/18814, Node ≥ 22)? Or default to `--nocheck` with a documented reason?
- Q7: Upstream `SYSTEMD_UNITDIR` (P1) and a `NO_GUARD=1` build knob (P2), instead of PKGBUILD workarounds?
- Q8: LCOS has no systemd. Ship a sysvinit script for TURN daemon mode, or document CHILD mode only?
- Q9: Does "no sudo by bots" cover root inside a disposable guest VM?
- Q10: Should a pacman-installed `/usr/bin/hush-relay` coexist with an existing `~/.local/bin/hush-relay` from `make install`? PATH order on Omarchy is UNKNOWN. Should the post-install message warn about a shadowing copy?
- Q11: Does Hush's browser launcher detect Brave Origin on LCOS (L10)?
- Q12: What is the coturn service user called on Arch and on Devuan/LCOS, and is `DynamicUser=yes` acceptable instead (P7)?
- Q13: In daemon mode, may the relay user write the generated TURN secret (e.g. through a `hush` group, 0640)? Or are package installs CHILD-mode only (P7)?

## 7. Related repo history
- #184 (PR, merged): make/install refuse a live relay; clean reaps CHILD turnserver (packaging guards).
- #199 / #200 (PRs, merged): honour explicit PREFIX; refuse empty PREFIX/BINDIR/DATADIR; command-line PREFIX re-derives BINDIR/DATADIR (`Makefile:39-46`, `check-prefix` `:101-116`).
- #221 (issue) / #222 (PR): install/clean stop any `hush-relay*`; DESTDIR staging installs skip the stop.
- #223 (issue) / #259 (PR, merged): `debian/rules` as a makefile; clean and test stay in the tree.
- #232 (issue): BSD packaging uses gmake.
- #346–#352 (issues): P1–P6, D1/D2 packaging gaps from this plan. #353 (issue, P1 security): P7.

## 8. Gauge response (#345 @a84a5eda, FAIL 2:19 PM ET)
| Finding | Where fixed |
|---|---|
| S1 P2: public `replace-me` TURN credential, root coturn | §2.4 P7; issue #353; §2.6 PKGBUILD drops the unit and conf until #353; §2.3; L11; V1/V2/V6/V8 checks; Q12, Q13; chore 8 |
| S2: no BSD in the VM matrix | §4 V8, V9 and the coverage note |
| S2: no home named for the keys | §4 rules; chores 3, 4 (vault.onerelay.app, per-job `pass` copy) |
| S2: AUR names | A2 is USER_SPECIFIED (Chuck 14:19); chore 1 |
| S3: root hook SIGTERMs every user's relay | §2.4 P8; §2.6 `.install` with no signals; V4, V5; §3.3 step 1 |
| S4: #336/GUARD deps | §2.4 note |
| S5: O14 not re-fetched | O14 re-fetched and confirmed in code (`bin/omarchy-update:127,152`, `bin/omarchy-update-aur-pkgs:12-15`) |
| License (CoS 14:19) | §2.2: OBSERVED `LICENSE:1-2`; SPDX AMBIGUOUS (`CITATION.cff:7` vs `debian/copyright:7-13`, spec, ports) |
