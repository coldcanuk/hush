#!/bin/sh
# Canvas Ctrl+K rewrite must not insert a hive note.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18772
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

fail() { echo "fixup check failed: $1" >&2; exit 1; }

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
export HUSH_CONFIG_DIR="$home/.config/hush"
unset XDG_CONFIG_HOME
mkdir -p "$home/bin" "$home/.grok" "$home/.config/hush"
printf '%s\n' '#!/bin/sh' \
    'printf "%s\n" "package main"' \
    > "$home/bin/grok"
chmod 0755 "$home/bin/grok"
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

before=$(curl -sf "http://127.0.0.1:${port}/api/status")
before_n=$(printf '%s' "$before" | sed -n 's/.*"events":\([0-9]*\).*/\1/p')
test -n "$before_n" || fail "status events missing"

got=$(curl -sf -X POST "http://127.0.0.1:${port}/api/fixup" \
    -H 'Content-Type: application/json' \
    -d '{"instruction":"make it Go","text":"print hi"}')
printf '%s' "$got" | grep -q '"ok":true' || fail "fixup not ok: $got"
printf '%s' "$got" | grep -q 'package main' || fail "fixup text missing"

after=$(curl -sf "http://127.0.0.1:${port}/api/status")
after_n=$(printf '%s' "$after" | sed -n 's/.*"events":\([0-9]*\).*/\1/p')
test "$before_n" = "$after_n" || fail "fixup must not insert a hive note"

grep -q 'HUSH_AGENT_FIXUP_TURNS "1"' src/hush_agent.c \
    || fail "fixup turns must be 1"
grep -q 'HUSH_AGENT_KIND_FIXUP' src/hush_agent.c \
    || fail "missing fixup job kind"

echo "fixup rewrite ok"
