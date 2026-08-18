#!/bin/sh
# Test double for hush-pass. Store lives under HUSH_FAKE_PASS_DIR.
set -eu
dir="${HUSH_FAKE_PASS_DIR:-/tmp/hush-fake-pass}"
mkdir -p "$dir"
cmd="${1:-}"
path="${2:-}"
file="$dir/$(printf '%s' "$path" | tr '/' '_')"
case "$cmd" in
    save)
        cat > "$file"
        ;;
    get)
        [ -f "$file" ] || exit 1
        cat "$file"
        ;;
    has)
        [ -f "$file" ] || exit 1
        ;;
    *)
        echo "fake-pass: usage save|get|has <path>" >&2
        exit 2
        ;;
esac
