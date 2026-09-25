#!/bin/sh
# ID-1 restart honesty + pass-missing coverage (issue #211).
# Without the fix this fails: no pass_available in /api/session and none of
# the honesty copy (Click Begin cue, restart note, pass-missing backup note,
# header reset) in the served UI.
set -eu
cd "$(dirname "$0")/.."

bin=./hush-relay
port=18771
log=$(mktemp)
home=$(mktemp -d)
cfg=$(mktemp -d)
export HUSH_HOME="$home"
export HUSH_CONFIG_DIR="$cfg"
# No `pass` on a virgin VM: point the helper at a path that cannot run so a
# save attempt records pass_error instead of persisting the nsec.
export HUSH_PASS_HELPER="/nonexistent-hush-pass-helper"
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME}/session.token" 2>/dev/null || true)" "$@"; }

pid=""
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "$home" "$cfg"
}
trap cleanup EXIT
fail() { echo "restart check failed: $1" >&2; exit 1; }

start_relay() {
    "$bin" --no-open "$port" >"$log" 2>&1 &
    pid=$!
    i=0
    while [ "$i" -lt 50 ]; do
        if curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.05
    done
    fail "relay did not come up"
}

# --- A. pass-missing surface ---
start_relay
sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$sess" | grep -q '"pass_available":false' || fail "session should report pass_available false without pass"
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'Click Begin to continue' || fail "splash must cue clicking Begin"
echo "$html" | grep -q 'did not survive the restart' || fail "UI must explain the post-restart re-import"
echo "$html" | grep -q 'pass is not installed' || fail "backup must name the pass-missing reason"
echo "$html" | grep -q 'Setup continues without saving' || fail "backup must say setup continues"
echo "$html" | grep -q 'local hive mind' || fail "header must know its neutral default"
echo "$html" | grep -q 'session.logged_in && session.vibe' || fail "header must gate the hive name on login"

# --- B. restart honesty with the same HOME ---
created=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"create"}')
echo "$created" | grep -q '"logged_in":true' || fail "create did not log in"
npub_before=$(printf '%s' "$created" | sed -n 's/.*"npub":"\(npub1[^"]*\)".*/\1/p')
test -n "$npub_before" || fail "create returned no npub"
acked=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"ack_backup","save_pass":true}')
echo "$acked" | grep -q '"pass_error":"pass helper failed"' || fail "missing pass must record pass_error, got: $acked"
echo "$acked" | grep -q '"backup_acked":true' || fail "setup must continue past a failed save"
vibe=$(curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' -d '{"name":"HQ","about":"restart probe"}')
echo "$vibe" | grep -q '"ready":true' || fail "vibe should ready the hive"
token_before=$(cat "$HUSH_HOME/session.token")
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
start_relay
restored=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$restored" | grep -q '"has_vibe":true' || fail "restart should restore vibe"
echo "$restored" | grep -q '"name":"HQ"' || fail "restart should keep vibe name"
echo "$restored" | grep -q '"logged_in":false' || fail "restart without pass must not claim login"
echo "$restored" | grep -q '"ready":false' || fail "restart without login must not claim ready"
echo "$restored" | grep -q '"npub":""' || fail "restart without login must clear npub"
token_after=$(cat "$HUSH_HOME/session.token")
test "$token_before" = "$token_after" || fail "session.token must survive restart"

# --- C. restore still works when pass works ---
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
home2=$(mktemp -d)
cfg2=$(mktemp -d)
export HUSH_HOME="$home2"
export HUSH_CONFIG_DIR="$cfg2"
start_relay
created2=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"create"}')
npub2=$(printf '%s' "$created2" | sed -n 's/.*"npub":"\(npub1[^"]*\)".*/\1/p')
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"ack_backup","save_pass":true}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' -d '{"name":"HQ2","about":"pass probe"}' >/dev/null
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
start_relay
restored2=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$restored2" | grep -q '"logged_in":true' || fail "restart with pass should stay logged in"
echo "$restored2" | grep -q "\"npub\":\"$npub2\"" || fail "restart with pass should keep npub"
rm -rf "$home2" "$cfg2" "$HUSH_FAKE_PASS_DIR"
echo "restart honesty ok"
