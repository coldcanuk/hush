# WS3-E: PR ready for Floor to open

`ManagePullRequest` refuses `gb/` heads (requires `cursor/` prefix),
and repo law forbids `cursor/` branches — so the branch is pushed and
the PR is ready for Floor to open with one click. Delete this file
after the PR is opened (same pattern as WS3-D).

- Branch (pushed): `gb/reliability-ops-index` @ `04ab4ad0`
- Base: `main` @ `7d042620` (includes WS3-D merge)
- One-click compare / create PR:

  https://github.com/coldcanuk/hush/pull/new/gb/reliability-ops-index

- Suggested title:

  WS3-E: single-hive reliability ops index + cleanup

- Suggested body (paste as-is):

WS3-E (Floor / reliability): single-hive reliability ops index +
cleanup. Docs-only — no `.c`/`.h`, no CI edits.

## What

- New `docs/ops/reliability.md`: operator index sequencing the landed
  WS3 A–D work into one path — measure store
  (`docs/ops/store-bench.md`) → backup/restore
  (`docs/ops/store-backup.md`) → graceful package upgrade
  (`docs/ops/package-upgrade.md`) → healthprobe
  (`docs/ops/api-status.md`). OBSERVED links only; no new bench
  numbers, restore claims, or probe fields. Short "when to use which"
  + default bind/port pointer to the api-status doc. Explicit: closes
  the single-hive reliability loop for A–D; no federation /
  TLS-in-relay / multi-human.
- README Docs table gains one row linking the index (same pattern as
  other ops rows).
- Deletes leftover scaffold `PR_WS3-D_READY.md` from the tree root.

## Acceptance

Tree has reliability index + README link; `PR_WS3-D_READY.md` gone; no
drive-bys (no other WS3 slices, no Armory/chat).
