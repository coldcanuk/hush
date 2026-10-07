#!/bin/sh
# #287: the suite's PATH, HOME, and XDG_CONFIG_HOME must not reach a real CLI.
# make test puts a refusing sentinel ahead of goose, grok, codex, copilot,
# cline, and ollama. This check fails if that sentinel is missing.
set -eu
cd "$(dirname "$0")/.."

fail() { echo "hermetic path failed: $1" >&2; exit 1; }

test -d "${HOME:-}" || fail "HOME is not an empty scratch directory"
test -d "${XDG_CONFIG_HOME:-}" || fail "XDG_CONFIG_HOME is not an empty scratch directory"
find "$HOME" -mindepth 1 -print | grep -q . && fail "HOME is not empty" || true
find "$XDG_CONFIG_HOME" -mindepth 1 -print | grep -q . && fail "XDG_CONFIG_HOME is not empty" || true

for name in goose grok codex copilot cline ollama; do
  bin=$(command -v "$name" || true)
  test -n "$bin" || fail "$name is not on PATH"
  set +e
  out=$("$name" 2>&1)
  status=$?
  set -e
  test "$status" -eq 97 || fail "$name exited $status (a real CLI would be reached)"
  printf '%s\n' "$out" | grep -q "hush test refused real $name" \
    || fail "$name did not refuse loudly"
done

echo "hermetic path ok"
