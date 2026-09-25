#!/bin/sh
# Behaviour UI proof for ID-1 restart/backup (headless system Chrome over
# CDP, Node standard library only: no npm packages, nothing installed).
# Fails loudly when node or Chrome is missing.
set -eu
cd "$(dirname "$0")/.."
command -v node >/dev/null 2>&1 || {
    echo "restart UI check failed: node is required" >&2
    exit 1
}
CHROME=""
for bin in "${HUSH_CHROME_BIN:-}" google-chrome chromium chromium-browser; do
    [ -n "$bin" ] || continue
    if command -v "$bin" >/dev/null 2>&1; then
        CHROME="$bin"
        break
    fi
done
[ -n "$CHROME" ] || {
    echo "restart UI check failed: no Chrome on PATH (need google-chrome or chromium)" >&2
    exit 1
}
HUSH_CHROME_BIN="$CHROME" node tests/check_restart_ui.cjs
