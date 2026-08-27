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
export HUSH_HOME="$home/.hush"
export HUSH_CONFIG_DIR="$home/.config/hush"
unset XDG_CONFIG_HOME
mkdir -p "$home/bin" "$home/.grok" "$home/.codex" "$home/.config/hush" "$home/.hush"
printf '%s\n' '#!/bin/sh' \
    'log="${HUSH_CONFIG_DIR}/grok-p.log"' \
    'prev=""' \
    'for a in "$@"; do' \
    '  if [ "$prev" = "-p" ]; then printf "P:%s\n" "$a" >> "$log"; fi' \
    '  if [ "$prev" = "--system-prompt-override" ]; then printf "S:%s\n" "$a" >> "$log"; fi' \
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
    -d '{"name":"Happy","system_prompt":"Tell short jokes.","provider":"grok-build","save_pass":false,"picture":"panel:dogs:4"}')
echo "$ag" | grep -q '"slug":"happy"' || fail "happy not raised"
echo "$ag" | grep -q 'panel:dogs:4' || fail "happy picture id not stored"
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
printf '%s' "$got_ack" | python3 -c '
import json, sys
hexp = sys.argv[1]
data = json.loads(sys.stdin.read())
n = 0
for e in (data.get("events") or []):
    if (e.get("pubkey") or "") == hexp and "Standing orders are noted." in (e.get("content") or ""):
        n += 1
if n == 1:
    sys.exit(0)
print("INTRO_COUNT", n)
sys.exit(1)
' "${hex}" || fail "robot must emit exactly one on-deck intro in chat"

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
printf '%s' "$got" | python3 -c '
import json, sys
hexp = sys.argv[1]
data = json.loads(sys.stdin.read())
n = 0
for e in (data.get("events") or []):
    if (e.get("pubkey") or "") == hexp and "Standing orders are noted." in (e.get("content") or ""):
        n += 1
if n == 1:
    sys.exit(0)
print("INTRO_COUNT_AFTER_FOLLOWUP", n)
sys.exit(1)
' "${hex}" || fail "follow-up must not emit a second intro"

sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
payne_npub=$(printf '%s' "$sess" | python3 -c 'import json,sys; p=json.loads(sys.stdin.read()).get("payne") or {}; print(p.get("npub") or "")')
payne_hex=$(printf '%s' "$sess" | python3 -c 'import json,sys; p=json.loads(sys.stdin.read()).get("payne") or {}; print(p.get("pubkey") or "")')
test -n "$payne_npub" || fail "payne npub missing"
test -n "$payne_hex" || fail "payne hex missing"
pair=$(curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} tell me a joke nostr:${payne_npub} analyze the joke, was it funny?\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\",\"mention_1\":\"${payne_npub}\"}")
echo "$pair" | grep -q '"ok":true' || fail "co-mention event not stored"
pair_got=""
i=0
while [ "$i" -lt 60 ]; do
    pair_got=$(curl -sf "http://127.0.0.1:${port}/api/events")
    printf '%s' "$pair_got" | python3 -c '
import json, sys
happy, payne = sys.argv[1], sys.argv[2]
data = json.loads(sys.stdin.read())
root = None
for e in (data.get("events") or []):
    c = e.get("content") or ""
    if "analyze the joke" in c:
        root = e.get("id")
        break
if not root:
    sys.exit(1)
h = m = 0
hold = 0
for e in (data.get("events") or []):
    rt = e.get("reply_to") or ""
    if rt != root and e.get("id") != root:
        continue
    c = e.get("content") or ""
    if "Holding" in c:
        hold += 1
    if "Standing orders are noted." not in c:
        continue
    if (e.get("pubkey") or "") == happy:
        h += 1
    if (e.get("pubkey") or "") == payne:
        m += 1
if hold:
    print("HOLDING", hold)
    sys.exit(2)
if h == 1 and m == 1:
    sys.exit(0)
print("PAIR_INTRO", h, m)
sys.exit(1)
' "${hex}" "${payne_hex}" && break
    i=$((i + 1))
    sleep 0.05
done
printf '%s' "$pair_got" | python3 -c '
import json, sys
happy, payne = sys.argv[1], sys.argv[2]
data = json.loads(sys.stdin.read())
root = None
for e in (data.get("events") or []):
    if "analyze the joke" in (e.get("content") or ""):
        root = e.get("id")
        break
h = m = 0
for e in (data.get("events") or []):
    if (e.get("reply_to") or "") != root:
        continue
    if "Standing orders are noted." not in (e.get("content") or ""):
        continue
    if (e.get("pubkey") or "") == happy:
        h += 1
    if (e.get("pubkey") or "") == payne:
        m += 1
print("PAIR_INTRO", h, m)
if h == 1 and m == 1:
    sys.exit(0)
sys.exit(1)
' "${hex}" "${payne_hex}" || fail "co-mention must intro Happy then Major once each"
printf '%s' "$pair_got" | grep -q "Holding" && fail "co-mention must not post Holding"
ce=$(curl -sf "http://127.0.0.1:${port}/api/chan-events")
printf '%s' "$ce" | grep -q '"ok":true' || fail "chan-events missing ok"
printf '%s' "$ce" | grep -q '"type":"mention"' || fail "chan-events missing mention"
printf '%s' "$ce" | grep -q '"type":"intro"' || fail "chan-events missing intro"
printf '%s' "$ce" | grep -q '"type":"follow"' || fail "chan-events missing follow"

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
grep -q 'Fulfill YOUR assignment' src/hush_agent.c || fail "hygiene must name the robot assignment"
grep -q 'Do not mention yourself' src/hush_agent.c || fail "hygiene must forbid self-mention"
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
echo "$sess" | grep -q '"name":"Major"' || fail "session must include Major"
echo "$sess" | grep -q '"name":"Sgt Major Payne"' && fail "old Payne name must not ship"
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
echo "$sess" | grep -q '"name":"Major"' || fail "Payne still on deck after seed"
echo "$sess" | grep -q '"name":"Sgt Major Payne"' && fail "old Payne name after seed"
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'function seedTeam' || fail "served UI missing seedTeam"
echo "$html" | grep -q 'INV_COLS = 4' || fail "served UI compact grid not 4 cols"
if echo "$html" | grep -q 'seedInventoryDemo'; then
    fail "served UI still has fake seedInventoryDemo"
fi

# In-order mentions: content keeps nostr: tokens in sentence position.
payne_npub=$(printf '%s' "$sess" | sed -n 's/.*"payne":{[^}]*"npub":"\([^"]*\)".*/\1/p')
test -n "$payne_npub" || fail "payne npub missing for order test"
ordered=$(curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} tell a joke. nostr:${payne_npub} was it funny?\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\",\"mention_1\":\"${payne_npub}\"}")
echo "$ordered" | grep -q '"ok":true' || fail "ordered mention event not stored"
got_ord=$(curl -sf "http://127.0.0.1:${port}/api/events")
printf '%s' "$got_ord" | grep -F "nostr:${npub} tell a joke. nostr:${payne_npub} was it funny?" \
    || fail "stored content lost mention order"

# M3.1 strict scoping: in an explicit delegation each robot receives only its
# own clause as the grok system prompt's "YOUR assignment", never the full ask.
i=0
while [ "$i" -lt 40 ]; do
    if grep -q 'S:.*YOUR assignment: was it funny' "$HUSH_CONFIG_DIR/grok-p.log" 2>/dev/null; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
grep -q 'S:.*YOUR assignment: tell a joke\.' "$HUSH_CONFIG_DIR/grok-p.log" \
    || fail "Happy must receive only its own clause"
grep -q 'S:.*YOUR assignment: was it funny' "$HUSH_CONFIG_DIR/grok-p.log" \
    || fail "Payne must receive only its own clause"
grep '^S:' "$HUSH_CONFIG_DIR/grok-p.log" | python3 -c '
import sys
bad = 0
for line in sys.stdin:
    if "tell a joke." in line and "was it funny" in line:
        bad = 1
sys.exit(bad)
' || fail "a robot received the full ask instead of its own clause"

echo "$html" | grep -q 'function assembleMentionContent' || fail "UI missing assembleMentionContent"
echo "$html" | grep -q 'function dropMentionFromInput' || fail "served UI missing dropMentionFromInput"
if echo "$html" | grep -q 'composerPills.pop()'; then
    fail "served UI Backspace must not pop composer pills"
fi
comp_pills=$(printf '%s' "$html" | awk '/function paintComposerPills/,/function applyMention/')
echo "$comp_pills" | grep -q 'dropMentionFromInput' || fail "served minus must strip @Name or submit still mentions"
if echo "$html" | grep -q 'splitFences(prettyMentions'; then
    fail "served UI must not run prettyMentions before in-sentence pills"
fi
echo "$html" | grep -q 'splitFences(e.content' || fail "served UI must split raw content for pills"
if echo "$html" | grep -q 'if (devLogEnabled) return events.slice()'; then
    fail "served UI must not un-hide logs when dest log is on"
fi
echo "$html" | grep -q '(now - created \* 1000) > 2000' || fail "served UI must skip ack gradient after 2s"
grep -q 'HUSH_AGENT_PEER_STANDARD' src/hush_agent.c || fail "missing inter-robot standard constant"
grep -q 'hush_agent_intro_seen' src/hush_agent.c || fail "missing intro table"
grep -q 'HUSH_AGENT_STRICT_SCOPE' src/hush_agent.c || fail "missing strict per-robot scope"
grep -q 'HUSH_AGENT_COOPERATE' src/hush_agent.c || fail "missing two-robot cooperation prompt"
grep -q 'hush_agent_leader_candidates' src/hush_agent.c || fail "missing leader candidate pool"
grep -q 'HUSH_AGENT_LEADER_PROMPT' src/hush_agent.c || fail "missing leader plan prompt"
grep -q 'hush_agent_parse_plan' src/hush_agent.c || fail "missing leader plan parser"
grep -q 'hush_agent_begin_elect' src/hush_agent.c || fail "missing leader election pass"
grep -q 'HUSH_AGENT_ELECT_PROMPT' src/hush_agent.c || fail "missing leader election prompt"
grep -q 'slot->group' src/hush_agent.c || fail "missing parallel wave groups"
grep -q 'system:hive-patterns' src/hush_agent.c || fail "missing leadership skill set"
handle=$(sed -n '/static void hush_agent_handle_mention/,/hush_agent_start_grok/p' src/hush_agent.c)
echo "$handle" | grep -q 'hush_agent_on_deck' || fail "one intro must precede grok start"
if echo "$handle" | grep -q 'dev_log_enabled'; then
    fail "intro must not be dest-log gated"
fi
if echo "$html" | grep -q 'ATLAS_N = 31'; then
    fail "served UI picker must not be exclusive 31-tile atlas"
fi
echo "$html" | grep -q '/icons/icon_panel_dogs.png' || fail "served UI missing dogs sheet"
sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$sess" | grep -q 'panel:dogs:4' || fail "session missing panel picture id"
for sheet in dogs cats sheep virus robots angevin; do
    code=$(curl -s -o "$home/sheet.png" -w '%{http_code}' \
        "http://127.0.0.1:${port}/icons/icon_panel_${sheet}.png")
    test "$code" = "200" || fail "served sheet ${sheet} HTTP ${code}"
    python3 -c 'import sys; d=open(sys.argv[1],"rb").read(8)
sys.exit(0 if d.startswith(b"\x89PNG\r\n\x1a\n") else 1)' "$home/sheet.png" \
        || fail "served sheet ${sheet} is not PNG"
done
about=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"action":"manage","slug":"general","about":"jokes | keep it short","kind":"open","robot_reply":"mention"}')
echo "$about" | grep -q 'jokes' || fail "channel about not saved"
keep=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"action":"manage","slug":"general","kind":"open","robot_reply":"mention"}')
echo "$keep" | grep -q 'jokes' || fail "manage without about must not wipe topics"

echo "agent mention reply ok"
