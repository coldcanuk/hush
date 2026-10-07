#!/bin/sh
# Smoke-test first-launch session routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
. ./tests/hush_free_port.sh
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="$test_home/hush"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" "$@"; }

bin=./hush-relay
port=$(hush_free_port) || exit 1
log=$(mktemp)
cfg=$(mktemp -d)
hush_home=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
export HUSH_HOME="$hush_home"
proj_alpha=$(mktemp -d)
bad_canvas=$(mktemp)
min1_copy=$(mktemp)
no_major_clone=$(mktemp)
bad_role=$(mktemp)
min1_sentry=$(mktemp)
cross_skill=$(mktemp)
noprov_agent=$(mktemp)
bad_agent=$(mktemp)
payne_del=$(mktemp)
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "$cfg" "$hush_home" "$proj_alpha"
    rm -f "$bad_canvas" "$min1_copy" "$no_major_clone" "$bad_role" \
        "$min1_sentry" "$cross_skill" "$noprov_agent" "$bad_agent" "$payne_del"
}
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
echo "$skills" | grep -q '"scopes":\["system","robot"\]' || fail "skills missing two product scopes"
echo "$skills" | grep -q '"scopes":\["system","user","robot"\]' && fail "user must not be a product scope"
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
echo "$skills" | grep -q 'system:bring-back' && fail "folded bring-back must be absent from catalog"
echo "$skills" | grep -q 'system:intro-once' || fail "skills missing intro-once"
sheet=../docs/research/RESEARCH_TEMPLATE_COMB.md
test -f "$sheet" || fail "quality panel missing"
grep -q 'C1 Distinctness' "$sheet" || fail "panel missing C1"
grep -q 'C6 Actionability' "$sheet" || fail "panel missing C6"
grep -q '| C1 Distinctness | 7.2 | 8.3 |' "$sheet" || fail "panel missing C1 post score"
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
echo "$html" | grep -q 'function presenceSlugFor' || fail "HTML missing presenceSlugFor"
echo "$html" | grep -q '/api/presence' || fail "HTML missing /api/presence tick"
pres=$(curl -sf "http://127.0.0.1:${port}/api/presence")
echo "$pres" | grep -q '"ok":true' || fail "presence json"
echo "$pres" | grep -q '"lines"' || fail "presence missing lines"
echo "$html" | grep -q '30315' || fail "HTML missing NIP-38 kind filter"
# UI-M9: no persistent tool rail. Drag/park persistence helpers are gone;
# the header KIT stamp toggles the #kit-menu overlay only.
if echo "$html" | grep -q 'function saveRail'; then
    fail "tool rail saveRail must be gone (UI-M9)"
fi
if echo "$html" | grep -q 'placeRailAtBrand'; then
    fail "tool rail placeRailAtBrand must be gone (UI-M9)"
fi
if echo "$html" | grep -q 'hush-rail'; then
    fail "tool rail hush-rail persistence must be gone (UI-M9)"
fi
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
echo "$html" | grep -q 'id="seed-provider"' || fail "HTML missing team provider selection"
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
grep -q 'hush_http_json_has_key' src/api_channels.c || fail "manage about must use hush_http_json_has_key"
manage=$(sed -n '/^static hush_status_t hush_http_channel_about/,/^}/p' src/api_channels.c)
echo "$manage" | grep -q 'hush_http_json_has_key(body, "about")' \
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
grep -q 'default on-deck line is not a chat note' ../UI_SPEC.md \
    || fail "spec must say the default intro is not a chat note"
grep -q 'A custom intro is one chat note' ../docs/plan/PLAN_CHAT_PILLS_INTRO_ACKS.md \
    || fail "plan must say a custom intro is one chat note"
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
echo "$html" | grep -q 'provider-config' || fail "HTML missing per-provider configuration"
echo "$html" | grep -q 'id="provider-drawer"' || fail "HTML missing provider drawer"
echo "$html" | grep -q 'id="provider-oauth"' || fail "HTML missing OAuth login button"
echo "$html" | grep -q 'A successful reply confirms the connection' || fail "HTML missing honest connection status"
echo "$html" | grep -q 'prov-status' || fail "HTML missing provider status labels"
echo "$html" | grep -q 'grid-template-columns: 1fr 1fr 1fr' || fail "HTML provider picker must be three columns"
echo "$html" | grep -q 'api token present' || fail "HTML missing provider token status text"
echo "$html" | grep -q 'authenticated' || fail "HTML missing provider auth status text"
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
echo "$html" | grep -q 'chan-options' || fail "HTML missing channel options"
echo "$html" | grep -q 'chan-voice' || fail "HTML missing channel voice"
echo "$html" | grep -q 'robot-call' || fail "HTML missing robot call"
echo "$html" | grep -q 'tile-mute' || fail "HTML missing tile mute"
# UI-M9: persistent tool rail deleted; header KIT menu only. Boards access
# stays the M8 overlay drawer. Fat bottom pills are folder tabs now.
if echo "$html" | grep -q 'id="tool-rail"'; then fail "tool rail must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'rail-grip'; then fail "rail grip must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'rail-body'; then fail "rail body must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'rail-grid'; then fail "rail grid must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'rail-pop'; then fail "rail popovers must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'utility-rail'; then fail "utility rail must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'repeat(4, 1fr) auto'; then fail "fat bottom-pill grid must be gone (UI-M9)"; fi
echo "$html" | grep -q 'id="kit-menu"' || fail "HTML missing KIT menu overlay (UI-M9)"
echo "$html" | grep -q 'id="folder-tabs"' || fail "HTML missing folder-tab strip (UI-M9)"
echo "$html" | grep -q 'folder-tab' || fail "HTML missing folder-tab markers (UI-M9)"
echo "$html" | grep -q 'fo-folder-tabs' || fail "HTML missing fo-folder-tabs marker (UI-M9)"
echo "$html" | grep -q 'UI-M9' || fail "HTML missing UI-M9 markers"
echo "$html" | grep -q 'id="qb-1"' || fail "HTML missing spellbar slot 1 (UI-M9 keeps 1-4)"
echo "$html" | grep -q 'id="qb-4"' || fail "HTML missing spellbar slot 4 (UI-M9 keeps 1-4)"
echo "$html" | grep -q 'id="qb-config"' || fail "HTML missing spellbar gear (UI-M9 keeps gear)"
echo "$html" | grep -q 'id="qb-editor"' || fail "HTML missing spellbar picker (UI-M9 keeps gear)"
echo "$html" | grep -q 'quickbarTyping' || fail "HTML missing spellbar typing guard (UI-M9)"
echo "$html" | grep -q 'id="thread-resize"' || fail "HTML missing thread resize"
echo "$html" | grep -q 'id="thread-pills"' || fail "HTML missing thread pills"
echo "$html" | grep -q 'id="thread-mention"' || fail "HTML missing thread mention"
if echo "$html" | grep -q 'id="rail-docks"'; then fail "rail docks must be gone"; fi
if echo "$html" | grep -q 'railAnchor'; then fail "rail docks/anchors must be gone"; fi
echo "$html" | grep -q 'reply_to' || fail "HTML missing reply_to indent"
echo "$html" | grep -q 'note reply' || fail "HTML missing reply class"
echo "$html" | grep -q 'id="thread-pane"' || fail "HTML missing thread pane"
echo "$html" | grep -q 'note.mine' || fail "HTML missing sided thread bubbles"
echo "$html" | grep -q 'Follow-ups go to' || fail "HTML missing 1:1 thread help"
echo "$html" | grep -q '@ a robot to direct its next turn' || fail "HTML missing 1:n thread help"
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
echo "$html" | grep -q 'id="install"' || fail "HTML missing install (KIT menu)"
echo "$html" | grep -q 'Install puts Hush on your app launcher' || fail "HTML missing install copy"
echo "$html" | grep -q 'id="rail-toggle"' || fail "HTML missing rail KIT stamp"
if echo "$html" | grep -q '☰'; then fail "burger glyph must be gone (UI-M6)"; fi
if echo "$html" | grep -qi 'hamburger'; then fail "hamburger chrome must be gone (UI-M6)"; fi
echo "$html" | grep -q 'id="nav-toggle"' || fail "HTML missing BOARDS stamp"
echo "$html" | grep -q '>Boards<' || fail "HTML nav-toggle must read Boards (UI-M6)"
echo "$html" | grep -q '>Kit<' || fail "HTML rail-toggle must read Kit (UI-M6)"
echo "$html" | grep -q 'fo-chrome-hard' || fail "HTML missing UI-M7 chrome-hard materials"
echo "$html" | grep -q 'fo-caret' || fail "HTML missing UI-M7 paper caret"
echo "$html" | grep -q 'id="fo-drawer"' || fail "HTML missing fo-drawer overlay nav (UI-M8)"
if echo "$html" | grep -q 'fo-expand'; then fail "edge tab must be gone (UI-M12a)"; fi
if echo "$html" | grep -q 'syncFoExpand'; then fail "edge tab aria sync must be gone (UI-M12a)"; fi
echo "$html" | grep -q 'UI-M8' || fail "HTML missing UI-M8 markers"
# UI-M11: volume dial owns dispatch-log scroll. The native fat bar stays
# hidden, but the message column owns NO in-column chrome: no track, dot,
# or end-arrow bar. A stereo-style VOLUME dial above the Send switch scrolls
# the log (wheel over dial, clockwise/counter-clockwise drag, keys).
echo "$html" | grep -q 'id="fo-log-wrap"' || fail "HTML missing log wrap (UI-M11)"
if echo "$html" | grep -q 'id="fo-scrollbar"'; then fail "M10 scrollbar must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-track"'; then fail "M10 scroll track must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-dot"'; then fail "M10 scroll dot must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-up"'; then fail "M10 scroll up arrow must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-down"'; then fail "M10 scroll down arrow must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'syncFoScrollbar'; then fail "M10 scrollbar sync must be gone (UI-M11)"; fi
echo "$html" | grep -q 'id="fo-dial"' || fail "HTML missing volume dial (UI-M11)"
echo "$html" | grep -q 'id="fo-dial-knob"' || fail "HTML missing dial knob (UI-M11)"
echo "$html" | grep -q 'id="fo-dial-row"' || fail "HTML missing dial row above Send (UI-M11)"
echo "$html" | grep -q 'scrollbar-width: none' || fail "HTML must hide native scrollbar (UI-M11)"
echo "$html" | grep -q '::-webkit-scrollbar' || fail "HTML must hide webkit scrollbar (UI-M11)"
echo "$html" | grep -q 'syncFoDial' || fail "HTML missing dial sync (UI-M11)"
echo "$html" | grep -q 'foDialScrollBy' || fail "HTML missing dial scroll driver (UI-M11)"
echo "$html" | grep -q 'foDialAngle' || fail "HTML missing dial drag angle (UI-M11)"
echo "$html" | grep -q 'UI-M11' || fail "HTML missing UI-M11 markers"
# UI-M12c: Send is a spring-return metal toggle switch, still the form's
# real submit <button> named "Send", and the UI-M11 dial sits directly above
# it in one composer column. The old "Send Dispatch" slab is gone.
echo "$html" | grep -q '<button id="send" class="fo-switch" type="submit"' \
  || fail "HTML missing Send switch submit button (UI-M12c)"
echo "$html" | grep -q '<span class="fo-switch-cap">Send</span>' \
  || fail "Send switch must be named Send (UI-M12c)"
echo "$html" | grep -q 'class="fo-switch-lever"' || fail "HTML missing Send switch lever (UI-M12c)"
echo "$html" | grep -q 'foSwitchFlick' || fail "HTML missing Send switch flick (UI-M12c)"
echo "$html" | grep -q 'createBiquadFilter' || fail "HTML missing Web Audio switch clicks (UI-M12c)"
if echo "$html" | grep -q 'Send Dispatch</button>'; then
  fail "old Send Dispatch button must be gone (UI-M12c)"
fi
line_of() { echo "$html" | grep -n "$1" | head -n 1 | cut -d: -f1; }
form_at=$(line_of 'id="form"')
col_at=$(line_of 'id="fo-send-col"')
dial_at=$(line_of 'id="fo-dial-row"')
send_at=$(line_of '<button id="send"')
if [ -z "$form_at" ] || [ -z "$col_at" ] || [ -z "$dial_at" ] || [ -z "$send_at" ] \
  || [ "$form_at" -ge "$col_at" ] || [ "$col_at" -ge "$dial_at" ] || [ "$dial_at" -ge "$send_at" ]; then
  fail "dial row must sit directly above Send inside the composer column (UI-M12c)"
fi
# UI-M12c r2: the flick is wired as a real call statement inside the
# #form submit handler (not the CSS comment or the definition), only after
# every submit guard and after Send is disabled, wrapped so a throw cannot
# lock the composer; reduced motion skips the swing in JS and CSS.
submit_at=$(line_of '$("form").addEventListener("submit", async (e) => {')
submit_end=
if [ -n "$submit_at" ]; then
  submit_end=$(echo "$html" | awk -v s="$submit_at" 'NR > s && /^    }\);$/ { print NR; exit }')
fi
flick_at=$(line_of '^ *foSwitchFlick();$')
if [ -z "$submit_at" ] || [ -z "$submit_end" ] || [ -z "$flick_at" ] \
  || [ "$flick_at" -le "$submit_at" ] || [ "$flick_at" -ge "$submit_end" ]; then
  fail "Send switch flick must be called inside the submit handler (UI-M12c)"
fi
mention_at=$(line_of 'if (mentionOpen) return;')
guard_at=$(line_of 'if (sendingMessage) return;')
set_at=$(line_of 'sendingMessage = true;')
off_at=$(line_of '$("send").disabled = true;')
if [ -z "$mention_at" ] || [ -z "$guard_at" ] || [ -z "$set_at" ] || [ -z "$off_at" ] \
  || [ -z "$flick_at" ] || [ -z "$submit_at" ] || [ "$submit_at" -ge "$mention_at" ] \
  || [ "$mention_at" -ge "$guard_at" ] || [ "$guard_at" -ge "$set_at" ] \
  || [ "$set_at" -ge "$off_at" ] || [ "$off_at" -ge "$flick_at" ]; then
  fail "Send switch must flick only after the submit guards (UI-M12c)"
fi
if [ -z "$flick_at" ] || [ "$flick_at" -le 1 ] \
  || ! echo "$html" | sed -n "$((flick_at - 1))p" | grep -q '^ *try {$'; then
  fail "Send switch flick must be wrapped so a throw cannot lock send (UI-M12c)"
fi
def_at=$(line_of '^ *function foSwitchFlick() {$')
calm_at=$(line_of '^ *if (foSwitchCalm.matches) return;$')
add_at=$(line_of 'send.classList.add("fo-flick");')
if [ -z "$def_at" ] || [ -z "$calm_at" ] || [ -z "$add_at" ] \
  || [ "$def_at" -ge "$calm_at" ] || [ "$calm_at" -ge "$add_at" ]; then
  fail "reduced motion must skip the Send switch flick in JS (UI-M12c)"
fi
echo "$html" | grep -q 'foSwitchCalm = window.matchMedia("(prefers-reduced-motion: reduce)")' \
  || fail "reduced motion must skip the Send switch flick in JS (UI-M12c)"
echo "$html" | grep -A1 '^@media (prefers-reduced-motion: reduce) {$' \
  | grep -q '^  \.fo-switch-lever, #send\.fo-switch\.fo-flick \.fo-switch-lever { transition: none; transform: rotate(-32deg); }$' \
  || fail "reduced-motion CSS must pin the Send switch lever (UI-M12c)"
echo "$html" | grep -q '#fo-dial-row #fo-dial { width: 100%;' \
  || fail "dial must span the Send column width (UI-M12c)"
# UI-M12d: one compact button scale (xs 24 / sm 28 / md 32) on :root.
# Visual size stays the tier at every width; phones get the 44px touch hit
# only from a transparent ::before (::after on .switch) inside the touch
# media block, which must never raise a visual height.
echo "$html" | grep -q 'UI-M12d' || fail "HTML missing UI-M12d markers"
echo "$html" | grep -q '^      --btn-h-xs: 24px; --btn-px-xs: 6px; --btn-fs-xs: 0.68rem;$' \
  && echo "$html" | grep -q '^      --btn-h-sm: 28px; --btn-px-sm: 8px; --btn-fs-sm: 0.74rem;$' \
  && echo "$html" | grep -q '^      --btn-h-md: 32px; --btn-px-md: 12px; --btn-fs-md: 0.8rem;$' \
  && echo "$html" | grep -q '^      --btn-lh: 1.1; --btn-hit-min: 24px; --btn-hit-touch: 44px;$' \
  || fail "HTML missing compact button tokens (UI-M12d)"
m12d_at=$(line_of '^/\* ===== UI-M12d compact buttons =====$')
btn_at=$(line_of '^\.btn:where(:not(#nav-toggle, \.leave-actions \.btn)) {$')
if [ -z "$m12d_at" ] || [ -z "$btn_at" ] || [ "$btn_at" -le "$m12d_at" ] \
  || ! echo "$html" | sed -n "$((btn_at + 1))p" | grep -q '^  min-height: var(--btn-h-md); padding: 0 var(--btn-px-md);$'; then
  fail ".btn must use the compact md tier (UI-M12d)"
fi
pill_at=$(line_of '^\.pill button {$')
if [ -z "$m12d_at" ] || [ -z "$pill_at" ] || [ "$pill_at" -le "$m12d_at" ] \
  || ! echo "$html" | sed -n "$((pill_at + 1)),$((pill_at + 3))p" \
    | grep -q 'min-width: var(--btn-h-xs); min-height: var(--btn-h-xs);'; then
  fail ".pill button must be at least 24px (UI-M12d)"
fi
touch_at=$(line_of '^@media (max-width: 640px), (pointer: coarse) {$')
touch_end=
if [ -n "$touch_at" ]; then
  touch_end=$(echo "$html" | awk -v s="$touch_at" 'NR > s && /^}$/ { print NR; exit }')
fi
hit_at=$(line_of '^    #install, \.iconbtn, \.skill-facet, \.think-stop, button\.fo-person, \.tile-mute)::before,$')
if [ -z "$touch_at" ] || [ -z "$touch_end" ] || [ -z "$hit_at" ] \
  || [ "$hit_at" -le "$touch_at" ] || [ "$hit_at" -ge "$touch_end" ] \
  || ! echo "$html" | sed -n "$((hit_at - 4)),$((hit_at))p" | grep -q '^  :where(\.btn, ' \
  || ! echo "$html" | sed -n "$((hit_at - 4)),$((hit_at))p" | grep -q '^    \.pill button, ' \
  || ! echo "$html" | sed -n "$((hit_at + 1))p" | grep -q '^  \.switch::after {$' \
  || ! echo "$html" | sed -n "$((hit_at + 2))p" | grep -q '^    content: ""; position: absolute; left: 0; right: 0;$' \
  || ! echo "$html" | sed -n "$((hit_at + 3))p" | grep -q '^    top: min(0px, calc(50% - var(--btn-hit-touch) / 2));$' \
  || ! echo "$html" | sed -n "$((hit_at + 4))p" | grep -q '^    bottom: min(0px, calc(50% - var(--btn-hit-touch) / 2));$' \
  || ! echo "$html" | sed -n "$((hit_at + 5))p" | grep -q '^    background: transparent; pointer-events: auto;$'; then
  fail "phone touch hit must come from a transparent pseudo-element (UI-M12d)"
fi
# The touch block adds container spacing, never control size: no height,
# padding* or block-size anywhere in it except these three container lines.
if echo "$html" | sed -n "${touch_at},${touch_end}p" \
  | grep -v -x -F \
    -e '  .mention-box { padding-block: 8px; }' \
    -e '  #fo-drawer { padding-bottom: 64px; } /* last row clears the fixed #quick-bar */' \
    -e '  .fav-item { padding-block: 8px; }' \
  | grep -q 'height\|padding\|block-size'; then
  fail "touch block must not raise a visual height (UI-M12d)"
fi
# The allowlist covers whole one-line rules. CSS comments (one-line or
# multi-line) are stripped first; then the previous non-blank line before
# each allowlisted line must end in { or }. The check does not parse CSS
# strings or escapes (a /* inside a quoted string, or an escaped brace in a
# selector, gets past it; tracked in #229).
# #229 V6/V6b/V7/V7b: blank quoted strings before comment strip; a \{ or \}
# is not a rule boundary (escaped braces must not fake allowlist adjacency).
if echo "$html" | sed -n "${touch_at},${touch_end}p" | awk '
  function blank_strings(s,    out, i, n, c, q) {
    out = ""; n = length(s); q = ""
    for (i = 1; i <= n; i++) {
      c = substr(s, i, 1)
      if (q != "") {
        if (c == "\\" && i < n) { out = out "  "; i++; continue }
        if (c == q) q = ""
        out = out " "
        continue
      }
      if (c == "\"" || c == "\47") { q = c; out = out " "; continue }
      out = out c
    }
    return out
  }
  function ends_brace(s,    ch, i) {
    for (i = length(s); i >= 1; i--) {
      ch = substr(s, i, 1)
      if (ch == " " || ch == "\t") continue
      if (ch == "{" || ch == "}") {
        if (i > 1 && substr(s, i - 1, 1) == "\\") return 0
        return 1
      }
      return 0
    }
    return 0
  }
  { t = blank_strings($0); c = ""
    while (t != "") {
      if (inc) { e = index(t, "*/"); if (!e) break; t = substr(t, e + 2); inc = 0; continue }
      s = index(t, "/*"); if (!s) { c = c t; break }
      c = c substr(t, 1, s - 1); t = substr(t, s + 2); inc = 1
    }
    sub(/[ \t]+$/, "", c) }
  $0 == "  .mention-box { padding-block: 8px; }" ||
  $0 == "  #fo-drawer { padding-bottom: 64px; } /* last row clears the fixed #quick-bar */" ||
  $0 == "  .fav-item { padding-block: 8px; }" { if (!ends_brace(prev)) bad = 1 }
  c ~ /[^ \t]/ { prev = c } END { exit bad ? 0 : 1 }'; then
  fail "touch allowlist lines must stay whole rules (UI-M12d)"
fi
if echo "$html" | sed -n "${touch_at},${touch_end}p" | grep -q -e '--btn-[a-z0-9-]*[[:space:]]*:'; then
  fail "touch block must not redefine --btn- tokens (UI-M12d)"
fi
# Each hit control must be the positioning box of its own ::before, so the
# position: relative list is the hit list minus .tile-mute (already absolute).
rel_at=$(line_of '^    #install, \.iconbtn, \.skill-facet, \.think-stop, button\.fo-person) { position: relative; }$')
if [ -z "$rel_at" ] || [ "$rel_at" -le "$touch_at" ] || [ "$rel_at" -ge "$hit_at" ] \
  || [ "$(echo "$html" | sed -n "$((rel_at - 4)),$((rel_at))p" | sed 's/) { position: relative; }$//')" \
    != "$(echo "$html" | sed -n "$((hit_at - 4)),$((hit_at))p" | sed 's/, \.tile-mute)::before,$//')" ]; then
  fail "touch hit controls must be position: relative (UI-M12d)"
fi
wide_at=$(line_of '^  :where(\.drawer-x, \.icon-plus, \.icon-minus, \.chan-options, \.chan-voice, \.prov-row \.cfg)::before {$')
if [ -z "$wide_at" ] || [ "$wide_at" -le "$hit_at" ] || [ "$wide_at" -ge "$touch_end" ] \
  || ! echo "$html" | sed -n "$((wide_at + 1))p" | grep -q '^    left: min(0px, calc(50% - var(--btn-hit-touch) / 2));$' \
  || ! echo "$html" | sed -n "$((wide_at + 2))p" | grep -q '^    right: min(0px, calc(50% - var(--btn-hit-touch) / 2));$'; then
  fail "isolated square hit must widen to 44px (UI-M12d)"
fi
for rule in '  .menu button + button, .mention-box button + button { margin-top: 16px; }' \
  '  #fo-individuals, #fo-individual-list { gap: 8px; }' \
  '  #fo-drawer { padding-bottom: 64px; } /* last row clears the fixed #quick-bar */'; do
  echo "$html" | sed -n "${touch_at},${touch_end}p" | grep -q -x -F -e "$rule" \
    || fail "touch spacing must keep neighbouring hit areas apart (UI-M12d)"
done
# r3 (Ops F2/F6): BOARDS drawer sections scroll instead of squashing, at
# every width (the inventory grid stays inside its frame); the main-view
# roster rows are static, so the touch block leaves their pitch alone (the
# you row and the panel top stay visible at 375x812).
# #229 P3-1: shrink rule must be live (not comment-wrapped) and the touch
# block must not re-enable flex-shrink on drawer sections / inventory.
shrink_at=$(echo "$html" | grep -n -x -F -e '#fo-drawer > * { flex-shrink: 0; } /* drawer scrolls; its sections never squash */' | head -n 1 | cut -d: -f1)
if [ -z "$shrink_at" ] || [ "$shrink_at" -le "$m12d_at" ] || [ "$shrink_at" -ge "$touch_at" ]; then
  fail "BOARDS drawer sections must not shrink (UI-M12d)"
fi
# O5: the exact shrink line must not sit inside an open /* … */ comment.
if echo "$html" | awk -v n="$shrink_at" '
  NR < n {
    t = $0
    while (t != "") {
      if (inc) { e = index(t, "*/"); if (!e) { t = ""; break } t = substr(t, e + 2); inc = 0; continue }
      s = index(t, "/*"); if (!s) break
      t = substr(t, s + 2); inc = 1
    }
  }
  NR == n { exit inc ? 0 : 1 }
'; then
  fail "drawer flex-shrink:0 must not be comment-wrapped (UI-M12d #229 O5)"
fi
if echo "$html" | sed -n "${touch_at},${touch_end}p" | grep -q 'flex-shrink'; then
  fail "touch block must not override flex-shrink (UI-M12d #229 O6/O7)"
fi
# #229 P3-2: no touch rule on the main-view roster (broader than name grep).
if echo "$html" | sed -n "${touch_at},${touch_end}p" \
  | grep -qE 'fo-roster-list|roster-pane|fo-roster-sec|fo-person\.static'; then
  fail "touch block must not change the main-view roster (UI-M12d #229 O3)"
fi
# O4: base roster list pitch stays 4px; no later #fo-roster-list gap bump.
echo "$html" | grep -q -x -F -e '#fo-roster-list, #fo-individual-list { display: flex; flex-direction: column; gap: 4px; }' \
  || fail "main-view roster list must keep gap: 4px (UI-M12d #229 O4)"
if echo "$html" | grep -E -e '^#fo-roster-list[[:space:]]*\{[^}]*gap:[[:space:]]*(1[6-9]|[2-9][0-9])px'; then
  fail "main-view roster list gap must stay 4px (UI-M12d #229 O4)"
fi
menu_at=$(line_of '^\.menu button, \.mention-box button {$')
if [ -z "$menu_at" ] || [ "$menu_at" -le "$m12d_at" ] || [ "$menu_at" -ge "$touch_at" ] \
  || ! echo "$html" | sed -n "$((menu_at + 1))p" | grep -q '^  min-height: var(--btn-h-sm); padding: 0 var(--btn-px-sm);$'; then
  fail ".menu button must use the compact sm tier, not 44px (UI-M12d)"
fi
# r4 (Ops F2/F7): the drawer stats are inline items (no <br> stack), so the
# BOARDS drawer fits at 1440x900; at <= 480px the robot-editor actions show
# the verb only, with the noun as visually hidden .act-noun text and the
# full name in aria-label.
if ! echo "$html" | grep -q -x -F -e '#stats .stat { white-space: nowrap; }' \
  || echo "$html" | grep -q 'sockets<br>'; then
  fail "drawer stats must flow inline so the drawer fits (UI-M12d r4)"
fi
noun_at=$(line_of '^  \.act-noun { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }$')
if [ -z "$noun_at" ] || [ "$noun_at" -le "$touch_end" ] \
  || ! echo "$html" | sed -n "$((noun_at - 1))p" | grep -q '^@media (max-width: 480px) {$'; then
  fail "robot-editor action nouns must be hidden only at <= 480px (UI-M12d r4)"
fi
for a in 'id="agent-save" type="button" aria-label="Create robot">Create<span class="act-noun"> robot</span>' \
  'id="agent-clone" type="button" aria-label="Clone Robot" disabled>Clone<span class="act-noun"> Robot</span>' \
  'id="agent-delete" type="button" aria-label="Delete Robot" disabled>Delete<span class="act-noun"> Robot</span>'; do
  echo "$html" | grep -q -F -e "$a" || fail "robot-editor actions must keep their full accessible name (UI-M12d r4)"
done
# Pre-walk: the stats separators lead each item inside a clipped row, so a
# wrapped line never ends (or starts) on a dangling "·".
if ! echo "$html" | grep -q -x -F -e '#stats .stats-clip { display: block; overflow: hidden; }' \
  || ! echo "$html" | grep -q -x -F -e '#stats .stats-row { display: block; margin-left: -1.2em; }' \
  || echo "$html" | grep -q -F -e ':not(:last-child)::after { content: " ·"; }'; then
  fail "drawer stats separators must not dangle at wrapped line ends (pre-walk)"
fi
# r5 (Ops F2 fallback): the drawer bottom fade sits after the touch block
# and is switched only by the overflow test, so a drawer that fits shows
# no cue. Pre-walk: 12px and not pulled up, so stats line 1 stays crisp.
# Pre-walk r4 (Ops FAIL-A): the test is the last row's bottom against the
# client box, so padding-only overflow shows no cue. r5 (Ops FAIL-1): the
# visible bottom is the #quick-bar top where it overlays the drawer, and the
# fade is lifted to end there.
cue_at=$(line_of '^#fo-drawer\.is-overflowing::after { content: ""; flex: 0 0 12px; position: sticky; bottom: -10px; background: linear-gradient(transparent, var(--surface)); pointer-events: none; }$')
if [ -z "$cue_at" ] || [ "$cue_at" -le "$touch_end" ] \
  || ! echo "$html" | grep -q -x -F -e '      d.classList.toggle("is-overflowing", last > seen + 0.5);' \
  || ! echo "$html" | grep -q -x -F -e '#fo-drawer.is-overflowing::after { bottom: var(--fade-bottom, -10px); }' \
  || ! echo "$html" | grep -q -F -e 'const drawerRO = new ResizeObserver(syncDrawerOverflow);'; then
  fail "drawer bottom fade must show only when the drawer overflows (UI-M12d r5)"
fi
# #237 F-C: fade latch uses leaf content bottom (padding excluded).
echo "$html" | grep -q -F 'parseFloat(cs.paddingBottom)' \
  || fail "drawer fade must measure content bottom (exclude child padding)"
echo "$html" | grep -q -F 'const visit = (el) =>' \
  || fail "drawer fade must walk leaves (F-C': nested row padding must not count)"
style_end=$(line_of '^</style>$')
if [ -z "$style_end" ] || [ "$style_end" -le "$touch_end" ] \
  || echo "$html" | sed -n "${m12d_at},${style_end}p" | grep -q '\(height\|width\|block-size\): *44px'; then
  fail "UI-M12d block must not size a control at 44px"
fi
# #229 P3-4 O13: a second <style> with min-height:44px used to slip past
# the first-</style> scan. Keep one style block; anything after it must
# not size controls at 44px (incl. min-height).
style_n=$(echo "$html" | grep -c -F -e '</style>' || true)
if [ "$style_n" -ne 1 ]; then
  fail "demo must keep a single </style> (UI-M12d #229 O13)"
fi
if echo "$html" | sed -n "$((style_end + 1)),\$p"   | grep -E '(min-)?(height|width|block-size):[[:space:]]*44px'; then
  fail "no trailing <style> may size a control at 44px (UI-M12d #229 O13)"
fi
# #229 P3-4 O12: --btn-h-* must stay compact; a later coarse media must not
# raise tokens to 44px (token ban covers only the touch block above).
if echo "$html" | grep -E -e '--btn-h-(xs|sm|md):[[:space:]]*44px'; then
  fail "btn height tokens must stay compact, never 44px (UI-M12d #229 O12)"
fi

# ---- #229 gate hardening (P3-1..P3-4 + V/X/Y/W) ----
# P3-3 O8: button.fo-person stays on the sm tier between M12d and touch.
fo_person_at=$(line_of '^button\.fo-person { min-height: var(--btn-h-sm); }$')
if [ -z "$fo_person_at" ] || [ "$fo_person_at" -le "$m12d_at" ] || [ "$fo_person_at" -ge "$touch_at" ]; then
  fail "button.fo-person must use sm tier before the touch block (UI-M12d #229 O8)"
fi
# P3-3 O9/O9b/O17: after the touch pitch rule, no later gap/row-gap override
# on the BOARDS individuals lists (0 collapses the shared hit; 16 reopens F2).
indiv_gap_at=$(echo "$html" | sed -n "${touch_at},${touch_end}p" \
  | grep -n -x -F -e '  #fo-individuals, #fo-individual-list { gap: 8px; }' \
  | head -n 1 | cut -d: -f1)
if [ -z "$indiv_gap_at" ]; then
  fail "touch pitch for BOARDS individuals must stay gap: 8px (UI-M12d)"
fi
indiv_gap_abs=$((touch_at + indiv_gap_at - 1))
if echo "$html" | sed -n "$((indiv_gap_abs + 1)),${style_end}p" \
  | grep -E -e '#fo-individual(s|-list)[^{]*\{[^}]*(row-)?gap:[[:space:]]*(0|0px|16px)([^0-9]|$)'; then
  fail "BOARDS individuals pitch must not be overridden after touch (UI-M12d #229 O9/O17)"
fi

# X1: .act-noun clip must not be undone by a later rule.
if echo "$html" | sed -n "$((noun_at + 1)),${style_end}p" \
  | grep -E '\.act-noun[^{]*\{[^}]*(position:[[:space:]]*static|clip-path:[[:space:]]*none)'; then
  fail ".act-noun must stay visually hidden at <=480px (UI-M12d #229 X1)"
fi
# X2: setActionLabel keeps an .act-noun span (not verb+" "+noun textContent).
set_at=$(echo "$html" | grep -n -F -e '    function setActionLabel(el, verb, noun) {' | head -n 1 | cut -d: -f1)
if [ -z "$set_at" ]; then
  fail "setActionLabel missing (UI-M12d #229 X2)"
fi
set_end=$(echo "$html" | awk -v s="$set_at" 'NR > s && /^    }$/ { print NR; exit }')
if [ -z "$set_end" ] \
  || ! echo "$html" | sed -n "${set_at},${set_end}p" | grep -q -F 'createElement("span")' \
  || ! echo "$html" | sed -n "${set_at},${set_end}p" | grep -q -F 'className = "act-noun"' \
  || ! echo "$html" | sed -n "${set_at},${set_end}p" | grep -q -F 'replaceChildren(verb, hid)' \
  || echo "$html" | sed -n "${set_at},${set_end}p" | grep -q 'textContent = verb'; then
  fail "setActionLabel must keep .act-noun span, not two-line text (UI-M12d #229 X2)"
fi
# X3: stats items stay inline (no display:block stack on #stats > span).
if echo "$html" | grep -E '#stats[[:space:]]*>[[:space:]]*span[^{]*\{[^}]*display:[[:space:]]*block'; then
  fail "drawer stats must not stack as display:block spans (UI-M12d #229 X3)"
fi
# X4: no createElement("br") (sockets<br> already banned; dynamic br too).
if echo "$html" | grep -q -F 'createElement("br")' || echo "$html" | grep -q -F "createElement('br')"; then
  fail "drawer must not insert <br> between stats (UI-M12d #229 X4)"
fi

# Y1 / W6 / W9: only .is-overflowing::after owns the fade; no bare ::after,
# display:none, or background:none overrides after the locked rules.
if echo "$html" | grep -E -e '^#fo-drawer::after[[:space:]]*\{'; then
  fail "drawer fade must be #fo-drawer.is-overflowing::after only (UI-M12d #229 Y1)"
fi
if echo "$html" | sed -n "$((cue_at + 1)),${style_end}p" \
  | grep -E '#fo-drawer\.is-overflowing::after[^{]*\{[^}]*(display:[[:space:]]*none|background:[[:space:]]*none)'; then
  fail "drawer fade ::after must not be blanked later (UI-M12d #229 W6/W9)"
fi
# W7: locked cue rule must not be comment-wrapped (line_of still finds it).
if echo "$html" | awk -v n="$cue_at" '
  NR < n {
    t = $0
    while (t != "") {
      if (inc) { e = index(t, "*/"); if (!e) { t = ""; break } t = substr(t, e + 2); inc = 0; continue }
      s = index(t, "/*"); if (!s) break
      t = substr(t, s + 2); inc = 1
    }
  }
  NR == n { exit inc ? 0 : 1 }
'; then
  fail "drawer fade cue rule must not be comment-wrapped (UI-M12d #229 W7)"
fi
# W1: both observe calls present.
echo "$html" | grep -q -x -F -e '      drawerRO.observe($("fo-drawer"));' \
  || fail "drawerRO must observe #fo-drawer (UI-M12d #229 W1)"
echo "$html" | grep -q -x -F -e '      [...$("fo-drawer").children].forEach((c) => drawerRO.observe(c));' \
  || fail "drawerRO must observe drawer children (UI-M12d #229 W1)"
# W2: ResizeObserver gate is live (not if (false && …)).
echo "$html" | grep -q -x -F -e '    if (window.ResizeObserver) {' \
  || fail "ResizeObserver gate must be if (window.ResizeObserver) (UI-M12d #229 W2)"
# W3: syncDrawerOverflow has no early return before the toggle.
sync_at=$(echo "$html" | grep -n -F -e '    function syncDrawerOverflow() {' | head -n 1 | cut -d: -f1)
sync_end=$(echo "$html" | awk -v s="$sync_at" 'NR > s && /^    }$/ { print NR; exit }')
if [ -z "$sync_at" ] || [ -z "$sync_end" ]; then
  fail "syncDrawerOverflow missing (UI-M12d #229 W3)"
fi
if echo "$html" | sed -n "$((sync_at + 1)),$((sync_end - 1))p" | grep -qE '^[[:space:]]*return;'; then
  fail "syncDrawerOverflow must not early-return (UI-M12d #229 W3)"
fi
# W4: exactly one is-overflowing toggle (no invert).
tog_n=$(echo "$html" | grep -c -F 'classList.toggle("is-overflowing"' || true)
if [ "$tog_n" -ne 1 ]; then
  fail "exactly one is-overflowing toggle allowed (UI-M12d #229 W4)"
fi
# W8: overflow test uses measured last/seen, not a huge constant cue.
if echo "$html" | sed -n "${sync_at},${sync_end}p" | grep -qE 'cue[[:space:]]*=[[:space:]]*1e9|last[[:space:]]*=[[:space:]]*1e9'; then
  fail "syncDrawerOverflow must not force a huge cue constant (UI-M12d #229 W8)"
fi
echo "$html" | grep -q -x -F -e '      d.classList.toggle("is-overflowing", last > seen + 0.5);' \
  || fail "overflow toggle must compare last > seen + 0.5 (UI-M12d #229 W8)"

echo "$html" | grep -q 'contextmenu' || fail "HTML missing channel contextmenu"
# #241 walk nits: provider note, favorites plural, Esc stack, B3 claim pins.
# (Server default theme field-office is pinned later via vibe JSON.)
echo "$html" | grep -q -F 'Provider / API priority (required)' \
  || fail "#241 provider note must not say harness"
if echo "$html" | grep -q -F 'Harness / API priority'; then
  fail "#241 provider note still says harness"
fi
echo "$html" | grep -q -F '((fav.skills || []).length === 1) ? "1 skill"' \
  || fail "#241 favorites must pluralize 1 skill"
# dismissJourneyLayer must list new-chan-drawer and settings for Esc (not a
# vacuous whole-file id grep — id="new-chan-drawer" always exists).
if ! echo "$html" | grep -A8 'function dismissJourneyLayer' | grep -q 'new-chan-drawer'; then
  fail "#241 Esc must dismiss new-chan-drawer"
fi
# The Esc order array itself must end with "settings" (the later
# LAYER_IDS branch must not satisfy this pin). #283: it starts with the
# Developer Log, which opens over Settings, and lists Manage Channel.
if ! echo "$html" | grep -A3 'const order = \["dev-log-drawer", "avatar-drawer"' | grep -q -F '"settings"];'; then
  fail "#241/#283 Esc order array must start with dev-log-drawer and include settings"
fi
if ! echo "$html" | grep -A3 'const order = \["dev-log-drawer"' | grep -q -F '"manage-chan"'; then
  fail "#283 Esc order array must include manage-chan"
fi
# B3 / Gauge: claimed #241 UI bytes whose revert must FAIL this gate.
echo "$html" | grep -q -F 'value="field-office" checked' \
  || fail "#241 M1b theme radio default must be field-office"
# #241 r4 F1/F2: Meet Major — no false Profile pointer; one At ease line.
if echo "$html" | grep -q -F 'Public key on file'; then
  fail "#241 F1 Meet Major must not point at Profile for Major's key"
fi
echo "$html" | grep -q -F "<p><strong>At ease.</strong> I'm Major. Tell me what you want built and I'll find — or raise — the right robot for the job.</p>" \
  || fail "#241 F2 Meet Major must ship the single At ease intro"
# F2: the Meet Major card source (page === "payne" .. meet-payne) holds
# exactly one "At ease", one "I'm Major" and one find/raise phrase, in any
# tag form (strong or plain <p>, literal or about-text line); no p.npub.
payne_src=$(echo "$html" | sed -n '/page === "payne"/,/meet-payne/p')
at_ease_n=$(echo "$payne_src" | grep -o 'At ease' | wc -l)
test "$at_ease_n" -eq 1 \
  || fail "#241 F2 Meet Major card source must contain 'At ease' exactly once (got $at_ease_n)"
major_n=$(echo "$payne_src" | grep -o "I'm Major" | wc -l)
test "$major_n" -eq 1 \
  || fail "#241 F2 Meet Major card source must contain 'I'm Major' exactly once (got $major_n)"
raise_n=$(echo "$payne_src" | grep -o 'find — or raise' | wc -l)
test "$raise_n" -eq 1 \
  || fail "#241 F2 Meet Major card source must contain 'find — or raise' exactly once (got $raise_n)"
if echo "$payne_src" | grep -q '\.about'; then
  fail "#241 F2 Meet Major card no longer renders Major's about text (disclosed); found .about"
fi
if echo "$html" | sed -n '/page === "payne"/,/meet-payne/p' | grep -q 'p.npub'; then
  fail "#241 Meet Major must not reference p.npub on the gate card"
fi
# F3 Settings a11y helpers present.
echo "$html" | grep -q -F 'function openSettings()' \
  || fail "#241 F3 openSettings missing"
echo "$html" | grep -q -F 'function closeSettings()' \
  || fail "#241 F3 closeSettings missing"
# #283: the Settings return target now lives in the shared layer helper
# (openLayer records it, closeLayer hands focus back).
echo "$html" | grep -q -F 'openLayer("settings", back,' \
  || fail "#241 F3/#283 openSettings must record its return target via openLayer"
echo "$html" | grep -A2 'function closeSettings()' | grep -q -F 'closeLayer("settings");' \
  || fail "#241 F3/#283 closeSettings must close through closeLayer (return focus)"
echo "$html" | grep -q -F 'function settingsFocusables()' \
  || fail "#241 F3 settingsFocusables missing"
if ! echo "$html" | grep -A20 'function dismissJourneyLayer' | grep -q -F 'if (LAYER_IDS.indexOf(order[k]) >= 0) closeLayer(order[k]);'; then
  fail "#241 F3/#283 Esc must close Settings and the shared layers via closeLayer (return focus)"
fi
echo "$html" | grep -q -F 'lastStatus = { ok: false };' \
  || fail "#241 M5 tick catch must clear lastStatus"
echo "$html" | grep -q -F 'paintRosterPane(lastStatus);' \
  || fail "#241 M5 tick catch must repaint roster/feed"
echo "$html" | grep -q -F 'err.scrollIntoView({ block: "nearest", behavior: "smooth" })' \
  || fail "#241 M7a skillNotice must scrollIntoView"
# M7b: agent-save path must call skillNotice (other keep-one sites exist).
if ! echo "$html" | grep -A25 '\$("agent-save").addEventListener("click"' \
    | grep -q -F 'skillNotice("Keep at least one skill equipped.")'; then
  fail "#241 M7b agent-save path must use skillNotice"
fi
echo "$html" | grep -q -F 'st.ok ? ("Relay listening"' \
  || fail "#241 M9a status feed must use plain Relay listening copy"
if echo "$html" | grep -q -F '"RELAY LIVE"'; then
  fail "#241 M9a status feed must not say RELAY LIVE"
fi
# M10: theme change posts /api/profile only when logged in.
if ! echo "$html" | grep -A12 "input\[name='theme'\]" | grep -q 'if (!session.logged_in) return;'; then
  # fallback: nearby comment + guard
  if ! echo "$html" | grep -B2 -A6 'api("/api/profile", { theme:' | grep -q 'if (!session.logged_in) return;'; then
    fail "#241 M10 theme POST must guard session.logged_in"
  fi
fi
# B1: outside/backdrop click must test panel containment (not drawer.contains).
echo "$html" | grep -q -F 'panel.contains(ev.target)' \
  || fail "#241 B1 outside click must use panel.contains (not drawer.contains)"
if echo "$html" | grep -A8 'const d = \$("new-chan-drawer");' | grep -q 'd.contains(ev.target)'; then
  fail "#241 B1 must not use drawer.contains for outside click"
fi
echo "$html" | grep -q 'id="provider-key-add"' || fail "HTML missing provider + pills"
echo "$html" | grep -q 'id="provider-username"' || fail "HTML missing provider username"
echo "$html" | grep -q 'id="provider-password"' || fail "HTML missing provider password"
echo "$html" | grep -q 'id="provider-token"' || fail "HTML missing provider token"
echo "$html" | grep -q 'id="provider-passkey"' || fail "HTML missing provider passkey"
echo "$html" | grep -q '/api/provider' || fail "HTML missing provider route"
echo "$html" | grep -q 'pass show hush/providers/' || fail "HTML missing provider retrieve CLI"
echo "$html" | grep -q 'Cline CLI' || fail "HTML missing Cline CLI setup copy"
echo "$html" | grep -q 'Delete Robot' || fail "HTML missing delete robot"
echo "$html" | grep -q 'Create robot' || fail "HTML missing create robot"
echo "$html" | grep -q -F 'setActionLabel($("agent-save"), "Save", "Robot")' || fail "HTML missing save robot"
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
# PE-2: the Armory groups gems by lifetime shelf (ALWAYS-ON | ON-CALL,
# exact wording); System / This-robot scope stays as a secondary facet
# (tabs/chips), not the shelf grouping. No third disk tree.
echo "$html" | grep -q 'ALWAYS-ON' || fail "armory must show the ALWAYS-ON shelf"
echo "$html" | grep -q 'ON-CALL' || fail "armory must show the ON-CALL shelf"
echo "$html" | grep -q 'skill-shelf' || fail "HTML missing lifetime shelves"
echo "$html" | grep -q 'skill-facet' || fail "HTML missing scope facet tabs"
echo "$html" | grep -q 'skill-life' || fail "HTML missing gem lifetime chips"
echo "$html" | grep -q 'System (application-wide)' \
    || fail "HTML missing system forge scope"
echo "$html" | grep -q 'This robot' || fail "HTML missing robot forge scope"
if echo "$html" | grep -q 'User-wide'; then
    fail "User-wide must not be a skill bucket"
fi
echo "$html" | grep -q 'id="skill-loadout"' || fail "HTML missing skill loadout"
if echo "$html" | grep -q 'id="hive-skill-cycle"'; then
    fail "hive skill stash must not sit on the main nav"
fi
if echo "$html" | grep -q 'id="hive-armory"'; then
    fail "hive-armory must not sit on the main nav"
fi
echo "$html" | grep -q 'function toggleRobotInventory' || fail "HTML missing i-key inventory toggle"
echo "$html" | grep -q 'if (!isDevLogNote(k))' \
    || fail "thread kids must hide Mention received"
echo "$html" | grep -q 'function attachLoadout' || fail "HTML missing attachLoadout"
echo "$html" | grep -q 'body.skill_0 = ""' || fail "empty loadout must post skill_0"
echo "$html" | grep -q 'body.nskills = equippedSkills.length' || fail "save must post nskills"
echo "$html" | grep -q 'Keep at least one skill equipped' || fail "HTML missing PE-3 min-1 block copy"
echo "$html" | grep -q 'id="agent-clone"' || fail "HTML missing clone control"
echo "$html" | grep -q 'skillWatermarks' || fail "HTML missing skillWatermarks"
if echo "$html" | grep -q 'makeSkillChip'; then
    fail "chip-wall makeSkillChip must be gone"
fi
echo "$html" | grep -q 'action: \"clone\"' || fail "HTML missing clone action"
echo "$html" | grep -q 'id="skill-forge-open"' || fail "HTML missing forge control"
echo "$html" | grep -q 'id="forge-drawer"' || fail "HTML missing forge drawer"
echo "$html" | grep -q 'w: 1, h: 1' || fail "inventory tiles must be equal 1x1"
if echo "$html" | grep -q 'PAYNE_SLUG ? "1x3"'; then
    fail "Payne must not be a 1x3 inventory exception"
fi
echo "$html" | grep -q 'el.title = it.name' || fail "hover title must use robot name"
echo "$html" | grep -q 'id="agent-enabled"' || fail "HTML missing enable slider"
echo "$html" | grep -q 'id="agent-intro-enabled"' || fail "HTML missing intro switch"
echo "$html" | grep -q 'id="agent-intro"' || fail "HTML missing intro editor"
echo "$html" | grep -q 'I am on deck. Standing orders are noted.' \
    || fail "HTML missing default intro"
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
# Pre-walk r3 (Ops 2): the robot-editor pass label uses the same plain
# words as the backup step; the retrieve command sits behind a details line.
echo "$html" | grep -q -F 'Checked to save its key in your password manager.' || fail "pass checkbox copy"
echo "$html" | grep -q -F '<details class="howto" id="agent-pass-howto"><summary>How to find it later</summary>' || fail "robot pass how-to details"
echo "$html" | grep -q -F 'pass ls hush/agents' || fail "robot pass retrieve command"
echo "$html" | grep -q -F '&lt;robot-id&gt;' && fail "robot pass howto must not invent robot-id" || true
echo "$html" | grep -q 'Unix Password Manager' && fail "no Unix Password Manager jargon in the UI"
# Pre-walk r5 (Ops FAIL-2): the profile Picture row is hidden whole, with
# no placeholder copy (nothing saves a profile picture yet).
echo "$html" | grep -q -F '<div id="prof-avatar-row" hidden>' || fail "profile picture row hidden"
echo "$html" | grep -q 'Not available yet' && fail "no placeholder copy in the profile"
echo "$html" | grep -q 'pass show hush/identity/nsec' || fail "retrieve CLI"
echo "$html" | grep -q 'id=\\\"save-pass\\\"' || fail "pass checkbox id"
echo "$html" | grep -q 'savePass = true' || fail "checkbox defaults on"
echo "$html" | grep -q 'lastGateHtml' || fail "gate paint must skip unchanged trees"
echo "$html" | grep -q 'data-lpignore' || fail "nsec input must ignore password-manager autofill"
echo "$html" | grep -q 'dialog class=\\\"secret\\\"' || fail "secret modal"
# #234 B2: preview derives npub without logging in (mutant that also launches must die).
prev=$(curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"preview","nsec":"nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5"}')
echo "$prev" | grep -q '"ok":true' || fail "preview should return ok"
echo "$prev" | grep -q '"npub":"npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg"' \
    || fail "preview should return the known matching npub"
echo "$prev" | grep -q '"logged_in"' && fail "preview must not be a session reply"
after_prev=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$after_prev" | grep -q '"logged_in":false' || fail "preview must leave session logged out"
echo "$after_prev" | grep -q '"npub":""' || fail "preview must leave session npub empty"

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
echo "$vibe" | grep -q '"slug":"marshal"' || fail "marshal template missing"
echo "$vibe" | grep -q '"name":"Marshal"' || fail "marshal name missing"
echo "$vibe" | grep -q '"role":"chaperon"' || fail "marshal chaperon role missing"
echo "$vibe" | grep -q 'panel:robots:0' || fail "coach icon missing"
echo "$vibe" | grep -q 'panel:robots:1' || fail "major icon missing"
echo "$vibe" | grep -q 'panel:robots:2' || fail "auditor icon missing"
echo "$vibe" | grep -q 'panel:angevin:3' || fail "marshal icon missing"
echo "$vibe" | grep -q 'system:human-cue' || fail "marshal missing human-cue"
echo "$vibe" | grep -q 'system:token-budget' || fail "marshal missing token-budget"
echo "$vibe" | grep -q 'system:hive-patterns' || fail "major missing hive-patterns"
echo "$vibe" | grep -q 'system:bring-back' && fail "folded bring-back must be absent"
echo "$vibe" | grep -q '"locked":true' || fail "locked template missing"
echo "$vibe" | grep -q 'sgt-major-payne-copy' && fail "seed must not clone Major"
echo "$vibe" | grep -q '"name":"Sgt Major Payne"' && fail "old Payne display name must not ship"
echo "$vibe" | grep -q 'Sgt. Maj. Payne' && fail "old Payne display name must not ship"
echo "$vibe" | grep -F '"providers":["grok-build"]' || fail "Payne default providers"
echo "$vibe" | grep -q '"slug":"welcome"' || fail "welcome channel missing"
echo "$vibe" | grep -q '"theme":"field-office"' || fail "default theme missing"
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
    -d '{"name":"alpha","path":"'"$proj_alpha"'","git":"true"}')
echo "$proj" | grep -q '"slug":"alpha"' || fail "project create"
test -d "$proj_alpha/.git" || fail "git init"
can=$(curl -sf -X POST "http://127.0.0.1:${port}/api/canvas" \
    -H 'Content-Type: application/json' \
    -d '{"project":"alpha","path":"snippet-1.py","content":"print(1)"}')
echo "$can" | grep -q '"ok":true' || fail "canvas save"
test -f "$proj_alpha/snippet-1.py" || fail "canvas file missing"
grep -q 'print(1)' "$proj_alpha/snippet-1.py" || fail "canvas content"
badcan=$(curl -s -o "$bad_canvas" -w '%{http_code}' -X POST \
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
prunedcopy=$(curl -s -o "$min1_copy" -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"coach-copy","skill_0":"","nskills":0}')
test "$prunedcopy" != "200" || fail "PE-3 min-1 must refuse unequip to empty"
keptcopy=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$keptcopy" | grep -q '"name":"Coach copy"[^}]*"skills":\["system:canvas-coach"\]' \
    || fail "refused unequip must keep the worn skill"
echo "$keptcopy" | grep -q '"name":"Coach","slug":"coach"[^}]*"skills":\["system:canvas-coach"\]' \
    || fail "locked coach must keep skill after copy prune"
noclone=$(curl -s -o "$no_major_clone" -w '%{http_code}' \
    -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"clone","slug":"sgt-major-payne"}')
test "$noclone" != "200" || fail "Major must not clone"
ui=$(curl -sf -X POST "http://127.0.0.1:${port}/api/skillui" \
    -H 'Content-Type: application/json' \
    -d '{"html":"body{color:#112233;font-family:sans-serif;padding:8px}","name":"fixture"}')
echo "$ui" | grep -q '#112233' || fail "skillui color"
echo "$ui" | grep -q 'sans-serif' || fail "skillui font"
badrole=$(curl -s -o "$bad_role" -w '%{http_code}' \
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
pruned=$(curl -s -o "$min1_sentry" -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"sentry","skill_0":""}')
test "$pruned" != "200" || fail "PE-3 min-1 must refuse last-skill unequip"
keptsentry=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$keptsentry" | grep -q '"name":"Sentry"[^}]*system:forge-skill' \
    || fail "refused unequip must keep sentry skill"
forged=$(curl -sf -X POST "http://127.0.0.1:${port}/api/skill" \
    -H 'Content-Type: application/json' \
    -d '{"name":"joke-book","summary":"Jokes.","body":"Tell one joke.","scope":"user"}')
echo "$forged" | grep -q 'user:joke-book' || fail "forge user skill"
skills=$(curl -sf "http://127.0.0.1:${port}/api/skills")
echo "$skills" | grep -q 'user:joke-book' || fail "catalog missing forged joke-book"
echo "$skills" | grep -q '"id":"user:joke-book","name":"joke-book","scope":"system"' \
    || fail "user-dir skill must present as system"
robotskill=$(curl -sf -X POST "http://127.0.0.1:${port}/api/skill" \
    -H 'Content-Type: application/json' \
    -d '{"name":"futurama","summary":"Sarcastic delivery.","body":"Be Bender.","scope":"robot","robot":"sentry"}')
echo "$robotskill" | grep -q 'robot:sentry:futurama' || fail "forge robot skill"
cross=$(curl -s -o "$cross_skill" -w '%{http_code}' \
    -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"coach","skill_0":"robot:sentry:futurama"}')
test "$cross" != "200" || fail "robot skill must not equip on another slug"
own=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"action":"update","slug":"sentry","skill_0":"robot:sentry:futurama"}')
echo "$own" | grep -q 'robot:sentry:futurama' || fail "robot skill must equip on owner"
noprov=$(curl -s -o "$noprov_agent" -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Ghost","system_prompt":"Watch.","save_pass":false}')
test "$noprov" != "200" || fail "provider required"
bad=$(curl -s -o "$bad_agent" -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
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
stay=$(curl -s -o "$payne_del" -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/agent" \
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

# #234 F-B + B1: tick block must syncIdentityViaImport after session=sess
# and route !logged_in → landing (awk scoped to tick — not vacuous greps).
awk '
  /async function tick\(\)/ { in_tick=1; next }
  in_tick && /^    (async )?function / { in_tick=0 }
  in_tick && /session = sess;/ { sess=NR }
  in_tick && sess && NR==sess+1 && /syncIdentityViaImport\(\);/ { sync=1 }
  in_tick && /!session\.logged_in/ { lo=1 }
  in_tick && lo && /page = "landing"/ { land=1 }
  END {
    if (!sync) { print "tick must call syncIdentityViaImport right after session = sess" > "/dev/stderr"; exit 1 }
    if (!land) { print "tick must set page=landing when !session.logged_in" > "/dev/stderr"; exit 1 }
  }
' demo/index.html || fail "tick must syncIdentityViaImport and route logged_out to landing"

# #234 F-A: #npub-preview wrap CSS present (UI@375 pin is in check_restart_ui).
awk '
  /#npub-preview \{/ { blk=1 }
  blk && /overflow-wrap: anywhere/ { ow=1 }
  blk && /word-break: break-all/ { wb=1 }
  blk && /\.gate \.card/ { blk=0 }
  END {
    if (!ow || !wb) { print "#npub-preview must set overflow-wrap/word-break (F-A)" > "/dev/stderr"; exit 1 }
  }
' demo/index.html || fail "F-A #npub-preview wrap CSS missing"
echo "$html" | grep -q -F '.gate .card { max-width: min(34rem, 100%); overflow-x: hidden; box-sizing: border-box; }' \
  || fail "F-A .gate .card must hide horizontal overflow"

# #237: paintSkillBoard before drawer show (gem flash).
# Extract openAgentDrawer and require paintSkillBoard line number < show line.
awk '
  /function openAgentDrawer\(bot\)/ { in_fn=1 }
  in_fn && /paintSkillBoard\(bot && bot\.slug\);/ { p=NR }
  in_fn && /\$\("agent-drawer"\)\.classList\.add\("show"\);/ { s=NR; if (p>0 && p<s) ok=1; in_fn=0 }
  END { if (!ok) { print "paintSkillBoard must precede agent-drawer show" > "/dev/stderr"; exit 1 } }
' demo/index.html || fail "paintSkillBoard must precede agent-drawer show in openAgentDrawer"

# #210 PE-4.1: drawer trusts API list order (no localeCompare re-sort); FULL cue.
echo "$html" | grep -q -F 'id="fav-full"'   || fail "PE-4.1 #fav-full missing next to Save"
echo "$html" | grep -q -F 'function paintFavFull()'   || fail "PE-4.1 paintFavFull missing"
if echo "$html" | grep -A12 'function refreshFavorites' | grep -E '\.sort\(.*localeCompare' >/dev/null; then
  fail "PE-4.1 drawer must not localeCompare-sort favorites"
fi
# list_json must stay under write-legible-c 40-line hard cap.
awk '
  /^hush_status_t hush_favorite_list_json\(/ { start=NR; depth=0; next }
  start {
    depth += gsub(/\{/, "{") - gsub(/\}/, "}")
    if (depth <= 0) {
      n = NR - start + 1
      if (n > 40) {
        print "hush_favorite_list_json is " n " lines (cap 40)" > "/dev/stderr"
        exit 1
      }
      exit 0
    }
  }
  END { if (start && depth > 0) { print "list_json unclosed" > "/dev/stderr"; exit 1 } }
' src/hush_favorite.c || fail "PE-4.1 list_json must be <=40 lines"

echo "launch routes ok"
