#!/bin/sh
# Smoke-test first-launch session routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18766
log=$(mktemp)
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; rm -f "$log"; }
trap cleanup EXIT
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
fail() { echo "launch check failed: $1" >&2; exit 1; }
sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$sess" | grep -q '"logged_in":false' || fail "cold session should be logged out"
echo "$sess" | grep -q '"ready":false' || fail "cold session should not be ready"
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'id="gate"' || fail "HTML missing first-launch gate"
# Drawers after </script> make boot listeners throw; splash stays blank.
prof_at=$(printf '%s' "$html" | awk 'index($0, "id=\"profile\""){print NR; exit}')
script_at=$(printf '%s' "$html" | awk 'index($0, "<script>"){print NR; exit}')
test -n "$prof_at" && test -n "$script_at" && test "$prof_at" -lt "$script_at" \
    || fail "profile drawer must precede boot script"
echo "$html" | grep -q 'class=\\\"feather\\\"' || fail "HTML missing feather splash"
echo "$html" | grep -q '/icon-192.png' || fail "HTML missing feather src"
echo "$html" | grep -q 'stepBar' || fail "HTML missing wizard progress"
echo "$html" | grep -q 'Carry on.' || fail "HTML missing Meet Payne CTA"
echo "$html" | grep -q 'Create a new identity key' || fail "HTML missing create CTA"
echo "$html" | grep -q 'prof-first' || fail "HTML missing profile first name"
echo "$html" | grep -q 'data-theme=\"dracula\"' || fail "HTML missing dracula theme"
echo "$html" | grep -q 'action: \"logout\"' || fail "HTML missing server logout"
echo "$html" | grep -q 'Raise a robot' || fail "HTML missing raise-agent"
echo "$html" | grep -q 'Invite human' || fail "HTML missing invite-human"
echo "$html" | grep -q 'id="robot-list"' || fail "HTML missing robot list"
echo "$html" | grep -q 'paintRobots' || fail "HTML missing robot cards"
echo "$html" | grep -q 'System Prompt' || fail "HTML missing system prompt"
echo "$html" | grep -q 'agent-provider' || fail "HTML missing AI provider"
echo "$html" | grep -q 'Delete this robot' || fail "HTML missing delete robot"
echo "$html" | grep -q 'CONTEXT_MAX = 3' || fail "HTML missing 3-file cap"
echo "$html" | grep -q 'id="hive-close"' || fail "HTML missing Close button"
echo "$html" | grep -q 'id="hive-exit"' || fail "HTML missing Exit button"
echo "$html" | grep -q '/api/close' || fail "HTML missing close route"
echo "$html" | grep -q '/api/exit' || fail "HTML missing exit route"
echo "$html" | grep -q 'isContextFile' || fail "HTML missing MIME check"
echo "$html" | grep -q 'Checked to save password to Unix Password Manager' || fail "pass checkbox copy"
echo "$html" | grep -q 'pass show hush/identity/nsec' || fail "retrieve CLI"
echo "$html" | grep -q 'id=\\\"save-pass\\\"' || fail "pass checkbox id"
echo "$html" | grep -q 'savePass = true' || fail "checkbox defaults on"
echo "$html" | grep -q 'lastGateHtml' || fail "gate paint must skip unchanged trees"
echo "$html" | grep -q 'data-lpignore' || fail "nsec input must ignore password-manager autofill"
echo "$html" | grep -q 'dialog class=\\\"secret\\\"' || fail "secret modal"
created=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"create"}')
echo "$created" | grep -q '"logged_in":true' || fail "create did not log in"
echo "$created" | grep -q '"nsec":"nsec1' || fail "create should return nsec once"
echo "$created" | grep -q '"npub":"npub1' || fail "create should return npub"
acked=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"ack_backup","save_pass":false}')
echo "$acked" | grep -q '"nsec":""' || fail "ack should drop nsec from session"
echo "$acked" | grep -q '"save_pass":false' || fail "opt-out should skip pass"
echo "$acked" | grep -q '"pass_saved":false' || fail "opt-out must not claim save"
vibe=$(curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' \
    -d '{"name":"HQ","about":"primary endpoint"}')
echo "$vibe" | grep -q '"ready":true' || fail "vibe should ready the hive"
echo "$vibe" | grep -q '"visibility":"public"' || fail "vibe default public"
echo "$vibe" | grep -q 'Sgt Major Payne' || fail "Payne missing"
echo "$vibe" | grep -q '"slug":"welcome"' || fail "welcome channel missing"
echo "$vibe" | grep -q '"theme":"dark"' || fail "default theme missing"
prof=$(curl -sf -X POST "http://127.0.0.1:${port}/api/profile" \
    -H 'Content-Type: application/json' \
    -d '{"first_name":"Ada","last_name":"Lovelace","email":"ada@hive.local","organization":"HQ","theme":"dracula"}')
echo "$prof" | grep -q '"first_name":"Ada"' || fail "profile first name"
echo "$prof" | grep -q '"theme":"dracula"' || fail "profile theme"
chan=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"name":"incidents"}')
echo "$chan" | grep -q '"slug":"incidents"' || fail "channel create"
proj=$(curl -sf -X POST "http://127.0.0.1:${port}/api/project" \
    -H 'Content-Type: application/json' \
    -d '{"name":"alpha","path":"/tmp/hush-check-alpha","git":"true"}')
echo "$proj" | grep -q '"slug":"alpha"' || fail "project create"
test -d /tmp/hush-check-alpha/.git || fail "git init"
mem=$(curl -sf -X POST "http://127.0.0.1:${port}/api/member" \
    -H 'Content-Type: application/json' \
    -d '{"npub":"npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg","name":"Alice"}')
echo "$mem" | grep -q 'Alice' || fail "member add"
ag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Sentry","system_prompt":"Watch.","provider":"goose","save_pass":false}')
echo "$ag" | grep -q '"slug":"sentry"' || fail "agent create"
echo "$ag" | grep -q '"provider":"goose"' || fail "agent provider"
noprov=$(curl -s -o /tmp/hush-noprov-agent -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Ghost","system_prompt":"Watch.","save_pass":false}')
test "$noprov" != "200" || fail "provider required"
bad=$(curl -s -o /tmp/hush-bad-agent -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Badfile","system_prompt":"Watch.","provider":"goose","context_name":"x.pdf","context_mime":"application/pdf","context_text":"%PDF"}')
test "$bad" != "200" || fail "pdf context must be rejected"
gone=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"delete","slug":"sentry"}')
echo "$gone" | grep -q '"slug":"sentry"' && fail "deleted agent still listed"
logged=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"logout"}')
echo "$logged" | grep -q '"logged_in":false' || fail "logout should clear login"
echo "$logged" | grep -q '"ready":false' || fail "logout should not stay ready"
echo "$logged" | grep -q '"has_vibe":true' || fail "logout should keep vibe"
echo "launch routes ok"
