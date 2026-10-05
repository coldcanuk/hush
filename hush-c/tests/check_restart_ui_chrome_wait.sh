#!/bin/sh
# #265 pins for Chrome DevTools ready wait.
# Positive: HTTP poll works when stderr never prints DevTools.
# Negative (a): never-ready + short WAIT_S → exit 1, exactly 2 launches,
#   deadline diagnostic, finishes quickly (relaunch + knob + fail text).
# Negative (b): immediate Chrome exit 3 → exit 1, exactly 1 launch,
#   "Chrome exited early code=3" (no relaunch on early exit).
# Negative (c): HUSH_TEST_WAIT_S=abc → knob reject.
set -eu
cd "$(dirname "$0")/.."
fail() { echo "restart UI chrome wait check failed: $1" >&2; exit 1; }

command -v node >/dev/null 2>&1 || fail "node is required"
node -e 'process.exit(typeof WebSocket === "function" ? 0 : 1)' \
    || fail "node has no global WebSocket (need node >= 22)"

fake="$(pwd)/tests/fake-chrome-devtools.sh"
countfake="$(pwd)/tests/fake-chrome-count.sh"
exitfake="$(pwd)/tests/fake-chrome-exit.sh"
[ -x "$fake" ] || fail "missing $fake"
[ -x "$countfake" ] || fail "missing $countfake"
[ -x "$exitfake" ] || fail "missing $exitfake"

# --- Positive: HTTP poll without stderr DevTools line ---
export HUSH_CHROME_BIN="$fake"
export HUSH_FAKE_CHROME_DELAY_MS="${HUSH_FAKE_CHROME_DELAY_MS:-250}"
export HUSH_TEST_WAIT_S="${HUSH_TEST_WAIT_S:-5}"
export ID1_CHROME_ONLY=1
unset HUSH_FAKE_CHROME_LAUNCH_LOG || true
out=$(node tests/check_restart_ui.cjs 2>&1) || fail "chrome-only wait failed: $out"
echo "$out" | grep -q 'restart UI chrome ready' \
    || fail "expected chrome ready line, got: $out"
echo "restart UI chrome wait ok (http poll)"

# --- Default 30s: HUSH_TEST_WAIT_S unset → ready line shows deadline 30000ms ---
unset HUSH_TEST_WAIT_S || true
export HUSH_CHROME_BIN="$fake"
export HUSH_FAKE_CHROME_DELAY_MS=250
export ID1_CHROME_ONLY=1
out=$(node tests/check_restart_ui.cjs 2>&1) || fail "default-30 wait failed: $out"
echo "$out" | grep -q 'deadline 30000ms' \
    || fail "default 30s pin want deadline 30000ms, got: $out"
echo "restart UI chrome wait ok (default 30000ms)"

# --- (a) never-ready: exit 1, exactly 2 launches, deadline text, fast ---
tmpdir=$(mktemp -d)
log="$tmpdir/launches"
: > "$log"
export HUSH_CHROME_BIN="$countfake"
export HUSH_FAKE_CHROME_LAUNCH_LOG="$log"
export HUSH_FAKE_CHROME_DELAY_MS=60000
export HUSH_TEST_WAIT_S=1
export ID1_CHROME_ONLY=1
t0=$(date +%s)
set +e
err=$(node tests/check_restart_ui.cjs 2>&1)
rc=$?
set -e
t1=$(date +%s)
elapsed=$((t1 - t0))
n=$(wc -l < "$log" | tr -d ' ')
# CoS r2: assert exit code 1 (not merely a message + 2 launches).
[ "$rc" -eq 1 ] || fail "(a) want exit 1, got $rc; out=$err"
[ "$n" -eq 2 ] || fail "(a) want exactly 2 launches, got $n; out=$err"
echo "$err" | grep -q 'Chrome printed no DevTools URL: deadline 1000ms elapsed' \
    || fail "(a) want deadline diagnostic, got: $err"
[ "$elapsed" -le 5 ] || fail "(a) want wall under ~5s, got ${elapsed}s"
echo "restart UI chrome wait ok (a never-ready relaunch)"

# --- (b) early exit: exit 1, exactly 1 launch, exited-early text ---
: > "$log"
export HUSH_CHROME_BIN="$exitfake"
export HUSH_FAKE_CHROME_LAUNCH_LOG="$log"
unset HUSH_FAKE_CHROME_DELAY_MS || true
export HUSH_TEST_WAIT_S=5
export ID1_CHROME_ONLY=1
set +e
err=$(node tests/check_restart_ui.cjs 2>&1)
rc=$?
set -e
n=$(wc -l < "$log" | tr -d ' ')
[ "$rc" -eq 1 ] || fail "(b) want exit 1, got $rc; out=$err"
[ "$n" -eq 1 ] || fail "(b) want exactly 1 launch, got $n; out=$err"
echo "$err" | grep -q 'Chrome exited early code=3' \
    || fail "(b) want early-exit diagnostic, got: $err"
echo "restart UI chrome wait ok (b early exit no relaunch)"

# --- (c) bad knob ---
export HUSH_CHROME_BIN="$fake"
unset HUSH_FAKE_CHROME_LAUNCH_LOG || true
export HUSH_TEST_WAIT_S=abc
export ID1_CHROME_ONLY=1
set +e
err=$(node tests/check_restart_ui.cjs 2>&1)
rc=$?
set -e
[ "$rc" -eq 1 ] || fail "(c) want exit 1 for bad knob, got $rc; out=$err"
echo "$err" | grep -q 'HUSH_TEST_WAIT_S must be a positive number' \
    || fail "(c) want knob error, got: $err"
echo "restart UI chrome wait ok (c bad knob)"

rm -rf "$tmpdir"
echo "restart UI chrome wait ok"
