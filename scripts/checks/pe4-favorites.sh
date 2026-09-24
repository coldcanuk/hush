#!/bin/sh
# PE-4 favorites proof (cloud-runnable, static).
# Proves Journey D save/load/unload/delete holds in #agent-drawer with
# relay-side persistence, and that PE-3 min-1 + armory honesty survive.
# Usage: sh scripts/checks/pe4-favorites.sh
set -eu
cd "$(dirname "$0")/../.."
html="hush-c/demo/index.html"
fav_c="hush-c/src/hush_favorite.c"
fav_h="hush-c/include/hush_favorite.h"
api="hush-c/src/api_favorite.c"
home="hush-c/src/hush_home.c"
http="hush-c/src/hush_http.c"
fail() { echo "FAIL: $1" >&2; exit 1; }

[ -f "$html" ] || fail "missing $html"
[ -f "$fav_c" ] || fail "missing $fav_c"
[ -f "$fav_h" ] || fail "missing $fav_h"
[ -f "$api" ] || fail "missing $api"

# 1. Relay persists per-robot JSON sets under robots/<slug>/loadouts/.
grep -q 'hush_home_loadouts_dir' "$home" || fail "home loadouts path missing"
grep -q 'hush_home_loadouts_dir' "$fav_c" \
    || fail "favorite paths must build through hush_home_loadouts_dir"
grep -q 'HUSH_HOME_DIR_LOADOUTS' hush-c/include/hush_home.h \
    || fail "loadouts dir constant missing"
grep -q 'HUSH_SKILL_EQUIP_LOW' "$fav_c" || fail "favorites min bound missing"
grep -q 'HUSH_SKILL_EQUIP_MAX' "$fav_c" || fail "favorites max bound missing"
grep -q 'rename(tmp, path)' "$fav_c" || fail "favorite write must be atomic"
grep -q 'hush_skill_find' "$fav_c" || fail "save must validate skill ids"
grep -q 'hush_skill_robot_ok' "$fav_c" || fail "save must refuse cross-slug ids"

# 2. POST /api/loadout serves save/list/load/delete with refusals.
grep -q 'hush_http_serve_loadout' "$http" || fail "/api/loadout route missing"
grep -q '"/api/loadout"' "$http" || fail "/api/loadout path missing"
for act in '"save"' '"list"' '"load"' '"delete"'; do
    grep -q "$act" "$api" || fail "loadout action $act missing"
done
grep -q 'nids == 0' "$fav_c" || fail "favorite save must refuse the empty set"

# 3. Drawer strip: save refuses empty name / 0 skills; load is atomic;
#    unload clears the highlight only; delete never touches the doll.
for id in 'id="fav-strip"' 'id="fav-name"' 'id="fav-save"' \
    'id="fav-unload"' 'id="fav-list"'; do
    grep -q "$id" "$html" || fail "drawer missing $id"
done
grep -q 'Name the favorite before saving' "$html" \
    || fail "save must refuse an empty name"
grep -q 'equippedSkills = valid.slice()' "$html" \
    || fail "load must replace the doll atomically"
grep -q 'has no skills on this relay' "$html" \
    || fail "load must refuse a favorite with zero valid skills"
if grep -A6 'function unloadFavorite' "$html" | grep -q 'equippedSkills'; then
    fail "unload must clear the highlight only, never the doll"
fi
if grep -A12 'function deleteFavorite' "$html" | grep -q 'equippedSkills'; then
    fail "delete must remove the list entry only, never the doll"
fi
grep -q 'Raise the robot before saving favorites' "$html" \
    || fail "new robots without a slug must not save favorites"

# 4. PE-3 min-1 still enforced client + server.
n=$(grep -c 'Keep at least one skill equipped' "$html" || true)
[ "$n" -ge 4 ] || fail "expected >=4 min-1 block sites, found $n"
grep -q 'in->nskills < (size_t)HUSH_SKILL_EQUIP_LOW' hush-c/src/api_agents.c \
    || fail "server empty-write guard missing"

# 5. Honesty: lifetime stays browser-only; favorites say relay-saved.
grep -q 'saved on this relay' "$html" || fail "favorites relay-saved note missing"
grep -q 'browser-only' "$html" || fail "browser-only qualifier missing"
if grep -rn 'lifetime' hush-c/src/hush_favorite.c hush-c/src/api_favorite.c \
    2>/dev/null | grep -q .; then
    fail "relay gained a lifetime field; browser-only claim would be stale"
fi
if grep -q 'favorite' hush-c/src/agent_prompt.c 2>/dev/null; then
    fail "prompt tiers must stay untouched by PE-4"
fi

# 6. Traversal: robot slug allowlisted at every entry; paths stay put.
grep -q 'hush_home_is_robot_slug' hush-c/include/hush_home.h \
    || fail "shared slug validator missing"
for fn in 'hush_favorite_save' 'hush_favorite_load' 'hush_favorite_delete' \
    'hush_favorite_list_json'; do
    grep -q "$fn" "$fav_c" || fail "missing $fn"
done
n=$(grep -c 'hush_favorite_loadouts(dir, sizeof dir, robot)' "$fav_c" || true)
[ "$n" -eq 4 ] || fail "all 4 entries must validate robot first, found $n"
grep -q 'hush_favorite_is_under_dir' "$fav_c" \
    || fail "dir containment missing"
if grep -A20 'hush_status_t hush_favorite_load(' "$fav_c" \
    | grep -q 'ensure_loadouts'; then
    fail "load must never mkdir"
fi
if grep -A20 'hush_status_t hush_favorite_delete(' "$fav_c" \
    | grep -q 'ensure_loadouts'; then
    fail "delete must never mkdir"
fi
if [ "$(grep -c 'hush_home_ensure_loadouts' "$fav_c")" -ne 1 ]; then
    fail "only the save commit path may create dirs"
fi
grep -q 'hush_home_ensure_loadouts' hush-c/include/hush_home.h \
    || fail "tree creation must live in the home module"
for dup in 'hush_favorite_slugify' 'hush_favorite_make_tree' \
    'hush_favorite_join' 'static hush_status_t hush_favorite_mkdir'; do
    if grep -q "$dup" "$fav_c"; then
        fail "shared helper copied instead of reused: $dup"
    fi
done
grep -q 'void hush_skill_slugify' hush-c/include/hush_skill.h \
    || fail "slugify must be shared through hush_skill.h"

# 7. Caps, clashes, cleanup, escapes: honest errors, no silent loss.
grep -q 'HUSH_ERR_DENIED' "$fav_h" || fail "DENIED contract missing"
grep -q 'count >= (size_t)HUSH_FAVORITE_COUNT_MAX' "$fav_c" \
    || fail "33rd favorite must be refused"
grep -q 'scan->dropped' "$fav_c" || fail "list must report truncation"
grep -q 'JSON_MAX' "$fav_c" || fail "save must fit the list envelope"
grep -q 'strcmp(stored, draft->name) != 0' "$fav_c" \
    || fail "clash check missing"
grep -q 'alias' "$fav_h" || fail "alias addressing must be documented"
grep -q 'clashes with a saved favorite' "$html" || fail "clash copy missing"
n=$(grep -c 'unlink(tmp)' "$fav_c" || true)
[ "$n" -ge 3 ] || fail "tmp file must be cleaned on error, found $n"
grep -q 'hush_favorite_escape' "$api" || fail "API must check escapes"
if grep -q 'hush_json_escape(' "$api"; then
    fail "API must use the checked escape wrapper"
fi
if [ "$(grep -c 'hush_json_escape(' "$fav_c")" -ne 1 ]; then
    fail "exactly one raw escape site (inside the wrapper)"
fi
grep -q 'hush_loadout_reply_favorite' "$api" || fail "load split missing"
grep -q '"skill_8"' "$api" || fail "skill_8 overflow must be refused"
if grep -q '\[24\]' "$api" hush-c/src/hush_favorite.c; then
    fail "key buffers must be named constants"
fi
if grep -q '0700' hush-c/src/hush_favorite.c; then
    fail "dir mode must be the shared constant"
fi
if grep -q 'strcpy' "$fav_c"; then
    fail "bounded copies must use memcpy"
fi

echo "PASS: PE-4 favorites hold (save/load/unload/delete, relay-saved)."
echo "- Drawer: empty-name and 0-skill saves refused; load atomic, never empty."
echo "- Unload clears the highlight only; delete removes the entry only."
echo "- Relay: robots/<slug>/loadouts/ JSON sets, 1-8 ids, save-time validation."
echo "- PE-3 min-1 + armory honesty preserved."
echo "Live proof: sh hush-c/tests/check_launch.sh + sh scripts/checks/pe3-min1.sh"
echo "  + sh scripts/checks/armory-honesty.sh + python3 hush-c/tests/check_collaboration.py"
