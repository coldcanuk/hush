#!/bin/sh
# Smoke-test PWA routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18765
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
fail() { echo "PWA check failed: $1" >&2; exit 1; }
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'rel="manifest"' || fail "HTML missing manifest link"
echo "$html" | grep -q 'serviceWorker' || fail "HTML missing SW register"
mf=$(curl -sf "http://127.0.0.1:${port}/manifest.webmanifest")
echo "$mf" | grep -q '"short_name"' || fail "manifest short_name"
echo "$mf" | grep -q '192x192' || fail "manifest 192"
echo "$mf" | grep -q '512x512' || fail "manifest 512"
echo "$mf" | grep -q '"standalone"' || fail "manifest display"
curl -sf "http://127.0.0.1:${port}/sw.js" | grep -q 'HUSH_CACHE' || fail "sw.js"
png192=$(curl -sf "http://127.0.0.1:${port}/icon-192.png" | od -An -tx1 | head -1)
echo "$png192" | grep -q '89 50 4e 47' || fail "icon-192 not PNG"
png512=$(curl -sf "http://127.0.0.1:${port}/icon-512.png" | od -An -tx1 | head -1)
echo "$png512" | grep -q '89 50 4e 47' || fail "icon-512 not PNG"
curl -sf "http://127.0.0.1:${port}/apple-touch-icon.png" >/dev/null || fail "apple-touch-icon"
echo "PWA routes ok"
