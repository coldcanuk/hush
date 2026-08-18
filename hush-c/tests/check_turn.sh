#!/bin/sh
# Smoke-test STUN/TURN and conference routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18767
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; rm -f "$log"; rm -rf "$cfg"; }
trap cleanup EXIT
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/turn" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
fail() { echo "turn check failed: $1" >&2; exit 1; }
turn=$(curl -sf "http://127.0.0.1:${port}/api/turn")
echo "$turn" | grep -q '"compiled":' || fail "turn compiled field"
echo "$turn" | grep -q '"mode":"off"' || fail "turn starts off"
ice=$(curl -sf "http://127.0.0.1:${port}/api/ice")
echo "$ice" | grep -q 'iceServers' || fail "ice servers"
st=$(curl -sf "http://127.0.0.1:${port}/api/status")
echo "$st" | grep -q '"whisper":' || fail "whisper flag"
echo "$st" | grep -q '"turn_running":false' || fail "turn not running"
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'id="settings-btn"' || fail "settings button"
echo "$html" | grep -q 'id="call-btn"' || fail "call button"
echo "$html" | grep -q 'Enable STUN/TURN' || fail "enable copy"
echo "$html" | grep -q 'Daemon mode' || fail "daemon copy"
echo "$html" | grep -q 'visibility' || fail "vibe visibility UI"
# Identity + private vibe
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"create"}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"ack_backup"}' >/dev/null
vibe=$(curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' \
    -d '{"name":"HQ","about":"lab","visibility":"private"}')
echo "$vibe" | grep -q '"visibility":"private"' || fail "private vibe"
echo "$vibe" | grep -q '"discoverable":false' || fail "not discoverable"
echo "$vibe" | grep -q '"join_token":"' || fail "join token"
sig=$(curl -sf -X POST "http://127.0.0.1:${port}/api/signal" \
    -H 'Content-Type: application/json' \
    -d '{"t":"join","from":"a","to":"*","role":"human","channel":"general"}')
echo "$sig" | grep -q '"ok":true' || fail "signal post"
ev=$(curl -sf "http://127.0.0.1:${port}/api/events")
echo "$ev" | grep -q '"kind":25000' || fail "signal stored"
# Enable without coturn must not crash
posted=$(curl -sf -X POST "http://127.0.0.1:${port}/api/turn" \
    -H 'Content-Type: application/json' \
    -d '{"enabled":true,"daemon":false}')
echo "$posted" | grep -q '"compiled":' || fail "turn post"
echo "turn routes ok"
