#!/bin/sh
# check_restart_ui.cjs removes every temp dir it makes (its Chrome profile
# and relay homes, all named id1-*; not the fixed id1-shots dir) on a normal exit, on a failure, and on
# SIGTERM and SIGINT, and on a thrown error. Before this pin it left one id1-chrome-* profile per
# run in the temp dir. Runs the check in Chrome-only mode against the
# in-tree fake Chromes, with TMPDIR pointed at a fresh mkdtemp dir, and
# counts what is left there after each exit.
set -eu
cd "$(dirname "$0")/.."
fail() { echo "restart UI tmp check failed: $1" >&2; exit 1; }

command -v node >/dev/null 2>&1 || fail "node is required"
node -e 'process.exit(typeof WebSocket === "function" ? 0 : 1)' \
    || fail "node has no global WebSocket (need node >= 22)"

# A Chrome that never answers keeps the check waiting this long.
NEVER_READY_MS=60000
# Polls for the profile dir: 200 x 0.05 s = 10 s.
POLL_TRIES=200
fake="$(pwd)/tests/fake-chrome-devtools.sh"
exitfake="$(pwd)/tests/fake-chrome-exit.sh"
scratch=$(mktemp -d)
pid=""
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT

# Fresh TMPDIR for one run; prints its path.
fresh_tmp() {
    d="$scratch/$1"
    mkdir "$d"
    printf '%s\n' "$d"
}
# id1-shots is the fixed screenshot dir (ID1_ART default), kept on purpose.
left() { find "$1" -mindepth 1 -maxdepth 1 -name 'id1-*' ! -name id1-shots | wc -l | tr -d ' '; }

export ID1_CHROME_ONLY=1
unset HUSH_FAKE_CHROME_LAUNCH_LOG || true

# Normal exit.
t=$(fresh_tmp normal)
TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_TEST_WAIT_S=5 \
    node tests/check_restart_ui.cjs >/dev/null 2>&1 || fail "chrome-only run failed"
[ "$(left "$t")" = 0 ] || fail "normal exit left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (normal exit)"

# Failure exit with Chrome up: it lists no page target.
t=$(fresh_tmp nopage)
set +e
ID1_CHROME_ONLY= TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_NO_PAGE=1 \
    HUSH_TEST_WAIT_S=5 node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "no page target want exit 1, got $rc"
[ "$(left "$t")" = 0 ] || fail "failure with Chrome up left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (failure with Chrome up)"

# Failure exit after a relaunch: Chrome never answers inside the deadline,
# so both launches are stopped and their profiles removed.
t=$(fresh_tmp never)
set +e
TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_DELAY_MS="$NEVER_READY_MS" \
    HUSH_TEST_WAIT_S=1 node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "never-ready Chrome want exit 1, got $rc"
[ "$(left "$t")" = 0 ] || fail "relaunch failure left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (relaunch failure)"

# Failure exit (Chrome exits early).
t=$(fresh_tmp failed)
set +e
TMPDIR="$t" HUSH_CHROME_BIN="$exitfake" HUSH_TEST_WAIT_S=5 \
    node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "early-exit Chrome want exit 1, got $rc"
[ "$(left "$t")" = 0 ] || fail "failure exit left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (failure exit)"

# Thrown error: Chrome is up but its page socket refuses the CDP connect.
t=$(fresh_tmp thrown)
set +e
ID1_CHROME_ONLY= TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_TEST_WAIT_S=5 \
    node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "refused CDP connect want exit 1, got $rc"
[ "$(left "$t")" = 0 ] || fail "thrown error left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (thrown error)"

# Signals, while it waits on a Chrome that never answers.
for sig in TERM INT; do
    t=$(fresh_tmp "sig$sig")
    TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_DELAY_MS="$NEVER_READY_MS" \
        HUSH_TEST_WAIT_S=30 node tests/check_restart_ui.cjs >/dev/null 2>&1 &
    pid=$!
    i=0
    until [ "$(left "$t")" != 0 ]; do
        i=$((i + 1))
        [ "$i" -lt "$POLL_TRIES" ] || fail "SIG$sig: no Chrome profile appeared"
        sleep 0.05
    done
    kill -s "$sig" "$pid"
    set +e
    wait "$pid"
    rc=$?
    set -e
    pid=""
    [ "$rc" -ne 0 ] || fail "SIG$sig: the check must not exit 0"
    [ "$(left "$t")" = 0 ] || fail "SIG$sig left $(left "$t") id1-* dirs"
    echo "restart UI tmp ok (SIG$sig)"
done
echo "restart UI tmp ok"
