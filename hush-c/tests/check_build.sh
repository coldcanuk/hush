#!/bin/sh
# Focused check: the build stamp (version + short SHA) is baked at build
# time and visible on the main display via /api/status and served HTML.
set -eu
cd "$(dirname "$0")/.."
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="${HUSH_HOME:-$test_home/hush}"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" "$@"; }

bin=./hush-relay
port=18770
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; rm -f "$log"; rm -rf "$cfg"; }
trap cleanup EXIT
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/status" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
fail() { echo "build stamp check failed: $1" >&2; exit 1; }
st=$(curl -sf "http://127.0.0.1:${port}/api/status")
echo "$st" | grep -q '"version":"' || fail "status missing version"
echo "$st" | grep -q '"build":"' || fail "status missing build"
version=$(printf '%s' "$st" | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')
build=$(printf '%s' "$st" | sed -n 's/.*"build":"\([^"]*\)".*/\1/p')
test -n "$version" || fail "status version empty"
test -n "$build" || fail "status build empty"
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'status.build' || fail "main display never renders status.build"
echo "$html" | grep -q 'status.version' || fail "main display never renders status.version"
if [ "$build" != "unknown" ]; then
    "$bin" --help | grep -q "$build" || fail "--help hides baked build $build"
    if command -v git >/dev/null 2>&1 && git rev-parse --short HEAD >/dev/null 2>&1; then
        head=$(git rev-parse --short HEAD)
        test "$build" = "$head" || fail "baked build $build is not short HEAD $head"
    fi
fi
echo "build stamp ok ($version $build)"
