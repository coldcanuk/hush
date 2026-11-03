#!/bin/sh
# Ops CH0_HARD pin for M1 (channels only): no thread UI in any state, theme
# or viewport (headless system Chrome over CDP, Node standard library only).
# Needs node >= 22 (global WebSocket) and Chrome/Chromium on PATH. When
# either is missing it prints SKIP on a dev box, but fails when CI is set,
# so CI can never pass without running it.
set -eu
cd "$(dirname "$0")/.."
missing() {
    if [ -n "${CI:-}" ]; then
        echo "CH0 UI check FATAL: $1 (CI is set, so this check may not skip)" >&2
        exit 1
    fi
    echo "SKIP CH0 UI check: $1 (install node >= 22 and Chrome to run it; fatal when CI is set)"
    exit 0
}
command -v node >/dev/null 2>&1 || missing "node is required"
node -e 'process.exit(typeof WebSocket === "function" ? 0 : 1)' \
    || missing "node has no global WebSocket (need node >= 22)"
CHROME=""
for bin in "${HUSH_CHROME_BIN:-}" google-chrome chromium chromium-browser; do
    [ -n "$bin" ] || continue
    if command -v "$bin" >/dev/null 2>&1; then
        CHROME="$bin"
        break
    fi
done
[ -n "$CHROME" ] || missing "no Chrome on PATH (need google-chrome or chromium)"
# Run with TMPDIR pointed at a private dir and require that the run removed
# every ch0-* temp dir it made (Chrome profile, relay home and config).
run_tmp=$(mktemp -d)
trap 'rm -rf "$run_tmp"' EXIT
set +e
TMPDIR="$run_tmp" HUSH_CHROME_BIN="$CHROME" node tests/check_ch0_ui.cjs
rc=$?
set -e
[ "$rc" -eq 0 ] || exit "$rc"
left=$(find "$run_tmp" -mindepth 1 -maxdepth 1 -name 'ch0-*' -exec basename {} \;)
if [ -n "$left" ]; then
    echo "CH0 UI check failed: the run left ch0-* temp dirs: $(echo $left)" >&2
    exit 1
fi
echo "CH0 UI run left no ch0-* temp dirs"
