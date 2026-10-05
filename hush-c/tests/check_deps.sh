#!/bin/sh
# Names missing make-test tools. Xvfb and gcc are reported and never fail
# this check. Chrome/Chromium and Node >= 22 do.
set -eu
missing=""
add() {
    if [ -z "$missing" ]; then
        missing=$1
    else
        missing="$missing $1"
    fi
}
have() { command -v "$1" >/dev/null 2>&1; }

for tool in sh python3 curl node git cc make ps awk; do
    have "$tool" || add "$tool"
done
if ! stat -c %a . >/dev/null 2>&1; then
    add "GNU stat"
fi

fail=0
if [ -n "$missing" ]; then
    echo "check-deps: missing required tool(s): $missing" >&2
    fail=1
fi

if have node; then
    node_major=$(node -p 'Number(String(process.versions.node).split(".")[0])')
    if [ "$node_major" -lt 22 ]; then
        echo "check-deps: Node 22 or newer is required (found $(node --version))" >&2
        fail=1
    fi
fi

found_chrome=""
for bin in "${HUSH_CHROME_BIN:-}" google-chrome chromium chromium-browser; do
    [ -n "$bin" ] || continue
    if have "$bin"; then
        found_chrome=$bin
        break
    fi
done
if [ -z "$found_chrome" ]; then
    echo "check-deps: Chrome or Chromium is required (no HUSH_CHROME_BIN, google-chrome, chromium, or chromium-browser on PATH)" >&2
    fail=1
fi

if have Xvfb; then
    echo "check-deps: optional Xvfb: found"
else
    echo "check-deps: optional Xvfb: not found (window checks skip; not required)"
fi
if have gcc; then
    echo "check-deps: optional gcc: found"
else
    echo "check-deps: optional gcc: not found (not required)"
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check-deps: ok"
