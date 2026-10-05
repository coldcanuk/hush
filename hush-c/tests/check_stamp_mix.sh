#!/bin/sh
# Regression: objects that embed a commit SHA must all embed the same one,
# and it must be the short HEAD. A stale api_status.o beside a rebuilt
# hush_relay_main.o fails this without starting the relay.
set -eu
cd "$(dirname "$0")/.."
fail() { echo "mixed stamp check failed: $1" >&2; exit 1; }
command -v strings >/dev/null 2>&1 || fail "strings not on PATH"
command -v git >/dev/null 2>&1 || fail "git not on PATH"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "not a git checkout"
short=$(git rev-parse --short HEAD)
head_full=$(git rev-parse HEAD)
min=${#short}
found=$(mktemp)
trap 'rm -f "$found"' EXIT
: >"$found"
for obj in src/*.o; do
    [ -f "$obj" ] || continue
    strings -a "$obj" | while IFS= read -r cand; do
        case "$cand" in
            *[!0-9a-f]*) continue ;;
        esac
        n=${#cand}
        if [ "$n" -lt "$min" ] || [ "$n" -gt 40 ]; then
            continue
        fi
        full=$(git rev-parse --verify --quiet "${cand}^{commit}" 2>/dev/null || true)
        if [ -z "$full" ]; then
            continue
        fi
        printf '%s %s\n' "$full" "$obj" >>"$found"
    done
done
if [ ! -s "$found" ]; then
    fail "no object embeds a commit SHA"
fi
distinct=$(awk '{print $1}' "$found" | sort -u)
count=$(printf '%s\n' "$distinct" | wc -l | tr -d ' ')
if [ "$count" -ne 1 ]; then
    echo "mixed build SHAs:" >&2
    sort -u "$found" >&2
    fail "objects embed $count commit SHAs"
fi
only=$(printf '%s\n' "$distinct")
if [ "$only" != "$head_full" ]; then
    echo "embedded $only" >&2
    sort -u "$found" >&2
    fail "embedded SHA is not HEAD $short"
fi
echo "stamp objects agree ($short)"
