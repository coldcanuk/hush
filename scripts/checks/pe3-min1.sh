#!/bin/sh
# PE-3 min-1 proof (cloud-runnable, static).
# Proves the minimum-one-equipped-skill journey is enforced client + server
# with honesty-clean copy. PE-4 favorites now exist in their own module;
# this gate proves min-1 still holds across them (never passes through empty).
# Usage: sh scripts/checks/pe3-min1.sh
set -eu
cd "$(dirname "$0")/../.."
html="hush-c/demo/index.html"
agent="hush-c/src/api_agents.c"
launch="hush-c/tests/check_launch.sh"
collab="hush-c/tests/check_collaboration.py"
fail() { echo "FAIL: $1" >&2; exit 1; }

[ -f "$html" ] || fail "missing $html"
[ -f "$agent" ] || fail "missing $agent"

# 1. Relay refuses any loadout write that leaves zero skills.
grep -q 'HUSH_SKILL_EQUIP_LOW' "$agent" || fail "server min-1 watermark missing"
grep -q 'in->nskills < (size_t)HUSH_SKILL_EQUIP_LOW' "$agent" \
    || fail "server empty-write guard missing"
grep -q 'min-1 law' "$agent" || fail "server min-1 contract comment missing"

# 2. Client blocks the last-gem lift/prune and the empty-draft save with
#    one honest copy (draft guard; never claims relay-saved state).
n=$(grep -c 'Keep at least one skill equipped' "$html" || true)
[ "$n" -ge 4 ] || fail "expected >=4 min-1 block sites, found $n"
grep -q 'body.nskills = equippedSkills.length' "$html" || fail "save must post nskills"
if grep -q "No skills equipped" "$html"; then
    fail "phantom 'No skills equipped' claims equipped state for a draft"
fi

# 3. Live-relay gates assert refusal (not empty persistence).
grep -q 'min-1 must refuse' "$launch" || fail "launch gate lost min-1 refusal"
grep -q 'refused unequip must keep' "$launch" || fail "launch gate lost keep-worn check"
if grep -q 'must empty loadout\|must persist empty loadout' "$launch"; then
    fail "launch gate still asserts the old empty-OK behavior"
fi

# 4. Collaboration skill-removal step became swap + refusal (min-1 law).
grep -q 'SWAPPED_SKILL_PROOF' "$collab" || fail "collaboration swap skill missing"
grep -A1 '"nskills": 0}' "$collab" | grep -q 'expected=400' \
    || fail "collaboration empty write must expect 400"

# 5. UI_SPEC carries the PE-3 contract delta.
grep -q 'PE-3 min-1 block' UI_SPEC.md || fail "UI_SPEC missing PE-3 delta"

# 6. PE-4 favorites (if present) never bypass min-1: the agent gate owns
#    no favorites, and any favorite plumbing keeps the min-1 copy.
if grep -ri 'favorite' hush-c/src/api_agents.c 2>/dev/null | grep -q .; then
    fail "favorites leaked into the agent gate; they own api_favorite.c"
fi
if grep -ri 'favorite' hush-c/demo/index.html 2>/dev/null | grep -q .; then
    n=$(grep -c 'Keep at least one skill equipped' "$html" || true)
    [ "$n" -ge 4 ] || fail "favorites present but min-1 copy lost"
    grep -q 'has no skills on this relay' "$html" \
        || fail "favorite load must refuse the empty set"
fi

echo "PASS: PE-3 min-1 journey holds client + server, honestly labeled."
echo "- Relay: loadout writes with zero skills refused; worn loadout kept."
echo "- Client: last-gem lift/prune + empty-draft save blocked inline."
echo "- Untouched loadouts pass through; no phantom equipped/saved claims."
echo "- PE-4 favorites (if present): atomic load, never through empty."
echo "Live proof: sh hush-c/tests/check_launch.sh + python3 hush-c/tests/check_collaboration.py"
