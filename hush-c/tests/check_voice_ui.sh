#!/bin/sh
# VOICE V1 browser pins (PLAN_VOICE P1.2, P1.3, P1.4, D9 mic-only, relay
# chip): headless system Chrome over CDP, Node standard library only.
# Needs node >= 22 (global WebSocket) and Chrome/Chromium on PATH. When
# either is missing it prints SKIP on a dev box, but fails when CI is set.
set -eu
cd "$(dirname "$0")/.."
missing() {
    if [ -n "${CI:-}" ]; then
        echo "voice UI check FATAL: $1 (CI is set, so this check may not skip)" >&2
        exit 1
    fi
    echo "SKIP voice UI check: $1 (install node >= 22 and Chrome to run it; fatal when CI is set)"
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
HUSH_CHROME_BIN="$CHROME" node tests/check_voice_ui.cjs
