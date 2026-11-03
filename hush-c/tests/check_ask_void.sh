#!/bin/sh
# Gauge M1-F19 unit pin: the PWA's readAskState (demo/index.html) closes
# every ask waiting in the conversation an owner note lands in and no ask
# in another conversation. Node standard library only, no Chrome, no relay.
# Prints SKIP on a dev box without node, but fails when CI is set (same
# rule as check_palette.sh).
set -eu
cd "$(dirname "$0")/.."
if ! command -v node >/dev/null 2>&1; then
    if [ -n "${CI:-}" ]; then
        echo "ask void check FATAL: node is required (CI is set, so this check may not skip)" >&2
        exit 1
    fi
    echo "SKIP ask void check: node is required (fatal when CI is set)"
    exit 0
fi
node tests/check_ask_void.cjs demo/index.html
