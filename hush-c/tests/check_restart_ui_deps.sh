#!/bin/sh
# check_restart_ui_deps.sh: check_restart_ui.sh must print SKIP when node or
# Chrome is missing and CI is unset, and must fail when CI is set.
set -eu
cd "$(dirname "$0")/.."
fail() {
    echo "restart UI deps check failed: $1" >&2
    exit 1
}
bin=$(mktemp -d "${TMPDIR:-/tmp}/hush-ui-deps.XXXXXX")
trap 'rm -rf "$bin"' EXIT INT TERM
ln -s "$(command -v dirname)" "$bin/dirname"
run_wrapper() {
    env -u CI -u GITHUB_ACTIONS -u HUSH_CHROME_BIN PATH="$bin" "$@" /bin/sh tests/check_restart_ui.sh 2>&1
}
out=$(run_wrapper) || fail "missing node must not fail when CI is unset: $out"
echo "$out" | grep -q '^SKIP restart UI check: node is required' \
    || fail "missing node must print SKIP when CI is unset: $out"
if out=$(run_wrapper CI=true); then
    fail "missing node must fail when CI is set: $out"
fi
echo "$out" | grep -q 'restart UI check FATAL: node is required' \
    || fail "missing node must print FATAL when CI is set: $out"
node_bin=$(command -v node || true)
if [ -n "$node_bin" ]; then
    ln -s "$node_bin" "$bin/node"
    out=$(run_wrapper) || fail "missing Chrome must not fail when CI is unset: $out"
    echo "$out" | grep -q '^SKIP restart UI check: ' \
        || fail "missing Chrome must print SKIP when CI is unset: $out"
    if out=$(run_wrapper CI=true); then
        fail "missing Chrome must fail when CI is set: $out"
    fi
    echo "$out" | grep -q 'restart UI check FATAL: ' \
        || fail "missing Chrome must print FATAL when CI is set: $out"
fi
echo "restart UI deps ok"
