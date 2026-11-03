#!/bin/sh
# check_restart_cleanup.sh — killing test for check_restart.sh's failure-path
# cleanup. Runs check_restart.sh with a private TMPDIR and
# HUSH_TEST_RESTART_FAIL_AT=C, which fails it after section C has stored
# real-format nsecs in its fake pass store, then requires that the run
# failed at that hook and left nothing in its TMPDIR: every mktemp dir and
# file check_restart.sh made (log, homes, configs, pass store) is gone.
# Prints names and modes only; nothing left behind is opened.
set -eu
cd "$(dirname "$0")/.."

FAIL_HOOK=C
FAIL_MARK="forced section-C failure"

run=""
out=""
cleanup() {
    if [ -n "$run" ]; then
        rm -rf "$run"
    fi
    if [ -n "$out" ]; then
        rm -f "$out"
    fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() { echo "restart cleanup check failed: $1" >&2; exit 1; }

run=$(mktemp -d)
out=$(mktemp)
set +e
TMPDIR="$run" HUSH_TEST_RESTART_FAIL_AT="$FAIL_HOOK" sh tests/check_restart.sh >"$out" 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] || fail "check_restart.sh passed with HUSH_TEST_RESTART_FAIL_AT=$FAIL_HOOK"
grep -q "$FAIL_MARK" "$out" || { cat "$out" >&2; fail "check_restart.sh failed before the section-C hook (rc=$rc)"; }
left=$(find "$run" -mindepth 1 -printf '  %m %p\n')
if [ -n "$left" ]; then
    echo "$left" >&2
    fail "a section-C failure left $(printf '%s\n' "$left" | wc -l) entr(ies) in its TMPDIR"
fi
echo "restart cleanup ok: a forced section-C failure (rc=$rc) left nothing in its TMPDIR"
