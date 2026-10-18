#!/bin/sh
# Ranked provider failover: a robot with an unconfigured primary (goose, no
# config.yaml) must fall back to its next ready runtime (grok-build), not
# emit goose's "No provider configured" error.
set -eu
cd "$(dirname "$0")/.."
. ./tests/hush_free_port.sh
. ./tests/hush_pair.sh
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="$test_home/hush"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
# The paired window's X-Hush-Nonce rides along: owner Yes/No answers need it (#336).
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" \
    -H "X-Hush-Nonce: $(cat "${HUSH_HOME:-/nonexistent}/ui.nonce" 2>/dev/null || true)" \
    -b "${HUSH_HOME:-/nonexistent}/ui.jar" "$@"; }

bin=./hush-relay
port=$(hush_free_port) || exit 1
log=$(mktemp)
base=$(mktemp -d)
home="$base/user"
bin_dir="$base/bin"
pid=""

cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "${base:-$home}"
}
trap cleanup EXIT

fail() { echo "failover check failed: $1" >&2; exit 1; }

wait_up() {
    i=0
    while [ "$i" -lt 50 ]; do
        if curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.05
    done
    return 1
}

export HOME="$home"
export HUSH_HOME="$base/home"
export HUSH_CONFIG_DIR="$base/config"
unset XDG_CONFIG_HOME
mkdir -p "$bin_dir" "$home/.grok" "$base/home" "$base/config" "${HUSH_CONFIG_DIR:-$base/config}" "${HUSH_HOME:-$base/home}"
# grok-build is the fallback: fake it so a job succeeds with a marker.
printf '%s\n' '#!/bin/sh' 'case "$1" in --version) echo "grok 0.2.50"; exit 0;; --help) echo "--permission-mode dontAsk --allow --deny --disallowed-tools"; exit 0;; esac' 'printf "%s\n" "FALLBACK_GROK_MARKER"' \
    > "$bin_dir/grok"
chmod 0755 "$bin_dir/grok"
# No goose binary and no goose config.yaml: goose is unready, so failover must
# pick grok-build.
PATH="$bin_dir:$PATH"
export PATH
printf '%s\n' '{"ok":true}' > "$home/.grok/auth.json"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"
hush_pair "$port" "$log" "$HUSH_HOME/ui.jar" || fail "relay printed no working pairing link"

curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"create"}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"ack_backup","save_pass":false}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' \
    -d '{"name":"HQ","about":"primary endpoint"}' >/dev/null

ag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Fallback","system_prompt":"Answer briefly.","provider":"goose","providers":"goose,grok-build","save_pass":false}')
echo "$ag" | grep -q '"slug":"fallback"' || fail "fallback not raised"
npub=$(printf '%s' "$ag" | sed -n 's/.*"slug":"fallback"[^}]*"npub":"\([^"]*\)".*/\1/p')
test -n "$npub" || fail "fallback npub missing"

curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} hello\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}" \
    >/dev/null

got=""
i=0
while [ "$i" -lt 40 ]; do
    got=$(curl -sf "http://127.0.0.1:${port}/api/events")
    printf '%s' "$got" | grep -q 'FALLBACK_GROK_MARKER' && break
    i=$((i + 1))
    sleep 0.05
done
printf '%s' "$got" | grep -q 'FALLBACK_GROK_MARKER' \
    || fail "failover did not run grok-build"
printf '%s' "$got" | grep -q 'error: No provider configured' \
    && fail "goose error leaked through failover"

echo "failover routes ok"
