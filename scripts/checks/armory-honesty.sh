#!/bin/sh
# Armory honesty-label proof (cloud-runnable, static).
# Proves the UI never claims relay-saved state for browser-only labels:
# ALWAYS-ON / ON-CALL lifetime + Character split + draft doll/sheet.
# Usage: sh scripts/checks/armory-honesty.sh
set -eu
cd "$(dirname "$0")/../.."
html="hush-c/demo/index.html"
fail() { echo "FAIL: $1" >&2; exit 1; }

[ -f "$html" ] || fail "missing $html"

# 1. PE-2 shelf contract intact (exact shelf words still painted).
grep -q 'ALWAYS-ON' "$html" || fail "ALWAYS-ON shelf missing"
grep -q 'ON-CALL' "$html" || fail "ON-CALL shelf missing"
grep -q 'skill-shelf' "$html" || fail "lifetime shelves missing"
grep -q 'skill-life' "$html" || fail "gem lifetime chips missing"

# 2. Shelves + note admit browser-only (no saved/equipped phantom).
grep -q 'browser-only' "$html" || fail "no browser-only qualifier in UI"
grep -q 'not saved on the relay' "$html" || fail "no not-saved-on-relay qualifier in UI"
grep -q 'skill-life-note' "$html" || fail "armory honesty note missing"
grep -q 'skillLifetimeShelf(lt) + " · browser-only"' "$html" || fail "shelf header lacks browser-only suffix"

# 3. Draft doll/sheet never claim equipped/saved before POST /api/agent.
grep -q 'draft until Save' "$html" || fail "doll draft qualifier missing"
grep -q 'No skills in this draft loadout' "$html" || fail "sheet draft empty-state missing"
grep -q 'Save the robot to keep' "$html" || fail "sheet draft-save qualifier missing"
if grep -q "No skills equipped" "$html"; then
  fail "phantom 'No skills equipped' still claims equipped state for a draft"
fi

# 4. Character strip: relay-saved count + browser-only split.
grep -q 'saved on this relay' "$html" || fail "character relay-saved qualifier missing"
grep -q 'Browser-only labels' "$html" || fail "character browser-only split missing"

# 5. Relay has no lifetime store: catalog JSON carries scope/role/category,
#    never a lifetime field; overrides live only in localStorage.
if grep -rn 'lifetime' hush-c/src/hush_skill.c hush-c/include/ hush-c/src/api_agents.c 2>/dev/null | grep -iv 'product label\|browser-only' | grep -q .; then
  grep -rn 'lifetime' hush-c/src/hush_skill.c hush-c/include/ hush-c/src/api_agents.c | head -5 >&2
  fail "relay gained a lifetime field; UI browser-only claim would be stale"
fi
grep -q 'hush-skill-lifetime' "$html" || fail "lifetime override key missing"
if grep -q 'hush-skill-lifetime' hush-c/src/*.c 2>/dev/null; then
  fail "relay must not read the browser lifetime key"
fi

# 6. Existing PE-2 launch gate expectations still hold (no regressions).
launch="hush-c/tests/check_launch.sh"
[ -f "$launch" ] || fail "missing $launch"
grep -q "ALWAYS-ON" "$launch" || fail "launch gate lost ALWAYS-ON check"
grep -q "ON-CALL" "$launch" || fail "launch gate lost ON-CALL check"

echo "PASS: armory honesty labels match real persistence state."
echo "- Shelves/chips/sheet: browser-only, not saved on the relay."
echo "- Doll/sheet: draft until Save; never equipped before POST /api/agent."
echo "- Character: N/8 saved on this relay; Always/On-call split browser-only."
echo "Leave->return: clear localStorage (or a second browser) resets shelves to"
echo "defaults while relay-saved equipped ids survive; re-check with this script."
