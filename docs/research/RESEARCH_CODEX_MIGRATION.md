# Codex migration research and synthesis

2026-09-06; baseline 565bcfc64.

The provider table already contains Codex, its OAuth gate, login command, and
headless runner. The retired provider adds a redundant exec branch, two unused
policy bits, UI controls, and an end-to-end test. Saved rosters can still contain
its id; restoration must normalize those fields without accepting new requests.

`codex exec --help` confirms `--cd`, `--skip-git-repo-check`, and `--sandbox`.
`codex --help` confirms its update subcommand; preserve the existing opt-in scanner. The Codex skill documentation confirms
repository and user `.agents/skills` discovery and support for skill symlinks:
https://learn.chatgpt.com/docs/build-skills

The upstream skill is instruction-only. Keep its complete normative reference
and MIT license, adjust invocation to `$write-legible-c`, and install it for the
user and repository. Hush's isolated runtime working directory must expose the
same skill for spawned Codex jobs; verify this at the fake-CLI process boundary.

Synthesis: remove the duplicate provider, normalize persisted ids at restore,
retain Codex OAuth, migrate project skill discovery to `.agents/skills`, update
current documentation, and mark old reports as historical where needed. Do not
rename other runtime integrations or migrate credentials.

## Validation and review

- Strict `./configure` and `make -j4`: passed.
- Full `make test`: passed with HOME, GNUPGHOME, and PASSWORD_STORE_DIR scoped
  to a temporary directory. The initial unisolated first-launch test detected
  an existing account identity; no production identity was modified.
- `test_codex`: validates complete skill linking, repeated setup, missing source,
  argument rejection, and preservation of conflicting files.
- `check_codex.sh`: verifies retired-id rejection, separate Codex auth,
  persisted primary/ranked migration and deduplication, exact exec options,
  child access to the skill/reference/license, and prompt/reply delivery.
- Staged `make install`: all four skill files match the repository copy.
- All ten project skills pass the Codex skill frontmatter validator. Account
  and repository skill copies match; the upstream normative reference is
  byte-for-byte unchanged.
- Diff review: OAuth flag value remains stable, provider count matches the
  12 registry entries, runtime-only removal does not rename other providers,
  persisted aliases are accepted only by restore, and documentation links
  point to the migrated skill tree. Older research is marked historical.

Verification uses a fake Codex executable to check the process contract;
no paid model call was made. The installed CLI help confirms the flags used.
