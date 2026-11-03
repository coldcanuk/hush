#!/bin/sh
# Test double for hush-pass. Store lives under HUSH_FAKE_PASS_DIR.
# umask 077 makes a new store dir 0700 and a new secret file 0600; the
# dir chmod resets a store dir that already existed with a wider mode, and
# save removes an old file and makes the new one under its own umask 077,
# so the file is 0600 from birth and no write lands before the chmod.
set -eu
umask 077
dir="${HUSH_FAKE_PASS_DIR:-/tmp/hush-fake-pass}"
mkdir -p "$dir"
chmod 700 "$dir"
cmd="${1:-}"
path="${2:-}"
file="$dir/$(printf '%s' "$path" | tr '/' '_')"
case "$cmd" in
    save)
        # Remove first, so no secret byte is written into a file that
        # already had a wider mode. The new file is made under umask 077,
        # scoped to this subshell, so it is born 0600 even if the chmod
        # below were dropped; the chmod stays as a second guard.
        rm -f "$file"
        (umask 077 && : > "$file")
        chmod 600 "$file"
        cat > "$file"
        ;;
    get)
        [ -f "$file" ] || exit 1
        cat "$file"
        ;;
    has)
        [ -f "$file" ] || exit 1
        ;;
    rm)
        # #264 O1: wipe must run while vibe.json still lists the slug.
        # Flat vibe keys look like "agent_slug_3":"sentry". A mutant that
        # clears after favorites+roster+vibe save sees agent_in_vibe=0.
        if [ -n "${HUSH_FAKE_PASS_WIPE_LOG:-}" ]; then
            vibe="${HUSH_CONFIG_DIR:-}/vibe.json"
            if [ ! -f "$vibe" ] && [ -n "${HUSH_HOME:-}" ]; then
                vibe="${HUSH_HOME}/config/vibe.json"
            fi
            slug=$(printf '%s' "$path" | sed -n 's|^agents/\([^/]*\)/nsec$|\1|p')
            present=0
            if [ -n "$slug" ] && [ -f "$vibe" ]; then
                if grep -E -q "\"agent_slug_[0-9]+\":\"${slug}\"" "$vibe"; then
                    present=1
                fi
            fi
            printf '{"path":"%s","slug":"%s","agent_in_vibe":%s}\n' \
                "$path" "$slug" "$present" > "$HUSH_FAKE_PASS_WIPE_LOG"
        fi
        rm -f "$file"
        ;;
    *)
        echo "fake-pass: usage save|get|has|rm <path>" >&2
        exit 2
        ;;
esac
