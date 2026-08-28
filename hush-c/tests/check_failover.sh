#!/bin/sh
# Ranked provider failover: a robot with an unconfigured primary (goose, no
# config.yaml) must fall back to its next ready runtime (grok-build), not
# emit goose's "No provider configured" error.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18782
log=$(mktemp)
home=$(mktemp -d)
pid=""

cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "$home"
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
export HUSH_HOME="$home/.hush"
export HUSH_CONFIG_DIR="$home/.config/hush"
unset XDG_CONFIG_HOME
mkdir -p "$home/bin" "$home/.grok" "$home/.config/hush" "$home/.hush"
# grok-build is the fallback: fake it so a job succeeds with a marker.
printf '%s\n' '#!/bin/sh' 'printf "%s\n" "FALLBACK_GROK_MARKER"' \
    > "$home/bin/grok"
chmod 0755 "$home/bin/grok"
# No goose binary and no goose config.yaml: goose is unready, so failover must
# pick grok-build.
PATH="$home/bin:$PATH"
export PATH
printf '%s\n' '{"ok":true}' > "$home/.grok/auth.json"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"

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
