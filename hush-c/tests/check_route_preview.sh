#!/bin/sh
# Ops M1 F1/FU1 unit pin: the composer's route line in demo/index.html
# (paintRoute and the #route-pick chooser). Node standard library only, no
# Chrome, no relay. Prints SKIP without node, but fails when CI is set
# (same rule as check_ask_void.sh).
set -eu
cd "$(dirname "$0")/.."
if ! command -v node >/dev/null 2>&1; then
    if [ -n "${CI:-}" ]; then
        echo "route preview check FATAL: node is required (CI is set, so this check may not skip)" >&2
        exit 1
    fi
    echo "SKIP route preview check: node is required (fatal when CI is set)"
    exit 0
fi
node tests/check_route_preview.cjs demo/index.html
