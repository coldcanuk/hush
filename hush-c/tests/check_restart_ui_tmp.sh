#!/bin/sh
# check_restart_ui.cjs removes every temp dir it makes (its Chrome profile
# and relay homes, all named id1-*; not the fixed id1-shots dir) on a normal exit, on a failure, and on
# SIGTERM and SIGINT, and on a thrown error. Before this pin it left one id1-chrome-* profile per
# run in the temp dir. Runs the check in Chrome-only mode against the
# in-tree fake Chromes, with TMPDIR pointed at a fresh mkdtemp dir, and
# counts what is left there after each exit. The fake Chrome takes
# STOP_MS to shut down and rewrites its profile while it does, and the count
# waits until every ready fake Chrome has logged its exit, so a check that
# removes a profile before its Chrome has exited is caught. A preload throws
# (and rejects) from a callback outside the check's promises, for the
# uncaughtException and unhandledRejection paths.
# Every fake logs "ready" once its SIGTERM handler is in (the
# fake-chrome-ready.cjs preload, through NODE_OPTIONS), and this pin (and the
# throw preload) signal only after that line, so the stop never races the
# fake's start. A fake that outlives the check logs "orphan": that is
# reported as a leak. A fake stopped before it was ready (it dies at once,
# with no exit line and no profile rewrite) is a start race and is only
# noted, never failed.
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
# Fake Chrome shutdown time; well under the check's 5 s SIGKILL wait.
STOP_MS=300
fake="$(pwd)/tests/fake-chrome-count.sh"
throwjs="$(pwd)/tests/fake-throw-callback.cjs"
exitfake="$(pwd)/tests/fake-chrome-exit.sh"
readyjs="$(pwd)/tests/fake-chrome-ready.cjs"
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

# Fresh TMPDIR for run $1, in t. Also starts fresh fake Chrome launch and
# exit logs for that run.
fresh_tmp() {
    t="$scratch/$1"
    mkdir "$t"
    : > "$scratch/$1.launch"
    : > "$scratch/$1.exit"
    : > "$scratch/$1.ready"
    export HUSH_FAKE_CHROME_LAUNCH_LOG="$scratch/$1.launch"
    export HUSH_FAKE_CHROME_EXIT_LOG="$scratch/$1.exit"
    export HUSH_FAKE_CHROME_READY_LOG="$scratch/$1.ready"
}
# Lines in run $1's log $2 (launch, ready or exit) that are $3.
lines() { grep -c "^$3\$" "$scratch/$1.$2" || true; }
# Waits until every ready fake Chrome of run $1 has logged its exit. A fake
# that logs "orphan" was never stopped by the check (a leak); a ready fake
# with no exit line by the deadline is still running (a leak); a launched
# fake that never got ready was stopped during its start (a start race,
# not a leak: it dies at once and rewrites nothing), noted only.
settled() {
    i=0
    while :; do
        orphans=$(lines "$1" exit orphan)
        [ "$orphans" = 0 ] || fail "$1: leak: $orphans fake Chrome(s) outlived the check (orphaned: never stopped, or the check exited before they did)"
        ready=$(lines "$1" ready ready)
        exited=$(lines "$1" exit exit)
        [ "$exited" -lt "$ready" ] || break
        i=$((i + 1))
        [ "$i" -lt "$POLL_TRIES" ] || fail "$1: leak: $((ready - exited)) ready fake Chrome(s) never exited after the check stopped"
        sleep 0.05
    done
    launched=$(lines "$1" launch launch)
    if [ "$launched" -gt "$ready" ]; then
        echo "restart UI tmp note: $1: $((launched - ready)) fake Chrome(s) stopped before ready (start race, not a leak)"
    fi
}
# Waits until run $1 has a Chrome profile in t and a ready fake Chrome.
wait_ready() {
    i=0
    until [ "$(left "$t")" != 0 ] && [ "$(lines "$1" ready ready)" != 0 ]; do
        i=$((i + 1))
        [ "$i" -lt "$POLL_TRIES" ] || fail "$1: no Chrome profile and ready fake Chrome appeared"
        sleep 0.05
    done
}
# id1-shots is the fixed screenshot dir (ID1_ART default), kept on purpose.
left() { find "$1" -mindepth 1 -maxdepth 1 -name 'id1-*' ! -name id1-shots | wc -l | tr -d ' '; }

export ID1_CHROME_ONLY=1
export NODE_OPTIONS="--require $readyjs"
export HUSH_FAKE_CHROME_STOP_MS="$STOP_MS"

# Normal exit.
fresh_tmp normal
TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_TEST_WAIT_S=5 \
    node tests/check_restart_ui.cjs >/dev/null 2>&1 || fail "chrome-only run failed"
settled normal
[ "$(left "$t")" = 0 ] || fail "normal exit left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (normal exit)"

# Failure exit with Chrome up: it lists no page target.
fresh_tmp nopage
set +e
ID1_CHROME_ONLY= TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_NO_PAGE=1 \
    HUSH_TEST_WAIT_S=5 node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "no page target want exit 1, got $rc"
settled nopage
[ "$(left "$t")" = 0 ] || fail "failure with Chrome up left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (failure with Chrome up)"

# Failure exit after a relaunch: Chrome never answers inside the deadline,
# so both launches are stopped and their profiles removed.
fresh_tmp never
set +e
TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_DELAY_MS="$NEVER_READY_MS" \
    HUSH_TEST_WAIT_S=1 node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "never-ready Chrome want exit 1, got $rc"
settled never
[ "$(left "$t")" = 0 ] || fail "relaunch failure left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (relaunch failure)"

# Failure exit (Chrome exits early).
fresh_tmp failed
set +e
TMPDIR="$t" HUSH_CHROME_BIN="$exitfake" HUSH_TEST_WAIT_S=5 \
    node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "early-exit Chrome want exit 1, got $rc"
[ "$(left "$t")" = 0 ] || fail "failure exit left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (failure exit)"

# Thrown error: Chrome is up but its page socket refuses the CDP connect.
fresh_tmp thrown
set +e
ID1_CHROME_ONLY= TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_TEST_WAIT_S=5 \
    node tests/check_restart_ui.cjs >/dev/null 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "refused CDP connect want exit 1, got $rc"
settled thrown
[ "$(left "$t")" = 0 ] || fail "thrown error left $(left "$t") id1-* dirs"
echo "restart UI tmp ok (thrown error)"

# A throw and a rejection from a callback outside the check's promises,
# while it waits on a Chrome that never answers.
for how in throw reject; do
    fresh_tmp "cb$how"
    set +e
    TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_DELAY_MS="$NEVER_READY_MS" \
        HUSH_FAKE_THROW="$how" HUSH_TEST_WAIT_S=30 \
        node --require "$throwjs" tests/check_restart_ui.cjs >/dev/null 2>&1
    rc=$?
    set -e
    [ "$rc" -eq 1 ] || fail "callback $how want exit 1, got $rc"
    settled "cb$how"
    [ "$(left "$t")" = 0 ] || fail "callback $how left $(left "$t") id1-* dirs"
    echo "restart UI tmp ok (callback $how)"
done

# Signals, while it waits on a Chrome that never answers.
for sig in TERM INT; do
    fresh_tmp "sig$sig"
    TMPDIR="$t" HUSH_CHROME_BIN="$fake" HUSH_FAKE_CHROME_DELAY_MS="$NEVER_READY_MS" \
        HUSH_TEST_WAIT_S=30 node tests/check_restart_ui.cjs >/dev/null 2>&1 &
    pid=$!
    wait_ready "sig$sig"
    kill -s "$sig" "$pid"
    set +e
    wait "$pid"
    rc=$?
    set -e
    pid=""
    [ "$rc" -ne 0 ] || fail "SIG$sig: the check must not exit 0"
    settled "sig$sig"
    [ "$(left "$t")" = 0 ] || fail "SIG$sig left $(left "$t") id1-* dirs"
    echo "restart UI tmp ok (SIG$sig)"
done
echo "restart UI tmp ok"
