#!/bin/sh
# Smoke-test STUN/TURN and conference routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
. ./tests/hush_free_port.sh
. ./tests/hush_pair.sh
# Everything this script makes, empty until made. cleanup removes only
# these: the relay, its log, and the mktemp config, pass-store and home dirs
# (the pass store holds the hive's nsecs). The traps go in before the first
# mktemp, so no exit path skips them. EXIT runs cleanup once; HUP, INT and
# TERM exit nonzero through it, since dash runs no EXIT trap on a signal.
pid=""
log=""
cfg=""
test_home=""
pass_dir=""
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
    fi
    if [ -n "$log" ]; then
        rm -f "$log"
    fi
    for made in "$cfg" "$pass_dir" "$test_home"; do
        if [ -n "$made" ]; then
            rm -rf "$made"
        fi
    done
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="$test_home/hush"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
pass_dir="$(mktemp -d)"
export HUSH_FAKE_PASS_DIR="$pass_dir"
# The paired window's X-Hush-Nonce rides along: owner Yes/No answers need it (#336).
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" \
    -H "X-Hush-Nonce: $(cat "${HUSH_HOME:-/nonexistent}/ui.nonce" 2>/dev/null || true)" \
    -b "${HUSH_HOME:-/nonexistent}/ui.jar" "$@"; }

bin=./hush-relay
port=$(hush_free_port) || exit 1
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/turn" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
fail() { echo "turn check failed: $1" >&2; exit 1; }
hush_pair "$port" "$log" "$HUSH_HOME/ui.jar" || fail "relay printed no working pairing link"
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
# The body must reach the POST handler (the old GET guard swallowed it).
hostset=$(curl -sf -X POST "http://127.0.0.1:${port}/api/turn" \
    -H 'Content-Type: application/json' \
    -d '{"host":"turn.example.test"}')
echo "$hostset" | grep -q '"host":"turn.example.test"' || fail "turn post host"
echo "turn routes ok"
