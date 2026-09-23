# Release hygiene

Short note for the WS1 blocking-CI work. Scope is hygiene only; no product changes.

## VERSION file

- Source of truth: top-level `VERSION` (currently `0.0.1`).
- `CHANGELOG.md` follows Keep a Changelog; versions come from `VERSION`.
- Bump `VERSION` on its own worktree branch (`gb/<slug>`) and land via PR like any other change.

## Tags

- Tag format observed in this repo: `v<VERSION>` (e.g. `v0.0.1` exists for `0.0.1`).
- Cut the tag from `main` **after** the version-bump PR merges and CI is green.
- Never move a tag; if the tag is wrong, bump the version and tag again.

## Packaging smoke (`make dist`)

- `make dist` exists (top-level `Makefile`): `git archive HEAD` into
  `hush-$(cat VERSION).tar.gz`.
- Smoke it before tagging a release:

```bash
./configure
make
make test
make dist
```

- DEB/RPM/Flatpak/OpenBSD/FreeBSD targets (`make deb`, `make rpm`,
  `make flatpak`, `make openbsd`, `make freebsd`) are **not** part of the
  blocking CI path and are not smoked here; see `README.md` installation
  sections when cutting platform packages.

## Package upgrade stops

Upgrades stop the running relay gracefully (SIGTERM + 30s poll, no
SIGKILL on upgrade; SIGKILL only on remove/purge after the same grace).
See [`docs/ops/package-upgrade.md`](ops/package-upgrade.md) for the
deb/RPM scriptlet mapping and verification.
