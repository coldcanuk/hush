#!/bin/sh
# agy (Antigravity) robot: must spawn `agy -p`, not grok, and the combined
# prompt (system prompt + rules + note) must reach the -p argument.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18780
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

fail() { echo "agy check failed: $1" >&2; exit 1; }

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
printf '%s\n' '#!/bin/sh' \
    'log="${HUSH_CONFIG_DIR}/agy-p.log"' \
    'prev=""' \
    'for a in "$@"; do' \
    '  if [ "$prev" = "-p" ]; then printf "%s\n" "$a" >> "$log"; fi' \
    '  prev="$a"' \
    'done' \
    'printf "%s\n" "AGY_REPLY_MARKER"' \
    > "$home/bin/agy"
chmod 0755 "$home/bin/agy"
# Seeded robots are grok-build; provide a stub so they stay quiet.
printf '%s\n' '#!/bin/sh' 'printf "%s\n" "grok stub"' > "$home/bin/grok"
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

ag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Sage","system_prompt":"Answer briefly.","provider":"agy","save_pass":false}')
echo "$ag" | grep -q '"slug":"sage"' || fail "sage not raised"
npub=$(printf '%s' "$ag" | sed -n 's/.*"slug":"sage"[^}]*"npub":"\([^"]*\)".*/\1/p')
test -n "$npub" || fail "sage npub missing"

curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} Hello from a human\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}" \
    >/dev/null

got=""
i=0
while [ "$i" -lt 40 ]; do
    got=$(curl -sf "http://127.0.0.1:${port}/api/events")
    printf '%s' "$got" | grep -q 'AGY_REPLY_MARKER' && break
    i=$((i + 1))
    sleep 0.05
done
printf '%s' "$got" | grep -q 'AGY_REPLY_MARKER' || fail "agy reply missing"

# The agy binary must have been invoked with -p and received the combined
# system prompt + human note (proving it ran agy, not grok).
test -f "$home/.config/hush/agy-p.log" || fail "agy -p log missing"
grep -q 'Answer briefly' "$home/.config/hush/agy-p.log" \
    || fail "agy prompt missing robot system prompt"
grep -q 'Hello from a human' "$home/.config/hush/agy-p.log" \
    || fail "agy prompt missing human note"

echo "agy routes ok"
