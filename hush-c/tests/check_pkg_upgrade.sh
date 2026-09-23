#!/bin/sh
# check_pkg_upgrade.sh — WS3-C: packaging upgrade must not surprise-kill.
#
# Focused test of the shared stop helper (scripts/hush-relay-stop) plus
# static checks that the Debian/RPM scriptlets delegate to it instead of
# the old SIGTERM / sleep 1 / SIGKILL blast. Full dpkg/rpm round-trips
# are deliberately out of scope for CI; the helper carries the semantics
# and the scriptlets are thin wrappers, so testing the helper is the
# honest signal. See docs/ops/package-upgrade.md.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
helper="$root/scripts/hush-relay-stop"
prerm="$root/debian/hush-relay.prerm"
spec="$root/hush-relay.spec"

fail() { echo "pkg upgrade check failed: $1" >&2; exit 1; }

test_pids=""

cleanup() {
    # shellcheck disable=SC2086: test pid list by construction.
    if [ -n "$test_pids" ]; then
        kill -KILL $test_pids 2>/dev/null || true
        wait 2>/dev/null || true
    fi
}
trap cleanup EXIT

[ -x "$helper" ] || fail "helper missing or not executable ($helper)"
sh -n "$helper" || fail "helper has a syntax error"
sh -n "$prerm" || fail "prerm has a syntax error"

# --- static: the old blast pattern must be gone from packaging ---

# The old scripts did SIGTERM, a fixed 1s sleep, then unconditional
# SIGKILL. The new scripts poll with a generous, documented grace instead:
# both scriptlets must honor HUSH_STOP_GRACE_S and delegate to the helper.
grep -q 'HUSH_STOP_GRACE_S' "$prerm" || fail "prerm has no generous grace period"
grep -q 'HUSH_STOP_GRACE_S' "$spec" || fail "spec has no generous grace period"
# prerm must not SIGKILL outside the remove-gated fallback: every KILL it
# contains must sit on a remove-marked line.
if grep -n 'kill -KILL' "$prerm" | grep -qv 'remove'; then
    fail "prerm SIGKILL outside a remove-gated path"
fi
# Both scriptlets must delegate to the shared helper.
grep -q 'hush-relay-stop' "$prerm" || fail "prerm does not use hush-relay-stop"
grep -q 'hush-relay-stop' "$spec" || fail "spec does not use hush-relay-stop"
# RPM must handle erase vs upgrade distinctly ($1==0 erase for %preun).
grep -q '%preun' "$spec" || fail "spec missing %preun for erase handling"

spawn_stuck() {
    # A process that ignores SIGTERM: the "slow agent job reap" stand-in.
    # Ignored dispositions survive exec, so sleep never sees the TERM.
    # Redirected so the background job never holds the $(...) capture pipe
    # open (which would block until sleep exits on its own).
    sh -c 'trap "" TERM; exec sleep 60' >/dev/null 2>&1 < /dev/null &
    echo "$!"
}

spawn_polite() {
    # A process with default SIGTERM handling: dies on the first TERM.
    sleep 60 >/dev/null 2>&1 < /dev/null &
    echo "$!"
}

# --- behavior: upgrade never SIGKILLs a stuck process within the grace ---

stuck=$(spawn_stuck)
test_pids="$stuck"
kill -0 "$stuck" 2>/dev/null || fail "stuck fixture did not start"
HUSH_RELAY_PIDS="$stuck" HUSH_STOP_GRACE_S=2 sh "$helper" upgrade 2>/dev/null \
    || fail "upgrade helper exited non-zero"
kill -0 "$stuck" 2>/dev/null \
    || fail "upgrade SIGKILLed a stuck process inside the grace window"

# --- behavior: upgrade reaps a polite process without any KILL ---

polite=$(spawn_polite)
test_pids="$stuck $polite"
HUSH_RELAY_PIDS="$polite" HUSH_STOP_GRACE_S=5 sh "$helper" upgrade 2>/dev/null \
    || fail "upgrade helper exited non-zero for a polite process"
if kill -0 "$polite" 2>/dev/null; then
    fail "upgrade left a SIGTERM-polite process running past the grace"
fi
test_pids="$stuck"

# --- behavior: remove is the exceptional KILL path (after the grace) ---

HUSH_RELAY_PIDS="$stuck" HUSH_STOP_GRACE_S=2 sh "$helper" remove 2>/dev/null \
    || fail "remove helper exited non-zero"
i=0
while kill -0 "$stuck" 2>/dev/null && [ "$i" -lt 10 ]; do
    sleep 0.2
    i=$((i + 1))
done
if kill -0 "$stuck" 2>/dev/null; then
    fail "remove did not SIGKILL a stuck process after the grace"
fi
test_pids=""

# --- behavior: opt-in KILL on upgrade when the operator asks for it ---

stuck2=$(spawn_stuck)
test_pids="$stuck2"
HUSH_RELAY_PIDS="$stuck2" HUSH_STOP_GRACE_S=2 HUSH_RELAY_ALLOW_KILL=1 \
    sh "$helper" upgrade 2>/dev/null \
    || fail "opt-in upgrade helper exited non-zero"
i=0
while kill -0 "$stuck2" 2>/dev/null && [ "$i" -lt 10 ]; do
    sleep 0.2
    i=$((i + 1))
done
if kill -0 "$stuck2" 2>/dev/null; then
    fail "HUSH_RELAY_ALLOW_KILL=1 did not kill a stuck process on upgrade"
fi
test_pids=""

# --- behavior: unknown mode degrades to graceful upgrade, never kills ---

stuck3=$(spawn_stuck)
test_pids="$stuck3"
HUSH_RELAY_PIDS="$stuck3" HUSH_STOP_GRACE_S=2 sh "$helper" bogus-mode 2>/dev/null \
    || fail "unknown-mode helper exited non-zero"
kill -0 "$stuck3" 2>/dev/null \
    || fail "unknown mode killed a stuck process (must default to graceful)"
test_pids=""

echo "pkg upgrade stop ok"
