#!/bin/sh
# codex (Codex) robot: must spawn `codex exec`, not grok, and the combined
# prompt (system prompt + rules + note) and complete C skill reach the child.
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

fail() { echo "codex check failed: $1" >&2; exit 1; }

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
mkdir -p "$home/bin" "$home/.codex" "$home/.grok" "$home/.config/hush" "$home/.hush"
cat > "$home/bin/codex" <<'CODEX'
#!/bin/sh
set -eu
[ "$1" = exec ] || exit 61
[ "$2" = --cd ] || exit 62
[ "$4" = --skip-git-repo-check ] || exit 63
[ "$5" = --sandbox ] && [ "$6" = read-only ] || exit 64
[ "$#" = 7 ] || exit 65
skill="$3/.agents/skills/write-legible-c"
[ -r "$skill/SKILL.md" ] && [ -r "$skill/references/c-standard.md" ] || exit 66
[ -r "$skill/LICENSE" ] || exit 67
printf '%s\n' "$7" > "$HUSH_CONFIG_DIR/codex-exec.log"
printf '%s\n' CODEX_REPLY_MARKER
CODEX
chmod 0755 "$home/bin/codex"
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

# The retired id cannot be selected or configured through the public API.
providers=$(curl -sf "http://127.0.0.1:${port}/api/provider")
printf '%s' "$providers" | grep -q '"codex"' || fail "Codex missing"
if printf '%s' "$providers" | grep -q '"agy"'; then fail "retired provider advertised"; fi
bad=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Retired","system_prompt":"Watch.","provider":"agy","save_pass":false}')
[ "$bad" = 400 ] || fail "retired provider accepted"

ag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Sage","system_prompt":"Answer briefly.","provider":"codex","save_pass":false}')
echo "$ag" | grep -q '"slug":"sage"' || fail "sage not raised"
npub=$(printf '%s' "$ag" | sed -n 's/.*"slug":"sage"[^}]*"npub":"\([^"]*\)".*/\1/p')
test -n "$npub" || fail "sage npub missing"

# Without Codex's own auth, another provider's auth cannot enable this robot.
curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} Before login\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}" >/dev/null
sleep 0.2
test ! -f "$home/.config/hush/codex-exec.log" || fail "Codex ran without login"
printf '%s\n' '{"ok":true}' > "$home/.codex/auth.json"

# Restore legacy primary and ranked choices, including a duplicate Codex slot.
kill "$pid"
wait "$pid" || true
pid=""
vibe="$HUSH_CONFIG_DIR/vibe.json"
test -f "$vibe" || fail "persisted vibe missing"
python3 - "$vibe" <<'MIGRATE'
import json, sys
from pathlib import Path
p=Path(sys.argv[1]); doc=json.loads(p.read_text())
for key, value in list(doc.items()):
    if key.startswith('agent_slug_') and value == 'sage':
        index=key.removeprefix('agent_slug_')
        doc['agent_provider_'+index]='agy'
        doc['agent_providers_'+index]='agy,codex,grok-build'
doc['npayne_providers']='3'
doc['payne_provider_0']='agy'
doc['payne_provider_1']='codex'
doc['payne_provider_2']='grok-build'
p.write_text(json.dumps(doc, separators=(',', ':')))
MIGRATE
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not restart"
session=$(curl -sf "http://127.0.0.1:${port}/api/session")
printf '%s' "$session" | python3 -c '
import json, sys
s=json.load(sys.stdin)
robot=next(a for a in s["agents"] if a["slug"] == "sage")
for actor in (robot, s["payne"]):
    assert actor["provider"] == "codex", actor
    assert actor["providers"] == ["codex", "grok-build"], actor
' || fail "legacy roster did not migrate"
npub=$(printf '%s' "$session" | python3 -c 'import json,sys; print(next(a["npub"] for a in json.load(sys.stdin)["agents"] if a["slug"] == "sage"))')
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"create"}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"ack_backup","save_pass":false}' >/dev/null

curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} Hello from a human\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}" \
    >/dev/null

got=""
i=0
while [ "$i" -lt 40 ]; do
    got=$(curl -sf "http://127.0.0.1:${port}/api/events")
    printf '%s' "$got" | grep -q 'CODEX_REPLY_MARKER' && break
    i=$((i + 1))
    sleep 0.05
done
printf '%s' "$got" | grep -q 'CODEX_REPLY_MARKER' || fail "codex reply missing"

# The Codex binary must have received the combined
# system prompt + human note and loaded the repository skill.
test -f "$home/.config/hush/codex-exec.log" || fail "codex exec log missing"
grep -q 'Answer briefly' "$home/.config/hush/codex-exec.log" \
    || fail "codex prompt missing robot system prompt"
grep -q 'Hello from a human' "$home/.config/hush/codex-exec.log" \
    || fail "codex prompt missing human note"

echo "codex routes ok"
