#!/bin/sh
# Mention a Grok Build robot; expect a reply_to note from the fake grok.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18771
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

fail() { echo "agent check failed: $1" >&2; exit 1; }

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
mkdir -p "$home/bin" "$home/.grok" "$home/.codex" "$home/.config/hush"
printf '%s\n' '#!/bin/sh' 'echo "Why did the robot laugh? Byte me."' \
    > "$home/bin/grok"
chmod 0755 "$home/bin/grok"
PATH="$home/bin:$PATH"
export PATH
printf '%s\n' '{"ok":true}' > "$home/.grok/auth.json"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"

prov=$(curl -sf "http://127.0.0.1:${port}/api/provider")
printf '%s' "$prov" | grep -q '"grok-build":{[^}]*"has_home":true' \
    || fail "grok auth.json should authenticate Grok"
printf '%s' "$prov" | grep -q '"codex":{[^}]*"has_home":false' \
    || fail "empty Codex dir must stay unauthenticated"

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
    -d '{"name":"Happy","system_prompt":"Tell short jokes.","provider":"grok-build","save_pass":false}')
echo "$ag" | grep -q '"slug":"happy"' || fail "happy not raised"
npub=$(printf '%s' "$ag" | sed -n 's/.*"slug":"happy"[^}]*"npub":"\([^"]*\)".*/\1/p')
test -n "$npub" || fail "happy npub missing"

st=$(curl -sf "http://127.0.0.1:${port}/api/status")
printf '%s' "$st" | grep -q '"thinking"' || fail "status missing thinking"

sent=$(curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} Hello. Tell me a joke\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}")
echo "$sent" | grep -q '"ok":true' || fail "mention event not stored"

got=""
i=0
while [ "$i" -lt 40 ]; do
    got=$(curl -sf "http://127.0.0.1:${port}/api/events")
    if printf '%s' "$got" | grep -q '"reply_to":"' && \
       printf '%s' "$got" | grep -q 'Byte me'; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
printf '%s' "$got" | grep -q '"reply_to":"' || fail "no threaded reply"
printf '%s' "$got" | grep -q 'Byte me' || fail "grok reply missing"

root=$(printf '%s' "$got" | sed -n 's/.*"id":"\([^"]*\)","pubkey":"[^"]*","kind":1,"created_at":[0-9]*,"content":"nostr:[^"]* Hello. Tell me a joke".*/\1/p')
test -n "$root" || root=$(printf '%s' "$got" | sed -n 's/.*"reply_to":"\([^"]*\)".*/\1/p')
test -n "$root" || fail "root id missing"
follow=$(curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"another\",\"kind\":1,\"channel\":\"general\",\"reply_to\":\"${root}\",\"mention_0\":\"${npub}\"}")
echo "$follow" | grep -q '"ok":true' || fail "follow-up not stored"
got=$(curl -sf "http://127.0.0.1:${port}/api/events")
printf '%s' "$got" | grep -q "\"content\":\"another\"" || fail "follow-up content missing"
printf '%s' "$got" | grep -q "\"reply_to\":\"${root}\"" || fail "follow-up missing root e tag"
grep -q -- '--cwd' src/hush_agent.c || fail "grok argv missing --cwd"
grep -q -- '--max-turns' src/hush_agent.c || fail "grok argv missing --max-turns"
grep -q 'HUSH_AGENT_GROK_TURNS "2"' src/hush_agent.c || fail "grok turns must be 2"
grep -q -- '--disallowed-tools' src/hush_agent.c || fail "grok argv missing denylist"
grep -q -- '--reasoning-effort' src/hush_agent.c || fail "grok argv missing reasoning"
grep -q 'HUSH_AGENT_GROK_EFFORT "low"' src/hush_agent.c || fail "grok effort must be low"
grep -q 'HUSH_AGENT_THREAD_HEAD' src/hush_agent.c || fail "grok must receive a thread transcript"
grep -q 'hush_agent_fill_thread' src/hush_agent.c || fail "missing thread transcript fill"
grep -q 'No preamble-only replies' src/hush_agent.c || fail "hygiene must forbid preamble-only replies"
grep -q 'Fulfill the last human ask' src/hush_agent.c || fail "hygiene must fulfill the last human ask"

echo "agent mention reply ok"
