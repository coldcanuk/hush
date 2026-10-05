#!/bin/sh
# #265 pin: Chrome DevTools ready wait must not depend on the stderr line.
# Fake Chrome never prints "DevTools listening on"; it only serves /json/list
# after a short delay. The hardened waiter (HTTP poll) must pass. A mutant
# that scrapes stderr only will fail.
set -eu
cd "$(dirname "$0")/.."
fail() { echo "restart UI chrome wait check failed: $1" >&2; exit 1; }

command -v node >/dev/null 2>&1 || fail "node is required"
node -e 'process.exit(typeof WebSocket === "function" ? 0 : 1)' \
    || fail "node has no global WebSocket (need node >= 22)"

fake="$(pwd)/tests/fake-chrome-devtools.sh"
[ -x "$fake" ] || fail "missing $fake"

# Delay past a few poll ticks; still well under HUSH_TEST_WAIT_S.
export HUSH_CHROME_BIN="$fake"
export HUSH_FAKE_CHROME_DELAY_MS="${HUSH_FAKE_CHROME_DELAY_MS:-250}"
export HUSH_TEST_WAIT_S="${HUSH_TEST_WAIT_S:-5}"
export ID1_CHROME_ONLY=1

out=$(node tests/check_restart_ui.cjs 2>&1) || fail "chrome-only wait failed: $out"
echo "$out" | grep -q 'restart UI chrome ready' \
    || fail "expected chrome ready line, got: $out"
# Guard: fake must not have unlocked via stderr scrape.
echo "$out" | grep -qi 'DevTools listening' \
    && fail "fake Chrome must not print DevTools listening (got: $out)"
echo "restart UI chrome wait ok"
