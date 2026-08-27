#!/bin/sh
# Smoke-test first-launch session routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18766
log=$(mktemp)
cfg=$(mktemp -d)
hush_home=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
export HUSH_HOME="$hush_home"
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; rm -f "$log"; rm -rf "$cfg" "$hush_home" /tmp/hush-check-alpha /tmp/hush-bad-canvas; }
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
test -d "$hush_home/config" || fail "ensure-home missing ~/.hush/config"
test -d "$hush_home/agents" || fail "ensure-home missing ~/.hush/agents"
test -f "$hush_home/skills/system/forge-skill/SKILL.md" || fail "forge-skill not seeded"
skills=$(curl -sf "http://127.0.0.1:${port}/api/skills")
echo "$skills" | grep -q '"scopes":\["system","user","robot"\]' || fail "skills missing three scopes"
echo "$skills" | grep -q 'system:forge-skill' || fail "skills missing forge-skill"
echo "$skills" | grep -q '"scope":"system"' || fail "skills missing system scope"
echo "$skills" | grep -q '"watermarks"' || fail "skills missing watermarks"
echo "$skills" | grep -q '"chars_high":8000' || fail "skills missing char watermark"
echo "$skills" | grep -q '"role":"chaperon"' || fail "skills missing chaperon role"
echo "$skills" | grep -q 'system:canvas-coach' || fail "skills missing coach pack"
echo "$skills" | grep -q 'system:hive-audit' || fail "skills missing audit pack"
echo "$skills" | grep -q 'system:mobile-trace' || fail "skills missing mobile-trace"
echo "$skills" | grep -q 'system:hive-teardown' || fail "skills missing hive-teardown"
echo "$skills" | grep -q 'system:hive-look' || fail "skills missing hive-look"
echo "$skills" | grep -q 'system:hive-apps' || fail "skills missing hive-apps"
echo "$skills" | grep -q 'system:token-extract' || fail "skills missing token-extract"
echo "$skills" | grep -q 'reverse-engineering' || fail "skills missing reverse-engineering"
echo "$skills" | grep -q 'system:ai-engineering-coach' && fail "old coach slug must be absent"
echo "$skills" | grep -q 'system:security-audit' && fail "old audit slug must be absent"
echo "$skills" | grep -q 'system:skillui-extract' && fail "old skillui slug must be absent"
echo "$skills" | grep -q 'system:on-topic' && fail "folded on-topic must be absent"
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
devlog_at=$(printf '%s' "$html" | awk 'index($0, "id=\"dev-log-close\""){print NR; exit}')
test -n "$devlog_at" && test "$devlog_at" -lt "$script_at" \
    || fail "dev-log-close must precede boot script"
if echo "$html" | grep -q 'left: 360px !important'; then
    fail "rail must not inject 360px !important"
fi
if echo "$html" | grep -q 'GOOD = 360'; then
    fail "rail must not use canonical 360 lock"
fi
if echo "$html" | grep -F 'r.style.left = "360px"'; then
    fail "rail nanny must be gone"
fi
echo "$html" | grep -q 'saved.x' || fail "loadRail must restore saved.x"
echo "$html" | grep -q 'saved.y' || fail "loadRail must restore saved.y"
echo "$html" | grep -q 'saved.collapsed' || fail "loadRail must restore saved.collapsed"
echo "$html" | grep -q 'function saveRail' || fail "HTML missing saveRail"
save_body=$(printf '%s' "$html" | awk '/function saveRail/,/function applyThreadSize/')
echo "$save_body" | grep -q 'homed:' && fail "saveRail must persist {x,y,collapsed} only"
echo "$html" | awk '/rail-toggle"\)\.addEventListener\("dblclick"/,/rail-grip"\)\.addEventListener\("pointerdown"/' \
    | grep -q 'placeRailAtBrand' \
    || fail "rail-toggle dblclick must home at brand"
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
echo "$html" | grep -q 'INV_COLS = 4' || fail "compact inventory must be 4 cols"
echo "$html" | grep -q 'INV_ROWS = 3' || fail "compact inventory must be 3 rows"
if echo "$html" | grep -q 'INV_COLS = 8'; then
    fail "compact default must not be 8 cols"
fi
echo "$html" | grep -q 'class="inv-btn"' || fail "Seed/Clear/Raise must share inv-btn"
echo "$html" | grep -q 'syncInventoryFromRoster' || fail "inventory must bind live roster"
if echo "$html" | grep -q 'locked: true}'; then
    fail "robotModels extra brace would throw SyntaxError"
fi
js="$cfg/ui-script.js"
printf '%s' "$html" | awk 'BEGIN{p=0} /<script>/{p=1; next} /<\/script>/{p=0} p' > "$js"
node --check "$js" || fail "served inventory script failed node --check"
echo "$html" | grep -q 'id="seed-drawer"' || fail "HTML missing seed wizard"
echo "$html" | grep -q 'id="seed-actions"' || fail "HTML missing seed actions"
echo "$html" | grep -q 'id="seed-skills"' || fail "HTML missing seed skills"
echo "$html" | grep -q 'id="seed-project"' || fail "HTML missing seed project"
echo "$html" | grep -q 'id="seed-channel-new"' || fail "HTML missing seed new-channel choice"
echo "$html" | grep -q 'id="seed-channel-existing"' || fail "HTML missing seed existing-channel choice"
echo "$html" | grep -q 'function buildSeedPrompt' || fail "HTML missing buildSeedPrompt"
echo "$html" | grep -q 'function payneCanReply' || fail "HTML missing Payne seed gate"
echo "$html" | grep -q 'function seedTeam' || fail "HTML missing seedTeam"
echo "$html" | grep -q 'id="inv-expand"' || fail "HTML missing inventory expand"
echo "$html" | grep -q 'INV_EXPAND_COLS = 8' || fail "expanded inventory must be 8 cols"
if echo "$html" | grep -q 'seedInventoryDemo'; then
    fail "Seed must not be fake seedInventoryDemo tiles"
fi
seed_at=$(printf '%s' "$html" | awk 'index($0, "id=\"seed-drawer\""){print NR; exit}')
test -n "$seed_at" && test "$seed_at" -lt "$script_at" \
    || fail "seed drawer must precede boot script"
echo "$html" | grep -q 'function assembleMentionContent' || fail "HTML missing in-place mention assembler"
if echo "$html" | grep -q 'composerPills.map((p) => "nostr:"'; then
    fail "composer must not prepend all pills before leftover text"
fi
echo "$html" | grep -q 'function dropMentionFromInput' || fail "HTML missing dropMentionFromInput"
comp_pills=$(printf '%s' "$html" | awk '/function paintComposerPills/,/function applyMention/')
echo "$comp_pills" | grep -q 'dropMentionFromInput' || fail "composer minus must strip @Name from textarea"
thr_pills=$(printf '%s' "$html" | awk '/function paintThreadPills/,/function paintThreadMentionBox/')
echo "$thr_pills" | grep -q 'dropMentionFromInput' || fail "thread minus must strip @Name from textarea"
if echo "$html" | grep -q 'composerPills.pop()'; then
    fail "composer Backspace-at-0 must not pop pills"
fi
if echo "$html" | grep -q 'threadPills.pop()'; then
    fail "thread Backspace-at-0 must not pop pills"
fi
grep -q 'hush_json_has_key' src/hush_http.c || fail "manage about must use hush_json_has_key"
manage=$(sed -n '/static hush_status_t hush_http_channel_manage/,/hush_http_find_channel/p' src/hush_http.c)
echo "$manage" | grep -q 'hush_json_has_key(body, "about")' \
    || fail "manage must no-op about when the field is absent"
if echo "$html" | grep -q 'splitFences(prettyMentions'; then
    fail "paintNote must not run prettyMentions before in-sentence pills"
fi
echo "$html" | grep -q 'splitFences(e.content' || fail "paintNote must split raw content for pills"
if echo "$html" | grep -q 'if (devLogEnabled) return events.slice()'; then
    fail "visibleNotes must not un-hide logs when dest log is on"
fi
echo "$html" | grep -q '(now - created \* 1000) > 2000' || fail "ack must skip gradient on notes older than 2s"
echo "$html" | grep -q 'function ackStampFor' || fail "HTML missing ack stamp so tick can advance phases"
isdev=$(printf '%s' "$html" | awk '/function isDevLogNote/,/function appendDevLog/')
echo "$isdev" | grep -q 'Mention received' || fail "isDevLogNote must filter Mention received"
if echo "$isdev" | grep -q 'At ease'; then
    fail "isDevLogNote must not hide At ease intros"
fi
echo "$html" | grep -q 'is reacting' || fail "HTML missing reacting ack phase"
echo "$html" | grep -q 'mentionAckPhase' || fail "HTML missing progressive ack"
echo "$html" | grep -q 'id="manage-topic-pills"' || fail "HTML missing channel topic pills"
echo "$html" | grep -q 'id="manage-prompt"' || fail "HTML missing channel prompt"
echo "$html" | grep -q 'id="agent-pic-picker"' || fail "HTML missing robot picture picker"
echo "$html" | grep -q 'id="agent-pic-sheets"' || fail "HTML missing picture sheet tabs"
echo "$html" | grep -q 'ICON_PANELS' || fail "HTML missing ICON_PANELS"
echo "$html" | grep -q 'ICON_CELLS = 64' || fail "HTML missing 64-cell sheet math"
if echo "$html" | grep -q 'ATLAS_N = 31'; then
    fail "picker must not be exclusive 31-tile atlas"
fi
echo "$html" | grep -q '/icons/icon_panel_dogs.png' || fail "HTML missing dogs sheet"
echo "$html" | grep -q '/icons/icon_panel_cats.png' || fail "HTML missing cats sheet"
echo "$html" | grep -q '/icons/icon_panel_sheep.png' || fail "HTML missing sheep sheet"
echo "$html" | grep -q '/icons/icon_panel_virus.png' || fail "HTML missing virus sheet"
echo "$html" | grep -q '/icons/icon_panel_robots.png' || fail "HTML missing robots sheet"
echo "$html" | grep -q '/icons/icon_panel_angevin.png' || fail "HTML missing angevin sheet"
echo "$html" | grep -q 'panel:' || fail "HTML missing panel: picture ids"
for sheet in dogs cats sheep virus robots angevin; do
    code=$(curl -s -o "$cfg/sheet.png" -w '%{http_code}' \
        "http://127.0.0.1:${port}/icons/icon_panel_${sheet}.png")
    test "$code" = "200" || fail "sheet ${sheet} HTTP ${code}"
    python3 -c 'import sys; d=open(sys.argv[1],"rb").read(8)
sys.exit(0 if d.startswith(b"\x89PNG\r\n\x1a\n") else 1)' "$cfg/sheet.png" \
        || fail "sheet ${sheet} is not PNG"
done
echo "$html" | grep -q 'function manageAboutValue' || fail "HTML missing channel about writer"
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
echo "$html" | grep -q 'id="manage-policy"' || fail "HTML missing manage policy"
echo "$html" | grep -q 'id="manage-policy-more"' || fail "HTML missing policy advanced"
echo "$html" | grep -q 'name="manage-reply"' || fail "HTML missing robot_reply radios"
echo "$html" | grep -q 'name="manage-burst"' || fail "HTML missing burst_ms radios"
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
echo "$html" | grep -q 'id="code-canvas"' || fail "HTML missing code canvas"
echo "$html" | grep -q 'code-block' || fail "HTML missing fenced code block"
echo "$html" | grep -q '/api/canvas' || fail "HTML missing canvas save route"
echo "$html" | grep -q 'splitFences' || fail "HTML missing fence splitter"
echo "$html" | grep -q 'canvas-file' || fail "HTML missing canvas file selector"
echo "$html" | grep -q 'paintThreadThink' || fail "HTML missing thread think painter"
echo "$html" | grep -q 'send.disabled' || fail "HTML must disable send while thinking"
if echo "$html" | grep -q 'bots.filter((b) => b.kind !== "human")'; then
  fail "thread follow-up must not remention every member robot"
fi
echo "$html" | grep -q 'extra.filter((p) => p.kind !== "human")' \
  || fail "thread follow-up must mention only new pills"
echo "$html" | grep -q 'sole.length === 1' \
  || fail "1:1 follow-up must inherit the sole member robot"
echo "$html" | grep -q 'localThink' \
  || fail "HTML missing optimistic thread think"
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
echo "$html" | grep -q 'Delete Robot' || fail "HTML missing delete robot"
echo "$html" | grep -q 'Raise Robot' || fail "HTML missing raise robot"
echo "$html" | grep -q 'Save Robot' || fail "HTML missing save robot"
echo "$html" | grep -q 'value="deepseek-api"' || fail "HTML missing deepseek radio"
echo "$html" | grep -q 'Edit Major' || fail "HTML missing Payne edit title"
echo "$html" | grep -q 'id="inv-menu"' || fail "HTML missing inventory edit menu"
echo "$html" | grep -q 'data-act="edit"' || fail "HTML missing edit menu action"
echo "$html" | grep -q 'openInvMenu' || fail "HTML missing openInvMenu"
echo "$html" | grep -q 'hideInvMenu' || fail "HTML missing hideInvMenu"
echo "$html" | grep -q 'contextmenu' || fail "HTML missing contextmenu trap"
echo "$html" | grep -q 'metaKey' || fail "HTML missing macOS meta+click"
echo "$html" | grep -q 'id="agent-voice"' || fail "HTML missing voice picker"
echo "$html" | grep -q 'agent-voice-wrap' || fail "HTML missing whisper-gated voice wrap"
echo "$html" | grep -q 'whisperReady' || fail "HTML missing whisperReady gate"
echo "$html" | grep -q 'id="skill-armory"' || fail "HTML missing skill armory"
echo "$html" | grep -q 'id="skill-loadout"' || fail "HTML missing skill loadout"
echo "$html" | grep -q 'id="skill-cycle"' || fail "HTML missing skill cycle"
echo "$html" | grep -q 'id="skill-cycle-prev"' || fail "HTML missing skill cycle prev"
echo "$html" | grep -q 'function attachLoadout' || fail "HTML missing attachLoadout"
echo "$html" | grep -q 'body.skill_0 = ""' || fail "empty loadout must post skill_0"
echo "$html" | grep -q 'body.nskills = equippedSkills.length' || fail "save must post nskills"
echo "$html" | grep -q 'id="agent-clone"' || fail "HTML missing clone control"
echo "$html" | grep -q 'skillCycleIdx' || fail "HTML missing skillCycleIdx"
echo "$html" | grep -q 'skillWatermarks' || fail "HTML missing skillWatermarks"
echo "$html" | grep -q 'action: \"clone\"' || fail "HTML missing clone action"
echo "$html" | grep -q 'id="skill-forge-open"' || fail "HTML missing forge control"
echo "$html" | grep -q 'id="forge-drawer"' || fail "HTML missing forge drawer"
echo "$html" | grep -q 'w: 1, h: 1' || fail "inventory tiles must be equal 1x1"
if echo "$html" | grep -q 'PAYNE_SLUG ? "1x3"'; then
    fail "Payne must not be a 1x3 inventory exception"
fi
echo "$html" | grep -q 'el.title = it.name' || fail "hover title must use robot name"
echo "$html" | grep -q 'id="agent-enabled"' || fail "HTML missing enable slider"
echo "$html" | grep -q 'inv-item.disabled' || fail "HTML missing disabled greyscale"
echo "$html" | grep -q 'readOnly = true' || fail "HTML must lock Major name/prompt"
echo "$html" | grep -q 'platform robot' || fail "HTML missing Major platform copy"
echo "$html" | grep -q 'id="payne-provider-pills"' || fail "HTML missing Payne provider pills"
echo "$html" | grep -q 'id="agent-identity"' || fail "HTML missing lockable identity block"
echo "$html" | grep -q 'PAYNE_PROVIDERS_MAX = 4' || fail "HTML missing Payne provider cap"
echo "$html" | grep -q 'First on deck speaks first' || fail "HTML missing Payne order copy"
echo "$html" | grep -q 'Delete this robot?' || fail "HTML missing delete confirm"
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
echo "$vibe" | grep -q '"name":"Major"' || fail "Payne missing"
echo "$vibe" | grep -q '"slug":"coach"' || fail "coach template missing"
echo "$vibe" | grep -q '"slug":"auditor"' || fail "auditor template missing"
echo "$vibe" | grep -q '"locked":true' || fail "locked template missing"
echo "$vibe" | grep -q 'sgt-major-payne-copy' && fail "seed must not clone Major"
echo "$vibe" | grep -q '"name":"Sgt Major Payne"' && fail "old Payne display name must not ship"
echo "$vibe" | grep -q 'Sgt. Maj. Payne' && fail "old Payne display name must not ship"
echo "$vibe" | grep -F '"providers":["goose"]' || fail "Payne default providers"
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
can=$(curl -sf -X POST "http://127.0.0.1:${port}/api/canvas" \
    -H 'Content-Type: application/json' \
    -d '{"project":"alpha","path":"snippet-1.py","content":"print(1)"}')
echo "$can" | grep -q '"ok":true' || fail "canvas save"
test -f /tmp/hush-check-alpha/snippet-1.py || fail "canvas file missing"
grep -q 'print(1)' /tmp/hush-check-alpha/snippet-1.py || fail "canvas content"
badcan=$(curl -s -o /tmp/hush-bad-canvas -w '%{http_code}' -X POST \
    "http://127.0.0.1:${port}/api/canvas" \
    -H 'Content-Type: application/json' \
    -d '{"project":"alpha","path":"../escape.py","content":"no"}')
test "$badcan" != "200" || fail "canvas must refuse .."
mem=$(curl -sf -X POST "http://127.0.0.1:${port}/api/member" \
    -H 'Content-Type: application/json' \
    -d '{"npub":"npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg","name":"Alice"}')
echo "$mem" | grep -q 'Alice' || fail "member add"
ag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Sentry","system_prompt":"Watch.","provider":"goose","save_pass":false}')
echo "$ag" | grep -q '"slug":"sentry"' || fail "agent create"
echo "$ag" | grep -q '"provider":"goose"' || fail "agent provider"
cloned=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"clone","slug":"coach"}')
echo "$cloned" | grep -q '"slug":"coach-copy"' || fail "clone coach"
echo "$cloned" | grep -q '"name":"Coach copy"' || fail "clone display name"
echo "$cloned" | grep -q '"name":"Coach copy"[^}]*"skills":\["system:canvas-coach"\]' \
    || fail "clone copy wears coach skill"
prunedcopy=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"coach-copy","skill_0":"","nskills":0}')
echo "$prunedcopy" | grep -q '"name":"Coach copy"[^}]*"skills":\[\]' \
    || fail "clone unequip 1to0 must empty loadout"
echo "$prunedcopy" | grep -q '"name":"Coach copy"[^}]*system:canvas-coach' \
    && fail "clone unequip must drop skill"
echo "$prunedcopy" | grep -q '"name":"Coach","slug":"coach"[^}]*"skills":\["system:canvas-coach"\]' \
    || fail "locked coach must keep skill after copy prune"
noclone=$(curl -s -o /tmp/hush-no-major-clone -w '%{http_code}' \
    -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"clone","slug":"sgt-major-payne"}')
test "$noclone" != "200" || fail "Major must not clone"
ui=$(curl -sf -X POST "http://127.0.0.1:${port}/api/skillui" \
    -H 'Content-Type: application/json' \
    -d '{"html":"body{color:#112233;font-family:sans-serif;padding:8px}","name":"fixture"}')
echo "$ui" | grep -q '#112233' || fail "skillui color"
echo "$ui" | grep -q 'sans-serif' || fail "skillui font"
badrole=$(curl -s -o /tmp/hush-bad-role -w '%{http_code}' \
    -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"sentry","skill_0":"system:civility"}')
test "$badrole" != "200" || fail "chaperon skill must not equip on worker"
upd=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"sentry","name":"Sentry","system_prompt":"Watch.","picture":"panel:dogs:4","skill_0":"system:forge-skill"}')
echo "$upd" | grep -q 'panel:dogs:4' || fail "agent picture persist"
echo "$upd" | grep -q 'system:forge-skill' || fail "agent skill persist"
offag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"sentry","enabled":false}')
echo "$offag" | grep -q '"enabled":false' || fail "raised robot must disable"
echo "$offag" | grep -q '"name":"Sentry"[^}]*system:forge-skill' \
    || fail "disable must not drop loadout"
pruned=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"sentry","skill_0":""}')
echo "$pruned" | grep -q '"name":"Sentry"[^}]*"skills":\[\]' \
    || fail "unequip last skill must persist empty loadout"
echo "$pruned" | grep -q '"name":"Sentry"[^}]*system:forge-skill' \
    && fail "unequip must drop sentry skill"
forged=$(curl -sf -X POST "http://127.0.0.1:${port}/api/skill" \
    -H 'Content-Type: application/json' \
    -d '{"name":"joke-book","summary":"Jokes.","body":"Tell one joke.","scope":"user"}')
echo "$forged" | grep -q 'user:joke-book' || fail "forge user skill"
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
payne=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"slug":"sgt-major-payne","provider_0":"grok-build","provider_1":"goose"}')
echo "$payne" | grep -F '"providers":["grok-build","goose"]' || fail "Payne provider order"
echo "$payne" | grep -q '"name":"Major"' || fail "Payne default name stays Major"
echo "$payne" | grep -q '"enabled":true' || fail "Payne starts enabled"
renamed=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"slug":"sgt-major-payne","provider_0":"goose","name":"Major Two","system_prompt":"Nope."}')
echo "$renamed" | grep -q '"name":"Major"' || fail "Payne name stays locked"
echo "$renamed" | grep -q '"name":"Major Two"' && fail "Payne name must ignore posted rename"
off=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"slug":"sgt-major-payne","enabled":false}')
echo "$off" | grep -q '"enabled":false' || fail "Payne must disable"
echo "$off" | grep -q '"name":"Major"' || fail "disable keeps Major name"
on=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"slug":"sgt-major-payne","enabled":true}')
echo "$on" | grep -q '"enabled":true' || fail "Payne must enable"
stay=$(curl -s -o /tmp/hush-payne-del -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"delete","slug":"sgt-major-payne"}')
test "$stay" != "200" || fail "Payne must not delete"
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
