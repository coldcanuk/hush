#!/bin/sh
# Smoke-test first-launch session routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18766
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; rm -f "$log"; rm -rf "$cfg"; }
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
echo "$html" | grep -q 'id="provider-cfg"' || fail "HTML missing provider pencil"
echo "$html" | grep -q 'id="provider-drawer"' || fail "HTML missing provider drawer"
echo "$html" | grep -q 'id="provider-oauth"' || fail "HTML missing OAuth login button"
echo "$html" | grep -q 'Close the login browser and the terminal' || fail "HTML missing OAuth close-window copy"
echo "$html" | grep -q 'label.ready' || fail "HTML missing provider ready style"
echo "$html" | grep -q 'id="mention-box"' || fail "HTML missing mention box"
echo "$html" | grep -q 'composer-pill' || fail "HTML missing mention pills"
echo "$html" | grep -q 'nostr:' || fail "HTML missing NIP-27 mention insert"
echo "$html" | grep -q 'id="chan-menu"' || fail "HTML missing channel context menu"
echo "$html" | grep -q 'id="manage-chan"' || fail "HTML missing manage channel"
echo "$html" | grep -q 'manage-invite-add' || fail "HTML missing manage + invite"
echo "$html" | grep -q 'chan-del' || fail "HTML missing channel delete"
echo "$html" | grep -q 'chan-voice' || fail "HTML missing channel voice"
echo "$html" | grep -q 'robot-call' || fail "HTML missing robot call"
echo "$html" | grep -q 'tile-mute' || fail "HTML missing tile mute"
echo "$html" | grep -q 'id="tool-rail"' || fail "HTML missing tool rail"
echo "$html" | grep -q 'placeRailAtBrand' || fail "HTML missing rail brand home"
echo "$html" | grep -q 'dblclick' || fail "HTML missing rail double-click"
echo "$html" | grep -q 'id="thread-resize"' || fail "HTML missing thread resize"
echo "$html" | grep -q 'id="thread-pills"' || fail "HTML missing thread pills"
echo "$html" | grep -q 'id="thread-mention"' || fail "HTML missing thread mention"
if echo "$html" | grep -q 'id="rail-docks"'; then fail "rail docks must be gone"; fi
if echo "$html" | grep -q 'railAnchor'; then fail "rail docks/anchors must be gone"; fi
echo "$html" | grep -q 'reply_to' || fail "HTML missing reply_to indent"
echo "$html" | grep -q 'note reply' || fail "HTML missing reply class"
echo "$html" | grep -q 'id="thread-pane"' || fail "HTML missing thread pane"
echo "$html" | grep -q 'note.mine' || fail "HTML missing sided thread bubbles"
echo "$html" | grep -q '1:1 with' || fail "HTML missing 1:1 thread help"
echo "$html" | grep -q '1:n · you +' || fail "HTML missing 1:n thread help"
if echo "$html" | grep -q 'you · this robot. At ease.'; then
  fail "thread help must not be a Payne voice line"
fi
echo "$html" | grep -q 'thread-btn' || fail "HTML missing thread button"
echo "$html" | grep -q 'id="relay-drawer"' || fail "HTML missing relay drawer"
echo "$html" | grep -q 'id="relay-close"' || fail "HTML missing relay close"
echo "$html" | grep -q 'think-dot' || fail "HTML missing thinking chip"
echo "$html" | grep -q 'id="thread-think"' || fail "HTML missing thread thinking strip"
echo "$html" | grep -q 'paintThreadThink' || fail "HTML missing thread think painter"
echo "$html" | grep -q 'send.disabled' || fail "HTML must disable send while thinking"
if echo "$html" | grep -q 'bots.filter((b) => b.kind !== "human")'; then
  fail "thread follow-up must not remention every member robot"
fi
echo "$html" | grep -q 'extra.filter((p) => p.kind !== "human")' \
  || fail "thread follow-up must mention only new pills"
echo "$html" | grep -q 'id="install-help"' || fail "HTML missing install help"
echo "$html" | grep -q 'id="rail-toggle"' || fail "HTML missing rail hamburger"
echo "$html" | grep -q 'contextmenu' || fail "HTML missing channel contextmenu"
echo "$html" | grep -q 'id="provider-key-add"' || fail "HTML missing provider + pills"
echo "$html" | grep -q 'id="provider-username"' || fail "HTML missing provider username"
echo "$html" | grep -q 'id="provider-password"' || fail "HTML missing provider password"
echo "$html" | grep -q 'id="provider-token"' || fail "HTML missing provider token"
echo "$html" | grep -q 'id="provider-passkey"' || fail "HTML missing provider passkey"
echo "$html" | grep -q '/api/provider' || fail "HTML missing provider route"
echo "$html" | grep -q 'pass show hush/providers/' || fail "HTML missing provider retrieve CLI"
echo "$html" | grep -q 'ClinePass' || fail "HTML missing ClinePass copy"
echo "$html" | grep -q 'bring-your-own' || fail "HTML missing Cline BYOK copy"
echo "$html" | grep -q 'Delete this robot' || fail "HTML missing delete robot"
echo "$html" | grep -q 'CONTEXT_MAX = 3' || fail "HTML missing 3-file cap"
echo "$html" | grep -q 'id="hive-close"' || fail "HTML missing Close button"
echo "$html" | grep -q 'id="hive-exit"' || fail "HTML missing Exit button"
echo "$html" | grep -q 'id="hive-leave"' || fail "HTML missing leave chooser"
echo "$html" | grep -q 'id="leave-exit"' || fail "HTML missing leave Exit"
echo "$html" | grep -q 'id="leave-close"' || fail "HTML missing leave Close"
echo "$html" | grep -q 'id="leave-cancel"' || fail "HTML missing leave Cancel"
echo "$html" | grep -q 'openLeave' || fail "HTML missing openLeave"
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
echo "$chan" | grep -q '"id":"' || fail "channel uuid missing"
grp=$(curl -sf -X POST "http://127.0.0.1:${port}/api/group" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Duty"}')
echo "$grp" | grep -q '"name":"Duty"' || fail "group create"
gid=$(printf '%s' "$grp" | sed -n 's/.*"groups":\[{"name":"Duty","id":"\([0-9a-f]\{32\}\)".*/\1/p')
test -n "$gid" || fail "group id missing"
grouped=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d "{\"action\":\"group\",\"slug\":\"incidents\",\"group_id\":\"$gid\"}")
echo "$grouped" | grep -q "\"group_id\":\"$gid\"" || fail "channel group"
managed=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"action":"manage","slug":"incidents","human_0":"npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg","robot_0":"sgt-major-payne","kind":"humans","robot_reply":"off","burst_ms":5000,"max_jobs":1,"cooldown_s":30}')
echo "$managed" | grep -q 'sgt-major-payne' || fail "channel manage robots"
echo "$managed" | grep -q '"kind":"humans"' || fail "channel manage kind"
echo "$managed" | grep -q '"robot_reply":"off"' || fail "channel manage reply"
ungrouped=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"action":"ungroup","slug":"incidents"}')
echo "$ungrouped" | grep -q '"group_id":""' || fail "channel ungroup"
deleted=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"action":"delete","slug":"incidents"}')
echo "$deleted" | grep -q '"slug":"incidents"' && fail "deleted channel still listed"
chan=$(curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
    -H 'Content-Type: application/json' \
    -d '{"name":"incidents"}')
echo "$chan" | grep -q '"slug":"incidents"' || fail "channel recreate"
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
test -f "$cfg/vibe.json" || fail "vibe.json should survive create"
grep -q nsec "$cfg/vibe.json" && fail "vibe.json must not store nsec"
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
restored=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$restored" | grep -q '"has_vibe":true' || fail "restart should restore vibe"
echo "$restored" | grep -q '"name":"HQ"' || fail "restart should keep vibe name"
echo "$restored" | grep -q '"slug":"incidents"' || fail "restart should keep channel"
echo "$restored" | grep -q '"first_name":"Ada"' || fail "restart should keep profile"
echo "launch routes ok"
