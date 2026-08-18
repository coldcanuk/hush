# PLAN: Default-on Unix `pass` for identity keys (RDAP)

Branch: `gb/pass-identity-keys`
Worktree: `worktrees/pass-identity-keys`
Base: `a67299a71` (main, first-launch UX)

## Scope

See RESEARCH.md §2026-08-17 default-on `pass`. The backup card is the modal.
Checkbox is **checked by default**. Uncheck to opt out. Retrieve CLI is shown.

## Phase 0 — Isolation

- [x] M0.1 worktree `gb/pass-identity-keys` on clean main. No orphan worktrees.

## Phase 1 — Research

- [x] M1.1 Inventory identity / `pass` / modal code and docs.
- [x] M1.2 Confirm `pass` CLI, store, and local GPG/init gap.
- [x] M1.3 Synthesize RESEARCH.md + this plan. **Gate.**

## Phase 2 — Architecture

- [x] M2.1 Lock `hush_pass` module, session fields, default-on checkbox, paths.

## Phase 3 — Implementation

- [ ] M3.1 `scripts/hush-pass` force-insert + docs/pass-integration.md.
- [ ] M3.2 `hush_pass` C module + injectable helper + `test_pass`.
- [ ] M3.3 Launch/HTTP: `save_pass` default true; import uses backup modal;
      persist human + Payne; optional restore on boot.
- [ ] M3.4 PWA modal: checked by default, retrieve CLI, post `save_pass`.
- [ ] M3.5 Tests (`check_launch.sh`) + README / IMPORT / SECURITY.

## Phase 4 — Verify / land

- [ ] M4.1 `./configure && make && make test`. write-legible-c §14 on C diffs.
- [ ] M4.2 Push, PR, auto-merge, delete worktree.

## Task notes

Every C change follows write-legible-c. No `-Werror` carve-outs.
Never put secrets on argv. Missing `pass` does not block identity creation.
