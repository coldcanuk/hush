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
printf '%s\n' '#!/bin/sh' \
    'log="${HUSH_CONFIG_DIR}/grok-p.log"' \
    'prev=""' \
    'for a in "$@"; do' \
    '  if [ "$prev" = "-p" ]; then printf "%s\n" "$a" >> "$log"; fi' \
    '  prev="$a"' \
    'done' \
    'printf "%s\n" "Why did the robot laugh? Byte me."' \
    'printf "go:\tfmt\n"' \
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
hex=$(printf '%s' "$ag" | sed -n 's/.*"slug":"happy"[^}]*"pubkey":"\([^"]*\)".*/\1/p')
test -n "$hex" || fail "happy pubkey missing"

st=$(curl -sf "http://127.0.0.1:${port}/api/status")
printf '%s' "$st" | grep -q '"thinking"' || fail "status missing thinking"

sent=$(curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} Hello. Tell me a joke\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}")
echo "$sent" | grep -q '"ok":true' || fail "mention event not stored"

# M5 server ack note proof (atomic): after a successful mention dispatch,
# the server emits a real kind-1 note authored by the robot pubkey (hex)
# with content "Mention received." (threaded via h/e, T=confirm to avoid loops).
# This is the durable server-side receipt instead of pure client render.
got_ack=$(curl -sf "http://127.0.0.1:${port}/api/events")
printf '%s' "$got_ack" | python3 -c '
import json, sys
hexp = sys.argv[1]
data = json.loads(sys.stdin.read())
for e in (data.get("events") or []):
    if (e.get("pubkey") or "") == hexp:
        if "Mention received" in (e.get("content") or ""):
            sys.exit(0)
print("MISSING_SERVER_ACK_NOTE_FROM_ROBOT")
sys.exit(1)
' "${hex}" || fail "server must emit ack note authored by the mentioned robot"

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

# Proof for M1 ack slice: server must emit the p-tag as authoritative "mentions" array
# so UI can render truthful Discord-style acks instead of content heuristic only.
printf '%s' "$got" | python3 -c '
import json, sys
npub = sys.argv[1]
data = json.loads(sys.stdin.read())
for e in (data.get("events") or []):
    c = e.get("content") or ""
    if "Hello. Tell me a joke" in c:
        ments = e.get("mentions") or []
        if npub in ments:
            sys.exit(0)
print("MISSING_MENTIONS_IN_ROOT")
sys.exit(1)
' "${npub}" || fail "root event must include authoritative mentions array for the p-tag"

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
grep -q -- '--no-memory' src/hush_agent.c || fail "grok argv missing --no-memory"
grep -q -- '--disallowed-tools' src/hush_agent.c || fail "grok argv missing denylist"
grep -q -- '--reasoning-effort' src/hush_agent.c || fail "grok argv missing reasoning"
grep -q 'HUSH_AGENT_GROK_EFFORT "low"' src/hush_agent.c || fail "grok effort must be low"
grep -q 'HUSH_AGENT_THREAD_HEAD' src/hush_agent.c || fail "grok must receive a thread transcript"
grep -q 'hush_agent_fill_thread' src/hush_agent.c || fail "missing thread transcript fill"
grep -q 'No preamble-only replies' src/hush_agent.c || fail "hygiene must forbid preamble-only replies"
grep -q 'Fulfill the last human ask' src/hush_agent.c || fail "hygiene must fulfill the last human ask"
grep -q 'exactly one joke' src/hush_agent.c || fail "hygiene must demand one joke"
grep -q 'hush_agent_robot_busy' src/hush_agent.c || fail "must refuse a second job for a busy robot"
i=0
while [ "$i" -lt 40 ]; do
    if grep -q 'Byte me. go: fmt' "$HUSH_CONFIG_DIR/grok-p.log" 2>/dev/null; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
grep -q 'Byte me. go: fmt' "$HUSH_CONFIG_DIR/grok-p.log" \
    || fail "follow-up -p must flatten the prior tabbed reply"

got=$(curl -sf "http://127.0.0.1:${port}/api/events")
printf '%s' "$got" | python3 -c 'import json,sys; json.loads(sys.stdin.read())' \
    || fail "events JSON must parse with a tabbed grok reply"
printf '%s' "$got" | grep -q '\\tfmt' || fail "tab must be escaped as \\\\t"

echo "$ag" | grep -q '"name":"Happy"' || fail "happy name missing from raise"
sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$sess" | grep -q 'Sgt Major Payne' || fail "session must include Sgt Major Payne"
echo "$sess" | grep -q '"slug":"happy"' || fail "session must include raised Happy"

# Payne seed path uses existing /api/agent (wizard JS). Put Payne on grok-build
# so payneCanReply would pass (has_home from fake grok), then raise ≥2 teammates
# with the seeder prompt and a dedicated channel.
payne=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"slug":"sgt-major-payne","provider_0":"grok-build"}')
echo "$payne" | grep -F '"providers":["grok-build"]' || fail "Payne seed provider grok-build"
prompt='You are a specialist on a team seeded by Sgt Major Payne, Chief of Staff. Cooperate with sibling robots.'
scout=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d "{\"name\":\"Scout\",\"system_prompt\":\"$prompt Your station is: Scout.\",\"provider\":\"grok-build\",\"save_pass\":false}")
echo "$scout" | grep -q '"slug":"scout"' || fail "seed scout not raised"
builder=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d "{\"name\":\"Builder\",\"system_prompt\":\"$prompt Your station is: Builder.\",\"provider\":\"grok-build\",\"save_pass\":false}")
echo "$builder" | grep -q '"slug":"builder"' || fail "seed builder not raised"
team=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"name":"seeded-team"}')
echo "$team" | grep -q '"slug":"seeded-team"' || fail "seed channel missing"
managed=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"action":"manage","slug":"seeded-team","robot_0":"scout","robot_1":"builder","robot_2":"sgt-major-payne","kind":"robots","robot_reply":"mention"}')
echo "$managed" | grep -q 'scout' || fail "seed channel missing scout"
echo "$managed" | grep -q 'builder' || fail "seed channel missing builder"
echo "$managed" | grep -q 'sgt-major-payne' || fail "seed channel missing Payne"
sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$sess" | grep -q '"slug":"scout"' || fail "session missing seeded scout"
echo "$sess" | grep -q '"slug":"builder"' || fail "session missing seeded builder"
echo "$sess" | grep -q 'Sgt Major Payne' || fail "Payne still on deck after seed"
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'function seedTeam' || fail "served UI missing seedTeam"
echo "$html" | grep -q 'INV_COLS = 4' || fail "served UI compact grid not 4 cols"
if echo "$html" | grep -q 'seedInventoryDemo'; then
    fail "served UI still has fake seedInventoryDemo"
fi

echo "agent mention reply ok"
