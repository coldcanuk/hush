#!/usr/bin/env bash
# install-hooks.sh — install Hush Prime Directive git hooks (block direct main).
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
if [[ -z "${ROOT}" ]]; then
  echo "error: not inside a git repository" >&2
  exit 1
fi

HOOK_DIR="${ROOT}/.git/hooks"
# Linked worktrees use .git as a file; resolve common hooks dir via git
COMMON="$(git rev-parse --git-common-dir)"
HOOK_DIR="${COMMON}/hooks"
mkdir -p "${HOOK_DIR}"

install_hook() {
  local name="$1"
  local body="$2"
  local path="${HOOK_DIR}/${name}"
  printf '%s\n' "${body}" > "${path}"
  chmod +x "${path}"
  echo "installed ${path}"
}

PRE_COMMIT='#!/usr/bin/env bash
# Hush Prime Directive: refuse commits on main.
set -euo pipefail
branch="$(git branch --show-current 2>/dev/null || true)"
if [[ "${branch}" == "main" || "${branch}" == "master" ]]; then
  cat >&2 <<EOF
ERROR: Hush Prime Directive — direct commits on ${branch} are prohibited.

Required:
  1. git worktree add -b gb/<slug> worktrees/<slug>
  2. commit and push on gb/<slug>
  3. open a Pull Request into main (review + auto-merge)
  4. after merge, delete the worktree

See PRIME_DIRECTIVE.md
EOF
  exit 1
fi
exit 0
'

PRE_PUSH='#!/usr/bin/env bash
# Hush Prime Directive: refuse pushes to main (and commits while on main).
set -euo pipefail

branch="$(git branch --show-current 2>/dev/null || true)"
if [[ "${branch}" == "main" || "${branch}" == "master" ]]; then
  cat >&2 <<EOF
ERROR: Hush Prime Directive — pushes while on ${branch} are prohibited.
Use a gb/* worktree branch and a Pull Request.
See PRIME_DIRECTIVE.md
EOF
  exit 1
fi

remote_name="${1:-}"
url="${2:-}"
zero="0000000000000000000000000000000000000000"

while read -r local_ref local_sha remote_ref remote_sha; do
  [[ -z "${local_ref}" ]] && continue
  case "${remote_ref}" in
    refs/heads/main|refs/heads/master)
      cat >&2 <<EOF
ERROR: Hush Prime Directive — direct push to ${remote_ref} is prohibited.

Push your gb/* branch and open a Pull Request:
  git push -u origin HEAD
  gh pr create --base main --head <gb/slug>
  gh pr merge --auto --merge

See PRIME_DIRECTIVE.md
EOF
      exit 1
      ;;
  esac
  if [[ "${local_sha}" == "${zero}" ]]; then
    continue
  fi
done

exit 0
'

install_hook pre-commit "${PRE_COMMIT}"
install_hook pre-push "${PRE_PUSH}"

echo "Hush hooks installed. Direct main commit/push will fail."
