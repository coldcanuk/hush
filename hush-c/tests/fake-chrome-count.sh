#!/bin/sh
# Wrapper: append one line per Chrome spawn, then run the real fake.
set -eu
log="${HUSH_FAKE_CHROME_LAUNCH_LOG:-}"
if [ -n "$log" ]; then
    printf 'launch\n' >> "$log"
fi
exec "$(dirname "$0")/fake-chrome-devtools.sh" "$@"
